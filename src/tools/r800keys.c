/*
 * r800keys: top button (gpio_keys KEY_BACK) on the watch face turns the screen
 * off. Neither SM-R800 button maps to POWER, and Back does nothing on the
 * watch face. Runs from android-helper.sh as root.
 *
 * A virtual uinput keyboard is created once at start, so the SLEEP key is
 * delivered instantly (the "input keyevent" command starts a Java VM: ~2.7 s).
 *
 * Fires only when the screen was already on at the press (a wake press must
 * not sleep again) and the watch-face window is visible (tiles, notifications
 * and apps hide it).
 *
 * The watch-face check (dumpsys, 0.3-1.7 s) runs ahead of time: 0.7 s after
 * the last touch/button activity, at low priority, and is cached, so the
 * press itself sends SLEEP at once.
 *
 * Bottom button (s2mpw02-power-keys, KEY_POWER; its keylayout made it HOME):
 * grabbed here and re-sent, because Wear OS does nothing on a HOME long press
 * and holding on ran into the PMIC's hardware reset and S-Boot's boot menu.
 *   short press: HOME (KEY_HOMEPAGE), or just WAKEUP when the screen is off
 *   double press (within 0.35 s): Gemini; single press waits that long
 *   hold 0.6 s:  POWER held for 0.8 s = Android's long press = power menu
 *
 *   r800keys <top button eventN> [other input eventN ...]
 */
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>

#define DOUBLE_MS 350		/* bottom button double press = Gemini */

/*
 * Bottom button double press opens Gemini (the Pixel Watch holds the crown for
 * it; here holding is the power menu). Neither KEYCODE_ASSIST nor VOICE_ASSIST
 * reaches it on this Wear OS build (tested 2026-11-20), so start the ASSIST intent. "am" is a Java tool (~1 s), run detached so keys stay live.
 */
static void launch_gemini(void)
{
	pid_t p = fork();
	if (p == 0) {
		setsid();
		if (fork() == 0) {
			/* the assist action, resolved by Android: Gemini's activity names
			 * change (ClassicAssistantActivity vanished once it became a
			 * system app); this opens its "Ask Gemini" screen in ~0.7 s */
			execl("/system/bin/am", "am", "start", "-a", "android.intent.action.ASSIST",
			      "-p", "com.google.android.wearable.assistant", "-f", "0x10000000",
			      (char *)0);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0)
		waitpid(p, 0, 0);
}

#define HOLD_MS 600		/* bottom button held this long = power menu */
#define POWER_HOLD_MS 800	/* POWER down this long: Android's long press (500 ms) */

/* DECON state: "ON@60Hz"/"HBM@60Hz" when fully on, "LP@..." in ambient (doze),
 * "OFF". The backlight is not enough: ambient keeps it at 26, and a top press
 * there then woke the watch only to be put straight back to sleep. */
#define DECON_STATE "/sys/devices/platform/148b0000.decon_f/decon_power_state"

static int screen_on(void)
{
	char b[16] = "";
	int fd = open(DECON_STATE, O_RDONLY), n;
	if (fd < 0)
		return 0;
	n = read(fd, b, sizeof(b) - 1);
	close(fd);
	return n > 0 && (!strncmp(b, "ON", 2) || !strncmp(b, "HBM", 3));
}

/* watch-face window visible? ("Window #" line, then its isVisible=true) */
static int face_visible(void)
{
	char line[512], win[512] = "";
	int face = 0;
	FILE *p = popen("exec /system/bin/nice -n 10 /system/bin/dumpsys window windows 2>/dev/null", "r");
	if (!p)
		return 0;
	while (fgets(line, sizeof(line), p)) {
		if (strstr(line, "Window #"))
			strncpy(win, line, sizeof(win) - 1);
		else if (strstr(line, "isVisible=true") && strstr(win, "WatchFaceService"))
			face = 1;
	}
	pclose(p);
	return face;
}

/*
 * Is an app other than Wear's own UI in front? Android gives the top app
 * oom_score_adj 0; with the watch face up only sysui, the Wear app and the
 * face process have it (checked 2026-11-20). A few ms, unlike dumpsys: the
 * cached face check is refreshed 0.7 s after the last touch, but apps take
 * 1-3 s to open here, so a top press in a just-opened app saw "watch face"
 * and put the screen to sleep instead of going back.
 */
static int other_app_in_front(void)
{
	static const char *wear[] = { "com.google.android.wearable.sysui",
		"com.google.android.wearable.app", "com.google.android.wearable.watchface", 0 };
	DIR *d = opendir("/proc");
	struct dirent *e;
	int found = 0;
	while (d && !found && (e = readdir(d))) {
		char p[64], b[128];
		int fd, n, i, ours = 0;
		if (e->d_name[0] < '1' || e->d_name[0] > '9')
			continue;
		snprintf(p, sizeof(p), "/proc/%s/oom_score_adj", e->d_name);
		fd = open(p, O_RDONLY);
		if (fd < 0)
			continue;
		n = read(fd, b, sizeof(b) - 1);
		close(fd);
		if (n <= 0 || b[0] != '0' || (b[1] != '\n' && b[1] != 0))
			continue;
		snprintf(p, sizeof(p), "/proc/%s/cmdline", e->d_name);
		fd = open(p, O_RDONLY);
		if (fd < 0)
			continue;
		n = read(fd, b, sizeof(b) - 1);
		close(fd);
		if (n <= 0)
			continue;	/* kernel thread */
		b[n] = 0;
		/* apps run as their package name ("com.x.y[:proc]"); native daemons and
		 * scripts ("sh /data/r800dev/start.sh") have paths or no dot */
		if (!strchr(b, '.') || strchr(b, '/') || strchr(b, ' '))
			continue;
		for (i = 0; wear[i]; i++)
			if (!strncmp(b, wear[i], strlen(wear[i])))
				ours = 1;
		if (!ours)
			found = 1;
	}
	if (d)
		closedir(d);
	return found;
}

static void emit(int fd, int type, int code, int value)
{
	struct input_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;
	if (write(fd, &ev, sizeof(ev)) != sizeof(ev))
		perror("uinput write");
}

static int make_uinput(void)
{
	struct uinput_user_dev u;
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("/dev/uinput");
		return -1;
	}
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, KEY_SLEEP);
	ioctl(fd, UI_SET_KEYBIT, KEY_WAKEUP);
	ioctl(fd, UI_SET_KEYBIT, KEY_HOMEPAGE);
	ioctl(fd, UI_SET_KEYBIT, KEY_POWER);
	memset(&u, 0, sizeof(u));
	snprintf(u.name, UINPUT_MAX_NAME_SIZE, "r800-keys");
	u.id.bustype = BUS_VIRTUAL;
	u.id.vendor = 0x0800;
	u.id.product = 0x0001;
	if (write(fd, &u, sizeof(u)) != sizeof(u) || ioctl(fd, UI_DEV_CREATE)) {
		perror("uinput create");
		close(fd);
		return -1;
	}
	return fd;
}

