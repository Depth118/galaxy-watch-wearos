/*
 * r800charger: off-mode charging screen for the SM-R800 Wear OS build.
 *
 * When the watch is powered off and put on the charger, S-Boot starts the
 * kernel with "systemd.unit=charging-mode.target" (Tizen's LPM). start.sh then
 * execs this instead of booting Android. It:
 *   - shows Samsung's charging look (ring + bolt from param's lpm.jpg, the
 *     percentage in param's pow_digital_num digits), the ring filled to the
 *     battery level; the screen turns off after a few seconds
 *   - short power-key press: screen on again
 *   - long press (1.5 s): reboot into Wear OS
 *   - charger removed: power off (as stock)
 *   - runs wpcd so the wireless pad keeps charging (no P9222 kernel driver)
 *
 * Assets (raw RGB, from tools/charger/mkassets.sh) in /data/r800dev/charger/.
 * Static armv7a NDK build (runs before Android exists): tools/charger/build.sh
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/reboot.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/reboot.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* assets and wpcd sit next to the executable (/data/r800dev/charger/ before
 * Android, /oem/r800dev/charger/ under it) */
static char adir[200] = "/data/r800dev/charger/";
#define W 360
#define H 360
#define DW 55
#define DH 110
#define PW 36
#define BW 80
#define BH 60
#define SCREEN_MS 8000
#define LONG_MS 1500
#define UNPLUG_CHECKS 3

static uint8_t dig[10][DW * DH * 3], pct[PW * DH * 3], bolt[BW * BH * 3];
static uint32_t frame[W * H];
static uint32_t *fb;
static int fbfd = -1, line_px = W;
static struct fb_var_screeninfo var;

/* ring: pixel index + its clockwise position from 12 o'clock (0..1) */
#define RING_IN 166.0
#define RING_OUT 176.0
static int ring_n;
static int ring_idx[16000];
static float ring_pos[16000];

static void klog(const char *fmt, ...)
{
	char b[256];
	va_list a;
	int fd = open("/dev/kmsg", O_WRONLY), n;
	va_start(a, fmt);
	n = snprintf(b, sizeof(b), "<3>r800charger: ");
	n += vsnprintf(b + n, sizeof(b) - n, fmt, a);
	va_end(a);
	if (fd >= 0) {
		write(fd, b, n < (int)sizeof(b) ? n : (int)sizeof(b) - 1);
		close(fd);
	}
}

static int load(const char *name, void *buf, size_t n)
{
	char p[256];
	int fd, r;
	snprintf(p, sizeof(p), "%s%s", adir, name);
	fd = open(p, O_RDONLY);
	if (fd < 0)
		return -1;
	r = read(fd, buf, n);
	close(fd);
	return r == (int)n ? 0 : -1;
}

static int readstr(const char *path, char *buf, int n)
{
	int fd = open(path, O_RDONLY), r;
	if (fd < 0)
		return -1;
	r = read(fd, buf, n - 1);
	close(fd);
	if (r < 0)
		r = 0;
	buf[r] = 0;
	while (r > 0 && (buf[r - 1] == '\n' || buf[r - 1] == ' '))
		buf[--r] = 0;
	return r;
}

static int readint(const char *path, int def)
{
	char b[32];
	return readstr(path, b, sizeof(b)) > 0 ? atoi(b) : def;
}

static void writestr(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY);
	if (fd >= 0) {
		write(fd, s, strlen(s));
		close(fd);
	}
}

static long long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void blit(const uint8_t *s, int w, int h, int x0, int y0)
{
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			const uint8_t *p = s + (y * w + x) * 3;
			int X = x0 + x, Y = y0 + y;
			if (X < 0 || X >= W || Y < 0 || Y >= H)
				continue;
			/* lighten: glyph cells overlap, their black backgrounds must not cut */
			uint32_t o = frame[Y * W + X];
			int r = p[0] > ((o >> 16) & 255) ? p[0] : (o >> 16) & 255;
			int g = p[1] > ((o >> 8) & 255) ? p[1] : (o >> 8) & 255;
			int b = p[2] > (o & 255) ? p[2] : o & 255;
			frame[Y * W + X] = 0xff000000u | r << 16 | g << 8 | b;
		}
}

