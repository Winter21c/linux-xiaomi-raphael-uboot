#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <string.h>

int main(int argc, char **argv)
{
	int bus = argc > 1 ? atoi(argv[1]) : 0;
	char path[32];
	snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
	int fd = open(path, O_RDWR);
	if (fd < 0) { printf("open %s: %s\n", path, strerror(errno)); return 1; }
	printf("scanning %s\n", path);
	for (int a = 0x08; a < 0x78; a++) {
		unsigned char reg = 0;
		struct i2c_msg msgs[2] = {
			{ .addr = a, .flags = 0, .len = 1, .buf = &reg },
			{ .addr = a, .flags = I2C_M_RD, .len = 2, .buf = (unsigned char[2]){0} },
		};
		struct i2c_rdwr_ioctl_data d = { .msgs = msgs, .nmsgs = 2 };
		if (ioctl(fd, I2C_RDWR, &d) >= 0)
			printf("  0x%02x ACK  reg00=0x%02x%02x\n", a, msgs[1].buf[0], msgs[1].buf[1]);
	}
	close(fd);
	return 0;
}
