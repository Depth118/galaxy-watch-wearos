/*
 * ukey - instant key injection for the SM-R800 Wear OS helper.
 *
 * `input keyevent` starts a Java process (~2.3 s on this watch), far too slow for
 * Wear's "ungaze" (screen off when the wrist turns away). ukey registers a uinput
 * keyboard once and then presses a key for every line read from stdin:
 *
 *   ukey < /dev/r800_ukey        lines: "sleep" -> KEY_SLEEP, "wakeup" -> KEY_WAKEUP
 *
 * Android's Generic.kl maps 142 to SLEEP and 143 to WAKEUP.
 */
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static void emit(int fd, int type, int code, int value)
{
	struct input_event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.code = code;
	ev.value = value;
	(void)write(fd, &ev, sizeof(ev));
}

static void press(int fd, int code)
{
	emit(fd, EV_KEY, code, 1);
	emit(fd, EV_SYN, SYN_REPORT, 0);
	emit(fd, EV_KEY, code, 0);
	emit(fd, EV_SYN, SYN_REPORT, 0);
}

int main(void)
{
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (fd < 0) {
		perror("uinput");
		return 1;
	}
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_KEYBIT, KEY_SLEEP);
	ioctl(fd, UI_SET_KEYBIT, KEY_WAKEUP);

	struct uinput_user_dev ud;
	memset(&ud, 0, sizeof(ud));
	snprintf(ud.name, UINPUT_MAX_NAME_SIZE, "r800-ukey");
	ud.id.bustype = BUS_VIRTUAL;
	ud.id.vendor = 0x5252;
	ud.id.product = 0x0800;
	ud.id.version = 1;
	if (write(fd, &ud, sizeof(ud)) != sizeof(ud) || ioctl(fd, UI_DEV_CREATE) < 0) {
		perror("uinput create");
		return 1;
	}
	fprintf(stderr, "ukey: ready\n");

	char line[64];
	while (fgets(line, sizeof(line), stdin)) {
		if (!strncmp(line, "sleep", 5))
			press(fd, KEY_SLEEP);
		else if (!strncmp(line, "wakeup", 6))
			press(fd, KEY_WAKEUP);
	}
	ioctl(fd, UI_DEV_DESTROY);
	close(fd);
	return 0;
}
