/* loopdio /dev/block/loopN [0|1]: toggle LOOP_SET_DIRECT_IO (avoids double page caching) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <unistd.h>
#ifndef LOOP_SET_DIRECT_IO
#define LOOP_SET_DIRECT_IO 0x4C08
#endif
/* static bionic refuses a TLS segment aligned below 32 on ARM: force 64 */
__thread volatile char r800_tls_pad __attribute__((aligned(64)));

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: %s /dev/block/loopN [0|1]\n", argv[0]); return 2; }
	r800_tls_pad = (char)argc; if (r800_tls_pad == 99) return 3;
	int fd = open(argv[1], O_RDONLY);
	if (fd < 0) { fprintf(stderr, "open %s: %s\n", argv[1], strerror(errno)); return 1; }
	unsigned long on = argc > 2 ? strtoul(argv[2], NULL, 0) : 1;
	if (ioctl(fd, LOOP_SET_DIRECT_IO, on)) { fprintf(stderr, "LOOP_SET_DIRECT_IO(%lu): %s\n", on, strerror(errno)); return 1; }
	printf("%s direct_io=%lu ok\n", argv[1], on);
	return 0;
}