static long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

static void tap(int out, int code)
{
	emit(out, EV_KEY, code, 1);
	emit(out, EV_SYN, SYN_REPORT, 0);
	emit(out, EV_KEY, code, 0);
	emit(out, EV_SYN, SYN_REPORT, 0);
}

static void key(int out, int code, int value)
{
	emit(out, EV_KEY, code, value);
	emit(out, EV_SYN, SYN_REPORT, 0);
}

static void send_sleep(int out)
{
	int k = open("/dev/kmsg", O_WRONLY);
	if (k >= 0) {
		write(k, "<6>r800: r800keys: top button on watch face -> sleep\n", 53);
		close(k);
	}
	emit(out, EV_KEY, KEY_SLEEP, 1);
	emit(out, EV_SYN, SYN_REPORT, 0);
	emit(out, EV_KEY, KEY_SLEEP, 0);
	emit(out, EV_SYN, SYN_REPORT, 0);
}

/*
 * Ambient (DECON "LP@..."): Wear keeps touch input live and hands touches and
 * Back to the UI, but nothing wakes the system, so the UI ran dim at ambient
 * rate and dozed off again. Any touch or key press there sends WAKEUP, as a
 * touch controller's wake gesture would on other watches.
 */
static int in_ambient(void)
{
	char b[16] = "";
	int fd = open(DECON_STATE, O_RDONLY), n;
	if (fd < 0)
		return 0;
	n = read(fd, b, sizeof(b) - 1);
	close(fd);
	return n > 0 && !strncmp(b, "LP", 2);
}

static void maybe_wake(int out, const struct input_event *ev)
{
	static long last;
	long t;

	/* key press, new touch, or a bezel detent (tizen_detent, REL_WHEEL) */
	if (!((ev->type == EV_KEY && ev->value == 1) ||
	      (ev->type == EV_ABS && ev->code == ABS_MT_TRACKING_ID && ev->value >= 0) ||
	      (ev->type == EV_REL && ev->value != 0)))
		return;
	t = now_ms();
	if (t - last < 500 || !in_ambient())
		return;
	last = t;
	emit(out, EV_KEY, KEY_WAKEUP, 1);
	emit(out, EV_SYN, SYN_REPORT, 0);
	emit(out, EV_KEY, KEY_WAKEUP, 0);
	emit(out, EV_SYN, SYN_REPORT, 0);
}

