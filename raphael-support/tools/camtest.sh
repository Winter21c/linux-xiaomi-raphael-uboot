#!/bin/bash
# Safe camera test: arm the hardware watchdog first so a userspace freeze
# reboots the phone on its own (no physical power cycle needed).
set -x
TMO=${TMO:-60}
/tmp/wdt $TMO hold >/tmp/wdt.log 2>&1 &
WDT_PID=$!
sleep 1
cat /tmp/wdt.log

load_common() {
  for m in mc videodev v4l2-async v4l2-fwnode videobuf2-common videobuf2-memops videobuf2-v4l2 videobuf2-dma-sg v4l2-cci; do modprobe "$m" 2>/dev/null; done
  grep -q i2c_qcom_cci /proc/modules || insmod /root/ccifix/i2c-qcom-cci.ko
  sleep 2
}

case "$1" in
camss-first)
  load_common
  insmod /root/camss-final/qcom-camss.ko; echo "camss rc=$?"
  sleep 6
  # stop feeding 5 s after the next command starts: a wedge will reset the phone
  kill $WDT_PID 2>/dev/null
  insmod /root/ovcomb/ov8856.ko; echo "ov8856 rc=$?"
  ;;
sensor-first)
  load_common
  insmod /root/ovcomb/ov8856.ko; echo "ov8856 rc=$?"
  sleep 4
  kill $WDT_PID 2>/dev/null
  insmod /root/camss-final/qcom-camss.ko; echo "camss rc=$?"
  ;;
esac
sleep 8
echo "=== alive ==="
echo "=== sensor subdev ==="
for s in /sys/class/video4linux/v4l-subdev*; do n=$(cat $s/name 2>/dev/null); case "$n" in *ov8856*) echo "FOUND $(basename $s) = $n";; esac; done
echo "subdevs: $(ls /dev/v4l-subdev* 2>/dev/null | wc -l)"
echo "=== topo ==="
/tmp/v4ltool topo /dev/media0 2>&1 | head -30
# success: stop the watchdog before it reboots us
/tmp/wdt 0 dis
