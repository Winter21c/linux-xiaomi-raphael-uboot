// Trigger a rumble effect on an evdev force-feedback device.
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/input/event5";
	int len = argc > 2 ? atoi(argv[2]) : 1500;
	struct ff_effect e;
	struct input_event ie;
	int fd, ret;

	fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }

	memset(&e, 0, sizeof(e));
	e.type = FF_RUMBLE;
	e.id = -1;
	e.u.rumble.strong_magnitude = 0xffff;
	e.u.rumble.weak_magnitude = 0xffff;
	e.replay.length = len;
	e.replay.delay = 0;

	ret = ioctl(fd, EVIOCSFF, &e);
	if (ret < 0) { perror("EVIOCSFF"); return 1; }
	printf("uploaded effect id %d\n", e.id);

	memset(&ie, 0, sizeof(ie));
	ie.type = EV_FF;
	ie.code = e.id;
	ie.value = 1;
	if (write(fd, &ie, sizeof(ie)) != sizeof(ie)) { perror("play"); return 1; }
	printf("rumble on for %d ms\n", len);
	usleep(len * 1000);
	ie.value = 0;
	write(fd, &ie, sizeof(ie));
	close(fd);
	return 0;
}
