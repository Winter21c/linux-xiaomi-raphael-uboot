#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

/* usage: gpioset <chip> <line> <0|1> */
int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: gpioset chip line value\n"); return 2; }
	char path[32];
	snprintf(path, sizeof(path), "/dev/gpiochip%s", argv[1]);
	int fd = open(path, O_RDWR);
	if (fd < 0) { perror(path); return 1; }

	struct gpio_v2_line_request req;
	memset(&req, 0, sizeof(req));
	req.offsets[0] = atoi(argv[2]);
	req.num_lines = 1;
	strncpy(req.consumer, "camtest", sizeof(req.consumer) - 1);
	req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
	if (ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) { perror("get_line"); return 1; }

	struct gpio_v2_line_values v = { .bits = atoi(argv[3]) ? 1 : 0, .mask = 1 };
	if (ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v) < 0) { perror("set_values"); return 1; }
	printf("gpiochip%s line %s = %s\n", argv[1], argv[2], atoi(argv[3]) ? "high" : "low");
	/* hold the line for <hold> seconds so the pin keeps its value */
	sleep(argc > 4 ? atoi(argv[4]) : 1);
	return 0;
}