int main(int argc, char **argv)
{
	struct pollfd pf[8];
	struct input_event ev;
	int n = 0, i, out, lit = 0, dirty = 1, face = 0, pwr = -1, app_up = 0;
	long home_due = 0;	/* single press HOME, sent once no second press came */
	long deadline = now_ms() + 700;
	/* bottom button state: pressed at, screen on at press, POWER sent (down at) */
	long b_down = 0, p_down = 0;
	int b_held = 0, b_lit = 0, p_sent = 0, p_up_pending = 0;

	if (argc < 2 || argc > 9) {
		fprintf(stderr, "usage: r800keys <top button eventN> [other eventN ...]\n");
		return 2;
	}
	for (i = 1; i < argc; i++) {
		pf[n].fd = open(argv[i], O_RDONLY);
		if (pf[n].fd < 0) {
			perror(argv[i]);
			if (i == 1)
				return 1;
			continue;
		}
		{
			char name[64] = "";
			ioctl(pf[n].fd, EVIOCGNAME(sizeof(name)), name);
			if (!strcmp(name, "s2mpw02-power-keys")) {
				if (ioctl(pf[n].fd, EVIOCGRAB, 1) == 0)
					pwr = n;
				else
					perror("EVIOCGRAB power key");
			}
		}
		pf[n++].events = POLLIN;
	}
	out = make_uinput();
	if (out < 0)
		return 1;
	for (;;) {
		long t = now_ms();
		int timeout = dirty ? (int)(deadline > t ? deadline - t : 0) : -1;

		/* bottom button timers: the hold threshold, then POWER's minimum hold */
		if (b_held && !p_sent) {
			int w = (int)(b_down + HOLD_MS - t);
			if (w < 0) w = 0;
			if (timeout < 0 || w < timeout) timeout = w;
		}
		if (p_up_pending) {
			int w = (int)(p_down + POWER_HOLD_MS - t);
			if (w < 0) w = 0;
			if (timeout < 0 || w < timeout) timeout = w;
		}
		if (home_due) {
			int w = (int)(home_due - t);
			if (w < 0) w = 0;
			if (timeout < 0 || w < timeout) timeout = w;
		}
		int pr = poll(pf, n, timeout);
		t = now_ms();
		/* no second press within DOUBLE_MS: it was a single press */
		if (home_due && t >= home_due) {
			tap(out, KEY_HOMEPAGE);
			home_due = 0;
		}
		if (b_held && !p_sent && t - b_down >= HOLD_MS) {
			key(out, KEY_POWER, 1);	/* long press: power menu */
			p_sent = 1;
			p_down = t;
		}
		if (p_up_pending && t - p_down >= POWER_HOLD_MS) {
			key(out, KEY_POWER, 0);
			p_up_pending = 0;
		}
		if (pr == 0 && (b_held || p_up_pending || p_sent))
			continue;
		if (pr == 0) {
			/* quiet for 0.7 s: refresh the cached watch-face state */
			if (screen_on())
				face = face_visible();
			dirty = 0;
			continue;
		}
		for (i = 0; i < n; i++) {
			if (!(pf[i].revents & POLLIN))
				continue;
			if (read(pf[i].fd, &ev, sizeof(ev)) != sizeof(ev))
				continue;
			if (i == pwr) {
				if (ev.type != EV_KEY || ev.code != KEY_POWER)
					continue;
				if (ev.value == 1 && !b_held) {
					b_held = 1;
					b_down = now_ms();
					b_lit = screen_on() || in_ambient();
					p_sent = 0;
					if (!b_lit)
						tap(out, KEY_WAKEUP);	/* screen off: wake on press */
				} else if (ev.value == 0 && b_held) {
					b_held = 0;
					if (p_sent) {
						/* keep POWER down long enough to count as a long press */
						if (now_ms() - p_down >= POWER_HOLD_MS)
							key(out, KEY_POWER, 0);
						else
							p_up_pending = 1;
						p_sent = 0;
					} else if (!b_lit) {
						;	/* woke on the press */
					} else {
						/*
						 * HOME waits DOUBLE_MS for a second press: a HOME
						 * in between makes Android hold back the Gemini
						 * start (app-switch protection after HOME; the
						 * start was logged but never created, 2026-11-20).
						 */
						if (in_ambient()) {
							tap(out, KEY_WAKEUP);
						} else if (home_due) {
							home_due = 0;
							launch_gemini();	/* second press: assistant */
						} else {
							home_due = now_ms() + DOUBLE_MS;
						}
					}
				}
				dirty = 1;
				deadline = now_ms() + 700;
				continue;
			}
			maybe_wake(out, &ev);
			if (i == 0 && ev.type == EV_KEY && ev.code == KEY_BACK) {
				if (ev.value == 1) {
					lit = screen_on();
					/* checked at the press, before Back closes it */
					app_up = lit && other_app_in_front();
				} else if (ev.value == 0 && lit) {
					if (dirty)
						face = face_visible();	/* pressed right after activity */
					{
						char m[96];
						int k = open("/dev/kmsg", O_WRONLY), l;
						l = snprintf(m, sizeof(m), "<6>r800: r800keys: top release face=%d app=%d dirty=%d\n", face, app_up, dirty);
						if (k >= 0) { write(k, m, l); close(k); }
					}
					if (face && !app_up) {
						send_sleep(out);
						face = 0;
					}
				}
			}
			/* Any input may change what is on screen, except the top
			 * button's own press and sync: nothing changes until the
			 * release, and treating the press as activity made the
			 * release run the slow check synchronously (2.2 s). */
			if (i == 0 && (ev.type == EV_SYN ||
				       (ev.type == EV_KEY && ev.code == KEY_BACK && ev.value == 1)))
				continue;
			dirty = 1;
			deadline = now_ms() + 700;
		}
	}
	return 0;
}