static void ring_init(void)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			double dx = x - 179.5, dy = y - 179.5, r = sqrt(dx * dx + dy * dy);
			if (r < RING_IN || r > RING_OUT || ring_n >= 16000)
				continue;
			double a = atan2(dx, -dy);	/* 0 at 12 o'clock, clockwise */
			if (a < 0)
				a += 2 * M_PI;
			ring_idx[ring_n] = y * W + x;
			ring_pos[ring_n++] = (float)(a / (2 * M_PI));
		}
}

static uint32_t mix(uint32_t c, double k)
{
	int r = ((c >> 16) & 255) * k, g = ((c >> 8) & 255) * k, b = (c & 255) * k;
	return 0xff000000u | r << 16 | g << 8 | b;
}

/*
 * Display rotation from the accelerometer (0..3 = 0/90/180/270 degrees, as
 * Android's ROTATION_*): on the dock gravity reads +X (the screen's right edge
 * points up; verified 2026-11-20, Wear OS's ROTATION_90 reads upright there).
 */
static int rot;

static void present(void)
{
	if (!fb)
		return;
	for (int y = 0; y < H; y++) {
		uint32_t *d = fb + y * line_px;
		for (int x = 0; x < W; x++) {
			int sx, sy;	/* frame pixel shown at screen (x, y) */
			switch (rot) {
			case 1: sx = y; sy = W - 1 - x; break;		/* content top -> screen right */
			case 2: sx = W - 1 - x; sy = H - 1 - y; break;
			case 3: sx = H - 1 - y; sy = x; break;		/* content top -> screen left */
			default: sx = x; sy = y;
			}
			d[x] = frame[sy * W + sx];
		}
	}
	var.yoffset = 0;
	ioctl(fbfd, FBIOPAN_DISPLAY, &var);
}

static void draw(int level, int full)
{
	char s[8];
	int n, total, x;
	/* stock lpm.jpg ring is dark green; the charged part is the bright green */
	uint32_t on = level <= 15 && !full ? 0xffe0533du : 0xff4cd964u;
	uint32_t off = 0xff1b2a12u;

	memset(frame, 0, sizeof(frame));
	for (int i = 0; i < ring_n; i++) {
		int p = ring_idx[i], y = p / W, xx = p % W;
		double dx = xx - 179.5, dy = y - 179.5, r = sqrt(dx * dx + dy * dy);
		/* soften the ring's edges */
		double k = r - RING_IN < 1 ? r - RING_IN : RING_OUT - r < 1 ? RING_OUT - r : 1;
		frame[p] = mix(ring_pos[i] * 100 <= level || full ? on : off, k);
	}
	blit(bolt, BW, BH, (W - BW) / 2, 78);
	n = snprintf(s, sizeof(s), "%d", full ? 100 : level);
	total = n * (DW - 6) + PW;
	x = (W - total) / 2;
	for (int i = 0; i < n; i++, x += DW - 6)
		blit(dig[s[i] - '0'], DW, DH, x - 4, 150);
	blit(pct, PW, DH, x + 2, 150);

	present();
}

static int screen_on = -1;
static void screen(int on)
{
	if (on == screen_on || fbfd < 0)
		return;
	if (ioctl(fbfd, FBIOBLANK, on ? FB_BLANK_UNBLANK : FB_BLANK_POWERDOWN))
		klog("FBIOBLANK %d: %s\n", on, strerror(errno));
	if (on)
		writestr("/sys/class/backlight/exynos_panel/brightness", "110");
	screen_on = on;
}

static int find_input(const char *want)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;
	char p[64], name[64];
	int fd = -1;
	while (d && (e = readdir(d))) {
		if (strncmp(e->d_name, "event", 5))
			continue;
		snprintf(p, sizeof(p), "/dev/input/%s", e->d_name);
		int f = open(p, O_RDONLY | O_NONBLOCK);
		if (f < 0)
			continue;
		name[0] = 0;
		ioctl(f, EVIOCGNAME(sizeof(name)), name);
		if (!strcmp(name, want)) {
			fd = f;
			break;
		}
		close(f);
	}
	if (d)
		closedir(d);
	return fd;
}

