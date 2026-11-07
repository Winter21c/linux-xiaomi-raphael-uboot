#!/bin/bash
# Staged camera bring-up for the next session.  Each stage is independent: run
# one, check the log, then the next.  Every stage that touches camss/sensors
# arms the hardware watchdog first, so a userspace freeze reboots the phone
# instead of needing a physical power cycle.
#
#   ./next-session.sh stages            # list stages
#   ./next-session.sh run <stage>       # execute one stage, log to /tmp/cam-<stage>.log
#   ./next-session.sh all               # run every stage in order
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
STAGES="recover media-order media-variantC front-scan imx586-build imx586-probe"

stage_recover() {
	# restore the verified DTB + modules after any freeze
	D=/boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb
	[ -f "$D.bak" ] || cp "$D" "$D.bak"
	cp /root/raphael-support/camera/sm8150-xiaomi-raphael-camera.dtb "$D" && sync
	md5sum "$D"
	echo "now: systemctl reboot"
}

stage_media-order() {
	# camera-media-graph-wedge hypothesis 1: load the sensor before camss
	"$HERE"/wdt 60 hold & local w=$!
	for m in mc videodev v4l2-async v4l2-fwnode videobuf2-common videobuf2-memops \
	         videobuf2-v4l2 videobuf2-dma-sg v4l2-cci; do modprobe "$m" 2>/dev/null; done
	insmod /root/ccifix/i2c-qcom-cci.ko; sleep 2
	insmod /root/ovcomb/ov8856.ko; echo "ov8856 rc=$?"; sleep 4
	kill $w 2>/dev/null           # stop feeding: a wedge now self-reboots
	insmod /root/camss-final/qcom-camss.ko; echo "camss rc=$?"; sleep 8
	"$HERE"/wdt 0 dis
	"$HERE"/v4ltool topo /dev/media0 | head -40
	for s in /sys/class/video4linux/v4l-subdev*; do
		case "$(cat "$s/name" 2>/dev/null)" in *ov8856*) echo "SENSOR SUBDEV: $(basename "$s")";; esac
	done
}

stage_media-variantC() {
	# hypothesis 2: CAMSS-side remote-endpoint only (variantC DTB must be installed)
	grep -q "0x125\|remote-endpoint" /dev/null 2>/dev/null
	cp /root/raphael-support/camera/sm8150-xiaomi-raphael-camera-variantC.dtb \
	   /boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb && sync
	echo "variantC installed; reboot, then run stage media-order"
}

stage_front-scan() {
	# front module: CSIPHY1 / CCI0 m0 / MCLK2 gpio15 / reset gpio12 / VDIG_EN gpio95
	# power it through the borrowed node (patch 0006) and scan /dev/i2c-3
	"$HERE"/wdt 60 hold & local w=$!
	insmod /root/ovcomb/ov8856.ko keep_power=1; echo "rc=$?"; sleep 6
	kill $w 2>/dev/null
	for a in 0x0a 0x10 0x20 0x21 0x2d 0x36 0x54; do :; done
	"$HERE"/i2cscan 3 | head -30
	"$HERE"/probew 3
	"$HERE"/wdt 0 dis
}

stage_imx586-build() {
	mkdir -p /root/imx586 && cp "$HERE"/../camera/drivers/imx586.c /root/imx586/imx586.c
	printf 'obj-m += imx586.o\n' > /root/imx586/Makefile
	make -C /lib/modules/$(uname -r)/build M=/root/imx586 \
	     CC=/usr/local/bin/gcc-noclang LD=/usr/local/bin/ld-nollvm modules 2>&1 | tail -25
	ls -la /root/imx586/imx586.ko 2>/dev/null
}

stage_imx586-probe() {
	# node must exist at CCI0 master0 0x1a with VANA/VDIG/VDDL + vs5/vl17 supplies
	"$HERE"/wdt 60 hold & local w=$!
	modprobe v4l2-async 2>/dev/null; insmod /root/ccifix/i2c-qcom-cci.ko 2>/dev/null
	insmod /root/camss-final/qcom-camss.ko; sleep 4
	kill $w 2>/dev/null
	insmod /root/imx586/imx586.ko; echo "rc=$?"; sleep 6
	dmesg | grep -iE "imx586|chip id|0x30dc" | tail -8
	"$HERE"/wdt 0 dis
}

run_stage() {
	local s=$1
	case "$s" in
	recover)          stage_recover ;;
	media-order)      stage_media-order ;;
	media-variantC)   stage_media-variantC ;;
	front-scan)       stage_front-scan ;;
	imx586-build)     stage_imx586-build ;;
	imx586-probe)     stage_imx586-probe ;;
	*) echo "unknown stage: $s"; return 2 ;;
	esac
}

case "${1:-stages}" in
	stages) echo "$STAGES" | tr ' ' '\n' ;;
	run)    run_stage "${2:?stage name required}" 2>&1 | tee "/tmp/cam-${2}.log" ;;
	all)    for s in $STAGES; do echo "=== $s ==="; run_stage "$s" 2>&1 | tee "/tmp/cam-$s.log"; done ;;
	*)      echo "usage: $0 {stages|run <stage>|all}"; exit 2 ;;
esac
