/*
 * r800wdui: screen for wireless download (Odin over Wi-Fi) on the SM-R800
 * Wear OS build.
 *
 * start.sh runs Tizen's own download tool (wirelesstool-init, from the
 * recovery ramdisk on p7) when S-Boot boots with bootmode=wireless-download.
 * That tool draws through Tizen's libtdm, which cannot drive our kernel's
 * display, so it runs headless and this program shows its state instead,
 * read from the tool's log:
 *
 *   r800wdui <wd_log path>
 *
 * The log keeps earlier sessions: each session starts with the tool's
 * "<wireless-tools" banner, which resets the state.
 * Holding the bottom (power) key for 3 s before a PC connects restarts the
 * watch into Wear OS (the kernel resets S-Boot's boot mode to normal).
 * Static armv7a NDK build: tools/charger/build.sh. Font: wdfont.h (mkfont.py).
 */
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/reboot.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "wdfont.h"

#define W 360
#define H 360

static uint32_t frame[W * H];
static uint32_t *fb;
static int fbfd = -1, line_px = W;
static struct fb_var_screeninfo var;

static void writestr(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY);
	if (fd >= 0) {
		write(fd, s, strlen(s));
		close(fd);
	}
}

struct face {
	int h;
	const uint16_t *w;
	const uint32_t *off;
	const uint8_t *px;
};
static const struct face faces[2] = {
	{ F0_H, f0_w, f0_off, f0_px },
	{ F1_H, f1_w, f1_off, f1_px },
};

static int glyph_w(const struct face *f, int c)
{
	if (c < 32 || c > 126)
		c = '?';
	/* the renderer's space is too narrow for a text line */
	return c == ' ' ? f->h / 4 : f->w[c - 32];
}

static int text_w(const struct face *f, const char *s)
{
	int w = 0;
	for (; *s; s++)
		w += glyph_w(f, (unsigned char)*s);
	return w;
}

/* centered line, top edge at y, color 0xRRGGBB */
static void text(int fi, int y, uint32_t rgb, const char *s)
{
	const struct face *f = &faces[fi];
	int x = (W - text_w(f, s)) / 2;
	for (; *s; s++) {
		int c = (unsigned char)*s, gw;
		if (c < 32 || c > 126)
			c = '?';
		gw = glyph_w(f, c);
		if (c != ' ') {
			const uint8_t *g = f->px + f->off[c - 32];
			for (int yy = 0; yy < f->h; yy++) {
				if (y + yy < 0 || y + yy >= H)
					continue;
				for (int xx = 0; xx < f->w[c - 32]; xx++) {
					int a = g[yy * f->w[c - 32] + xx], px = x + xx;
					if (!a || px < 0 || px >= W)
						continue;
					uint32_t r = ((rgb >> 16) & 255) * a / 255;
					uint32_t gg = ((rgb >> 8) & 255) * a / 255;
					uint32_t b = (rgb & 255) * a / 255;
					frame[(y + yy) * W + px] = 0xff000000u | r << 16 | gg << 8 | b;
				}
			}
		}
		x += gw;
	}
}

static void present(void)
{
	if (!fb || fb == frame)
		return;
	for (int y = 0; y < H; y++)
		memcpy(fb + y * line_px, frame + y * W, W * 4);
	var.yoffset = 0;
	ioctl(fbfd, FBIOPAN_DISPLAY, &var);
}

enum { ST_WPS, ST_AP, ST_LINKED, ST_FLASHING, ST_REBOOT };

static void draw(int st, const char *ssid)
{
	memset(frame, 0, sizeof(frame));
	text(1, 62, 0xffffff, "Wireless download");
	switch (st) {
	case ST_WPS:
		text(0, 124, 0xbdbdbd, "Looking for Wi-Fi (WPS)...");
		text(0, 172, 0x8ab4f8, "Press the bottom button");
		text(0, 203, 0x8ab4f8, "twice for hotspot mode");
		break;
	case ST_AP:
		text(0, 124, 0xbdbdbd, "On the PC, join Wi-Fi");
		text(1, 158, 0x8ab4f8, ssid[0] ? ssid : "SM-R800");
		text(0, 206, 0xbdbdbd, "then start Odin / netodin");
		break;
	case ST_LINKED:
		text(0, 140, 0x81c995, "Connected to the PC");
		text(0, 172, 0xbdbdbd, "Waiting for the flash...");
		break;
	case ST_FLASHING:
		text(0, 140, 0x81c995, "Flashing...");
		break;
	case ST_REBOOT:
		text(0, 140, 0x81c995, "Done, restarting");
		break;
	}
	if (st == ST_WPS || st == ST_AP) {
		text(0, 250, 0x9e9e9e, "Hold the bottom button");
		text(0, 280, 0x9e9e9e, "to cancel");
	} else if (st != ST_REBOOT) {
		text(0, 250, 0xf28b82, "Do not turn off the watch");
	}
	present();
}

