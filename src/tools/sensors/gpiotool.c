/*
 * gpiotool - minimal GPIO chardev helper for the SM-R800 Wear OS port.
 *
 *   gpiotool find <bank>            print chip + line info for every line of <bank> (e.g. gpm13)
 *   gpiotool hold <bank> <line> <v> request the line as output = v and keep it held (forever)
 *
 * Uses the v1 GPIO chardev ABI (linux/gpio.h, kernel 4.19).
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int open_bank(const char *bank, struct gpiochip_info *ci)
{
	char path[64];
	for (int i = 0; i < 256; i++) {
		snprintf(path, sizeof(path), "/dev/gpiochip%d", i);
		int fd = open(path, O_RDWR);
		if (fd < 0)
			continue;
		if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, ci) == 0 && strcmp(ci->label, bank) == 0) {
			printf("%s: label=%s name=%s lines=%u\n", path, ci->label, ci->name, ci->lines);
			return fd;
		}
		close(fd);
	}
	return -1;
}

int main(int argc, char **argv)
{
	struct gpiochip_info ci;
	if (argc < 3) {
		fprintf(stderr, "usage: gpiotool find <bank> | hold <bank> <line> <value>\n");
		return 2;
	}
	int fd = open_bank(argv[2], &ci);
	if (fd < 0) {
		fprintf(stderr, "bank %s not found\n", argv[2]);
		return 1;
	}
	if (!strcmp(argv[1], "find")) {
		for (unsigned i = 0; i < ci.lines; i++) {
			struct gpioline_info li = { .line_offset = i };
			if (ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &li) == 0)
				printf("  line %u: name=%s consumer=%s flags=0x%x\n", i, li.name, li.consumer, li.flags);
		}
		return 0;
	}
	if (!strcmp(argv[1], "hold") && argc >= 5) {
		struct gpiohandle_request rq;
		memset(&rq, 0, sizeof(rq));
		rq.lineoffsets[0] = atoi(argv[3]);
		rq.lines = 1;
		rq.flags = GPIOHANDLE_REQUEST_OUTPUT;
		rq.default_values[0] = atoi(argv[4]);
		strcpy(rq.consumer_label, "gpiotool");
		if (ioctl(fd, GPIO_GET_LINEHANDLE_IOCTL, &rq) < 0) {
			fprintf(stderr, "linehandle: %s\n", strerror(errno));
			return 1;
		}
		printf("holding %s-%s = %s\n", argv[2], argv[3], argv[4]);
		fflush(stdout);
		for (;;)
			pause();
	}
	fprintf(stderr, "bad command\n");
	return 2;
}