static int plugged(void)
{
	return readint("/sys/class/power_supply/ac/online", 0) > 0 ||
	       readint("/sys/class/power_supply/usb/online", 0) > 0;
}

static void start_wpcd(void)
{
	pid_t p = fork();
	if (p == 0) {
		char lp[256], wp[256];
		snprintf(lp, sizeof(lp), "%swpcd.log", adir);
		snprintf(wp, sizeof(wp), "%swpcd", adir);
		int fd = open(lp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd >= 0) {
			dup2(fd, 1);
			dup2(fd, 2);
		}
		execl(wp, "wpcd", "loop", "5", (char *)0);
		_exit(127);
	}
	klog("wpcd pid %d\n", p);
}

/*
 * Accelerometer through the SSP hub (lhd, started by start.sh, loads its MCU
 * patch first): enable bit 0 of ssp_sensor/enable (keep the other bits), raw
 * REL_X/Y/Z in 0.244 mg/LSB (~4096 = 1 g), same axes as Android's.
 */
#define SSP "/sys/class/sensors/ssp_sensor/"
static int acc_fd = -1, acc_v[3], acc_have;
static long long acc_enabled_at;

static void acc_enable(long long t)
{
	char b[32];
	long long m = readstr(SSP "enable", b, sizeof(b)) > 0 ? atoll(b) : 0;
	writestr(SSP "accel_poll_delay", "200000000");
	snprintf(b, sizeof(b), "%lld", m | 1);
	writestr(SSP "enable", b);
	acc_enabled_at = t;
}

/* rotation for one sample, -1 if unclear (e.g. moving) */
static int acc_rotation(void)
{
	int x = acc_v[0], y = acc_v[1];
	if (x > 2500) return 1;
	if (x < -2500) return 3;
	if (y < -2500) return 2;
	return 0;
}

static void go(int cmd, const char *why)
{
	klog("%s\n", why);
	sync();
	if (cmd == LINUX_REBOOT_CMD_RESTART2)
		syscall(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd, "");
	else
		syscall(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd, 0);
	klog("reboot syscall failed: %s\n", strerror(errno));
}

static void load_assets(void)
{
	char e[200];
	int n = readlink("/proc/self/exe", e, sizeof(e) - 1);
	if (n > 0) {
		e[n] = 0;
		char *sl = strrchr(e, '/');
		if (sl) {
			sl[1] = 0;
			snprintf(adir, sizeof(adir), "%s", e);
		}
	}
	for (int i = 0; i < 10; i++) {
		char nm[8];
		snprintf(nm, sizeof(nm), "d%d.rgb", i);
		if (load(nm, dig[i], sizeof(dig[i])))
			klog("missing %s\n", nm);
	}
	if (load("pct.rgb", pct, sizeof(pct)) || load("bolt.rgb", bolt, sizeof(bolt)))
		klog("missing pct/bolt\n");
	ring_init();
}

