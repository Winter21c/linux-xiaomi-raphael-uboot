/* Arm the PM8150B/QCOM hardware watchdog so a userspace freeze reboots the
 * phone instead of needing a physical power cycle.  Usage:
 *   wdt <timeout_seconds> <mode>   mode: hold (keep feeding) | arm (stop feeding
 *   after 5 s so a wedged test reboots automatically) */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>

int main(int argc, char **argv)
{
	int to = argc > 1 ? atoi(argv[1]) : 60;
	const char *mode = argc > 2 ? argv[2] : "hold";
	int fd = open("/dev/watchdog", O_WRONLY);
	if (fd < 0) { perror("/dev/watchdog"); return 1; }
	if (ioctl(fd, WDIOC_SETTIMEOUT, &to) < 0) perror("SETTIMEOUT");
	int now = 0;
	ioctl(fd, WDIOC_GETTIMEOUT, &now);
	printf("watchdog timeout=%ds mode=%s\n", now, mode);
	if (!strcmp(mode, "dis")) {
		int flags = WDIOS_DISABLECARD;
		ioctl(fd, WDIOC_SETOPTIONS, &flags);
		printf("watchdog disabled\n");
		close(fd);
		return 0;
	}
	if (!strcmp(mode, "arm")) {
		/* keep the device open and alive but stop feeding after 5 s:
		 * the hardware will reset the phone if the test wedges */
		sleep(5);
		printf("armed: not feeding any more\n");
		fflush(stdout);
		pause();
	}
	for (;;) { ioctl(fd, WDIOC_KEEPALIVE, 0); sleep(now / 3); }
	return 0;
}
