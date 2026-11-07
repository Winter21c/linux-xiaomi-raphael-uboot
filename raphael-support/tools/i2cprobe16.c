#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* probe with a 16-bit register address (most camera sensors use those) */
static int rd16(int fd, int addr, unsigned short reg, unsigned char *buf, int n)
{
	unsigned char r[2] = { reg >> 8, reg & 0xff };
	struct i2c_msg m[2] = {
		{ .addr = addr, .flags = 0, .len = 2, .buf = r },
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
	printf("== %s (16-bit reg probe) ==\n", p);
	for (int a = 0x08; a < 0x78; a++) {
		unsigned char b[2] = {0};
		if (rd16(fd, a, 0x3000, b, 2) == 0) {
			unsigned char c[2] = {0};
			rd16(fd, a, 0x300a, c, 2);
			printf("  0x%02x ACK  r3000=%02x%02x r300a=%02x%02x\n", a, b[0], b[1], c[0], c[1]);
		}
	}
	close(fd);
	return 0;
}