int main(int argc, char **argv)
{
	struct fb_fix_screeninfo fix;
	char st[32];
	int key, level = -1, full = 0, unplug = 0, down = 0;
	long long t_down = 0, t_off, t_poll = 0;

	/* r800charger --dump <file> <level> [rot]: render one frame as shown (BGRA) */
	if ((argc == 4 || argc == 5) && !strcmp(argv[1], "--dump")) {
		static uint32_t shown[W * H];
		int fd;
		load_assets();
		rot = argc == 5 ? atoi(argv[4]) & 3 : 0;
		fb = shown;	/* present() into a buffer instead of the display */
		line_px = W;
		draw(atoi(argv[3]), atoi(argv[3]) >= 100);
		fd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (fd < 0 || write(fd, shown, sizeof(shown)) != sizeof(shown))
			return 1;
		close(fd);
		return 0;
	}

	signal(SIGCHLD, SIG_IGN);
	klog("start\n");
	writestr("/proc/r800_deadman", "off");
	writestr("/sys/module/decon/parameters/r800_keep_panel_on", "0");
	load_assets();

	fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd >= 0 && !ioctl(fbfd, FBIOGET_VSCREENINFO, &var) &&
	    !ioctl(fbfd, FBIOGET_FSCREENINFO, &fix) && var.bits_per_pixel == 32) {
		line_px = fix.line_length / 4;
		fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
		if (fb == MAP_FAILED)
			fb = 0;
	}
	klog("fb %d mapped %d\n", fbfd, fb != 0);

	start_wpcd();
	key = find_input("s2mpw02-power-keys");
	klog("power key fd %d\n", key);

	screen(1);
	t_off = now_ms() + SCREEN_MS;
	for (;;) {
		struct pollfd pfs[2] = { { key, POLLIN, 0 }, { acc_fd, POLLIN, 0 } };
		struct pollfd pf;
		long long t = now_ms();
		int timeout = down ? 100 : 500;
		poll(pfs, 2, timeout);
		pf = pfs[0];
		t = now_ms();

		/* the hub comes up a few seconds after lhd: keep looking / enabling */
		if (acc_fd < 0 && t - acc_enabled_at > 2000) {
			acc_fd = find_input("accelerometer_sensor");
			acc_enabled_at = t;
		}
		if (acc_fd >= 0 && !acc_have && t - acc_enabled_at > 2000)
			acc_enable(t);
		if (acc_fd >= 0 && (pfs[1].revents & POLLIN)) {
			struct input_event ev;
			static int last = -1, same;
			while (read(acc_fd, &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type == EV_REL && ev.code <= REL_Z)
					acc_v[ev.code] = ev.value;
				if (ev.type != EV_SYN)
					continue;
				if (!acc_have)
					klog("accelerometer up: %d %d %d\n", acc_v[0], acc_v[1], acc_v[2]);
				acc_have = 1;
				int r = acc_rotation();
				same = r == last ? same + 1 : 0;
				last = r;
				/* two equal samples (0.4 s) before turning */
				if (same >= 1 && r != rot) {
					klog("rotation %d -> %d (%d %d %d)\n", rot, r, acc_v[0], acc_v[1], acc_v[2]);
					rot = r;
					if (screen_on == 1 && level >= 0)
						draw(level, full);
				}
			}
		}

		if (key >= 0 && (pf.revents & POLLIN)) {
			struct input_event ev;
			while (read(key, &ev, sizeof(ev)) == sizeof(ev)) {
				if (ev.type != EV_KEY || ev.code != KEY_POWER)
					continue;
				if (ev.value == 1 && !down) {
					down = 1;
					t_down = t;
				} else if (ev.value == 0 && down) {
					down = 0;
					screen(1);
					draw(level < 0 ? 0 : level, full);
					t_off = t + SCREEN_MS;
				}
			}
		}
		if (down && t - t_down >= LONG_MS) {
			screen(1);
			memset(frame, 0, sizeof(frame));
			if (fb)
				for (int y = 0; y < H; y++)
					memset(fb + y * line_px, 0, W * 4);
			go(LINUX_REBOOT_CMD_RESTART2, "long press: booting Wear OS");
		}

		if (t - t_poll >= 2000) {
			int l = readint("/sys/class/power_supply/battery/capacity", 0);
			readstr("/sys/class/power_supply/battery/status", st, sizeof(st));
			int f = !strcmp(st, "Full");
			t_poll = t;
			if ((l != level || f != full) && screen_on == 1)
				draw(l, f);
			if (l != level || f != full)
				klog("level %d status %s\n", l, st);
			level = l;
			full = f;
			unplug = plugged() ? 0 : unplug + 1;
			if (unplug >= UNPLUG_CHECKS)
				go(LINUX_REBOOT_CMD_POWER_OFF, "charger removed: power off");
		}
		if (screen_on == 1 && level >= 0) {
			static int drawn;
			if (!drawn) {
				draw(level, full);
				drawn = 1;
			}
		}
		if (screen_on == 1 && t >= t_off && !down)
			screen(0);
	}
}
