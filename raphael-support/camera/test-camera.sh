#!/bin/bash
# Load the SM8150 camera stack *without* letting udev autoload it at boot.
#
#   ./test-camera.sh <qcom-camss.ko> [--keep]
#
# The module is loaded with insmod from /root so that it is never present under
# /lib/modules: if a probe wedges on a clock or PHY, the next boot is clean.
# All output goes to /root/camera-test.log as well as the terminal, so a wedged
# session still leaves evidence behind.
set -u

KO=${1:?usage: $0 <qcom-camss.ko>}
LOG=/root/camera-test.log

exec > >(tee -a "$LOG") 2>&1
echo "=== $(date -Is) camera test ==="

# Hardware watchdog safety net: if a probe wedges the kernel, the timer expires
# and the phone resets itself instead of needing a manual recovery boot.
WDT=""
for w in /dev/watchdog /dev/watchdog0; do
	[ -c "$w" ] && WDT="$w" && break
done
if [ -n "$WDT" ]; then
	echo "--- arming $WDT (30 s, userspace keep-alive)"
	( exec 3>"$WDT"; for i in $(seq 1 120); do
		printf '1' >&3 2>/dev/null || exit 0
		sleep 5
	done; printf 'V' >&3 2>/dev/null ) &
	WDT_PID=$!
	trap 'kill $WDT_PID 2>/dev/null' EXIT
	# the watchdog only bites if userspace stops running, which is exactly the
	# failure mode we are testing for
else
	echo "--- no hardware watchdog available"
fi

echo "--- DTB camera nodes"
ls /proc/device-tree/soc@0/ | grep -E "camss|cci" || echo "none (DTB without camera nodes)"

echo "--- CCI driver"
modprobe -r i2c-qcom-cci 2>/dev/null
modprobe i2c-qcom-cci
sleep 1
dmesg | tail -5 | grep -i cci || true
ls /sys/bus/i2c/devices/ 2>/dev/null | head

echo "--- camss"
insmod "$KO"
sleep 4
dmesg | grep -iE "camss|csiphy|csid|vfe" | tail -20
echo "--- media devices"
ls -l /dev/media* /dev/video* 2>/dev/null
echo "--- media topology"
for m in /dev/media*; do
	[ -e "$m" ] || continue
	media-ctl -d "$m" -p 2>/dev/null | head -30
done
echo "--- sensors"
dmesg | grep -iE "ov8856|ovti" | tail -10
echo "=== done ==="
