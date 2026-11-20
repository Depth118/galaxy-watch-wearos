/*
 * r800boost: touch boost for the SM-R800 (2x Cortex-A53, one cpufreq policy).
 *
 * The stock power HAL here is the do-nothing default, so nothing raises the CPU
 * when a finger lands: schedutil only ramps after it has seen the load, and the
 * first frames of every swipe miss their deadline. This daemon watches the
 * touchscreen and buttons, pins scaling_min_freq to the top frequency on the
 * first event, and drops it back BOOST_MS after the last one.
 *
 * GPU: when the Mali governor owns the clock (GPU_SCALE: dvfs_min_lock reads
 * -1 at start), the GPU is locked to GPU_BOOST MHz for the same window. From
 * its 100 MHz idle the Interactive governor needs several frames to ramp, and
 * those are the dropped frames at the start of every swipe. If the helper pins
 * the GPU instead, the lock is left alone.
 *
 * BOOST_MS covers the fling animation that keeps running after the finger
 * lifts (the last input event), not just the touch itself.
 *
 *   r800boost [-t ms] <eventN> [eventN ...]   (run as root from android-helper.sh)
 */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <linux/input.h>

#define POLICY   "/sys/devices/system/cpu/cpufreq/policy0/"
#define GPU      "/sys/class/misc/mali0/device/"
#define BOOST_MS 1000
#define GPU_BOOST "667"
#define MAX_DEV  8

static int write_str(const char *path, const char *s)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	int ok;
	if (fd < 0)
		return -1;
	ok = write(fd, s, strlen(s)) == (ssize_t)strlen(s);
	close(fd);
	return ok ? 0 : -1;
}

static int read_str(const char *path, char *buf, size_t len)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n;
	if (fd < 0)
		return -1;
	n = read(fd, buf, len - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = 0;
	buf[strcspn(buf, " \n")] = 0;
	return 0;
}

static long long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
	struct pollfd pfd[MAX_DEV];
	char max_f[32], min_f[32];
	char gpu_lock[32] = "";
	int n = 0, boosted = 0, i, gpu = 0, boost_ms = BOOST_MS;
	long long until = 0;

	if (read_str(POLICY "cpuinfo_max_freq", max_f, sizeof(max_f)) ||
	    read_str(POLICY "scaling_min_freq", min_f, sizeof(min_f))) {
		fprintf(stderr, "r800boost: cannot read %s\n", POLICY);
		return 1;
	}
	if (read_str(GPU "dvfs_min_lock", gpu_lock, sizeof(gpu_lock)) == 0 &&
	    strcmp(gpu_lock, "-1") == 0)
		gpu = 1;
	for (i = 1; i < argc && n < MAX_DEV; i++) {
		int fd;
		if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
			boost_ms = atoi(argv[++i]);
			if (boost_ms < 50)
				boost_ms = 50;
			continue;
		}
		fd = open(argv[i], O_RDONLY | O_CLOEXEC | O_NONBLOCK);
		if (fd < 0) {
			perror(argv[i]);
			continue;
		}
		pfd[n].fd = fd;
		pfd[n].events = POLLIN;
		n++;
	}
	if (!n)
		return 1;
	fprintf(stderr, "r800boost: %d devices, boost %s kHz, rest %s kHz, %d ms, gpu %s\n",
		n, max_f, min_f, boost_ms, gpu ? GPU_BOOST " MHz" : "left alone");

	for (;;) {
		int timeout = -1, r;
		if (boosted) {
			long long left = until - now_ms();
			timeout = left > 0 ? (int)left : 0;
		}
		r = poll(pfd, n, timeout);
		if (r > 0) {
			struct input_event ev[16];
			for (i = 0; i < n; i++)
				if (pfd[i].revents & POLLIN)
					while (read(pfd[i].fd, ev, sizeof(ev)) > 0)
						;
			if (!boosted) {
				write_str(POLICY "scaling_min_freq", max_f);
				if (gpu)
					write_str(GPU "dvfs_min_lock", GPU_BOOST);
				boosted = 1;
			}
			until = now_ms() + boost_ms;
		} else if (r == 0 && boosted) {
			write_str(POLICY "scaling_min_freq", min_f);
			if (gpu)
				write_str(GPU "dvfs_min_lock", "0");   /* 0 = unlock */
			boosted = 0;
		}
	}
}