static long long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int find_input(const char *want)
{
	DIR *d = opendir("/dev/input");
	struct dirent *e;
	char p[300], name[64];
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

int main(int argc, char **argv)
{
	struct fb_fix_screeninfo fix;
	char ssid[40] = "", buf[4096], line[512];
	int st = ST_WPS, shown = -1, ll = 0, key;
	long long down = 0, next_log = 0;
	off_t pos = 0;
	struct stat sb;

	/* preview: r800wdui --dump <state 0..4> <out.bgra> [ssid] */
	if ((argc == 4 || argc == 5) && !strcmp(argv[1], "--dump")) {
		int o;
		fb = frame;
		draw(atoi(argv[2]), argc == 5 ? argv[4] : "");
		o = open(argv[3], O_WRONLY | O_CREAT | O_TRUNC, 0644);
		write(o, frame, sizeof(frame));
		close(o);
		return 0;
	}
	if (argc != 2) {
		fprintf(stderr, "usage: r800wdui <wd_log>\n");
		return 2;
	}

	writestr("/sys/module/decon/parameters/r800_keep_panel_on", "1");
	fbfd = open("/dev/fb0", O_RDWR);
	if (fbfd >= 0 && !ioctl(fbfd, FBIOGET_VSCREENINFO, &var) &&
	    !ioctl(fbfd, FBIOGET_FSCREENINFO, &fix) && var.bits_per_pixel == 32) {
		line_px = fix.line_length / 4;
		fb = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
		if (fb == MAP_FAILED)
			fb = 0;
	}
	if (!fb) {
		fprintf(stderr, "r800wdui: no framebuffer (%s)\n", strerror(errno));
		return 1;
	}
	ioctl(fbfd, FBIOBLANK, FB_BLANK_UNBLANK);
	writestr("/sys/class/backlight/exynos_panel/brightness", "110");
	key = find_input("s2mpw02-power-keys");

	for (;;) {
		struct pollfd pf = { key, POLLIN, 0 };
		struct input_event ev;
		long long t;
		int fd, n;

		/* the tool reads the same key (two presses: hotspot mode); no grab */
		poll(&pf, key >= 0 ? 1 : 0, 200);
		while (key >= 0 && read(key, &ev, sizeof(ev)) == sizeof(ev))
			if (ev.type == EV_KEY && ev.code == KEY_POWER)
				down = ev.value ? (down ? down : now_ms()) : 0;
		t = now_ms();
		if (down && t - down >= 3000 && (st == ST_WPS || st == ST_AP)) {
			memset(frame, 0, sizeof(frame));
			text(1, 150, 0xffffff, "Restarting");
			present();
			sync();
			reboot(RB_AUTOBOOT);
		}
		if (t < next_log)
			continue;
		next_log = t + 500;
		fd = open(argv[1], O_RDONLY);

		/* the log restarts from 0 when the tool rotates it */
		if (fd >= 0 && !fstat(fd, &sb) && sb.st_size < pos)
			pos = 0;
		while (fd >= 0 && (n = pread(fd, buf, sizeof(buf), pos)) > 0) {
			pos += n;
			for (int i = 0; i < n; i++) {
				char *p;
				if (buf[i] != '\n') {
					if (ll < (int)sizeof(line) - 1)
						line[ll++] = buf[i];
					continue;
				}
				line[ll] = 0;
				ll = 0;
				if (strstr(line, "<wireless-tools")) {
					st = ST_WPS;
					ssid[0] = 0;
				} else if (strstr(line, "Connection manager start: 1") || strstr(line, "wd_cm_setup_ap"))
					st = ST_AP;
				else if (strstr(line, "Connection manager start: 0"))
					st = ST_WPS;
				else if ((p = strstr(line, "SSID: "))) {
					snprintf(ssid, sizeof(ssid), "%s", p + 6);
					if (st < ST_AP)
						st = ST_AP;
				} else if (strstr(line, "Connection established"))
					st = ST_LINKED;
				else if (strstr(line, "progress_bar_update") || strstr(line, "wd_core_write_to_emmc"))
					st = st < ST_FLASHING ? ST_FLASHING : st;
				else if (strstr(line, "wd_util_reboot") || strstr(line, "wd_sys_reboot"))
					st = ST_REBOOT;
			}
		}
		if (fd >= 0)
			close(fd);
		if (st != shown) {
			draw(st, ssid);
			shown = st;
		}
	}
}
