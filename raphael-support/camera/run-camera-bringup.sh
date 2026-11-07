#!/bin/bash
# On-device: build the instrumented camss module, load it safely and report.
#
#   ./run-camera-bringup.sh <camss source dir> [<camera dtb>]
#
# * builds with patches 0001 (SM8150) + 0003 (per-clock logging)
# * never touches /lib/modules, so the next boot is clean
# * arms the hardware watchdog so a wedged probe reboots by itself
# * logs everything to /root/camera-bringup.log
set -u
SRC=${1:?usage: $0 <camss source dir> [camera dtb]}
DTB=${2:-}
HERE=$(cd "$(dirname "$0")" && pwd)
LOG=/root/camera-bringup.log

exec > >(tee -a "$LOG") 2>&1
echo "=== $(date -Is) camera bring-up ==="

if [ ! -e "$SRC/.patched-sm8150" ]; then
	echo "--- patching"
	(cd "$SRC" && patch -p6 --forward < "$HERE/patches/0001-camss-add-sm8150-support.patch")
	(cd "$SRC" && patch -p6 --forward < "$HERE/patches/0003-camss-clock-debug.patch")
	touch "$SRC/.patched-sm8150"
fi

echo "--- building"
make -C "/lib/modules/$(uname -r)/build" M="$SRC" \
	CC=/home/user/raphael-drivers/tools/gcc-noclang modules 2>&1 | tail -5
KO="$SRC/qcom-camss.ko"
[ -f "$KO" ] || { echo "build failed"; exit 1; }
echo "built: $(ls -l "$KO")"

if [ -n "$DTB" ]; then
	echo "--- installing camera DTB (backup kept as .bak)"
	D=/boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb
	[ -f "$D.bak" ] || cp "$D" "$D.bak"
	cp "$DTB" "$D"
	sync
	echo "reboot required for the DTB change; re-run without the DTB argument after the reboot"
	exit 0
fi

echo "--- DTB nodes: $(ls /proc/device-tree/soc@0/ | grep -cE 'camss|cci')"

# hardware watchdog: if the kernel wedges, userspace stops feeding it and the
# phone resets itself instead of needing a rescue boot
WDT=""
for w in /dev/watchdog /dev/watchdog0; do [ -c "$w" ] && WDT="$w" && break; done
if [ -n "$WDT" ]; then
	echo "--- arming $WDT"
	( exec 3>"$WDT"; for i in $(seq 1 120); do printf 1 >&3 2>/dev/null || exit 0; sleep 5; done; printf V >&3 2>/dev/null ) &
	WDT_PID=$!
	trap 'kill $WDT_PID 2>/dev/null' EXIT
fi

echo "--- camcc clocks before"
grep -iE "cam_cc" /sys/kernel/debug/clk/clk_summary 2>/dev/null | head -30

echo "--- cci"
modprobe -r i2c-qcom-cci 2>/dev/null
modprobe i2c-qcom-cci
sleep 1
dmesg | grep -i cci | tail -3

echo "--- camss (insmod from $SRC, not /lib/modules)"
insmod "$KO"
sleep 5
dmesg | grep -iE "camss|csiphy|csid|vfe|ov8856" | tail -30

echo "--- media"
ls -l /dev/media* /dev/video* 2>/dev/null
for m in /dev/media*; do [ -e "$m" ] && media-ctl -d "$m" -p 2>/dev/null | head -25; done

echo "=== done $(date -Is) ==="
