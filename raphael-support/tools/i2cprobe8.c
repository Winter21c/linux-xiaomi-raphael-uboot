#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

static int rd(int fd, int addr, unsigned char reg, unsigned char *buf, int n)
{
	struct i2c_msg m[2] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = n, .buf = buf },
	};
	struct i2c_rdwr_ioctl_data d = { .msgs = m, .nmsgs = 2 };
	return ioctl(fd, I2C_RDWR, &d) >= 0 ? 0 : -1;
}

int main(int argc, char **argv)
{
	int bus = argc > 1 ? atoi(argv[1]) : 3;
	char p[32]; snprintf(p, sizeof(p), "/dev/i2c-%d", bus);
	int fd = open(p, O_RDWR);
	if (fd < 0) { perror(p); return 1; }
	printf("== %s ==\n", p);
	for (int a = 0x08; a < 0x78; a++) {
		unsigned char b[4] = {0};
		if (rd(fd, a, 0x00, b, 2) == 0)
			printf("  0x%02x ACK reg00=%02x%02x\n", a, b[0], b[1]);
	}
	/* chip-id candidates on any device found at 0x54/0x10/0x1a/0x36 */
	int cand[] = {0x54, 0x10, 0x1a, 0x36, 0x20};
	for (unsigned i = 0; i < sizeof(cand)/sizeof(cand[0]); i++) {
		unsigned char b[2];
		if (rd(fd, cand[i], 0x0a, b, 2) == 0)
			printf("  0x%02x id(0x0a)=%02x%02x\n", cand[i], b[0], b[1]);
		if (rd(fd, cand[i], 0x30, b, 2) == 0)
			printf("  0x%02x reg30=%02x%02x\n", cand[i], b[0], b[1]);
	}
	close(fd);
	return 0;
}
