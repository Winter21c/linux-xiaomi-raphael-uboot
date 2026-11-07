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
STAGES="post-mortem preflight regress recover media-order media-variantC front-build front-scan ultra-scan imx586-build imx586-probe"

stage_post-mortem() {
	# Decide WHY the previous boot froze: the modem WCSS crash loop and a camera
	# deadlock produce the same signature (userspace frozen, kernel alive).
	echo "=== previous boot: modem / crash evidence ==="
	journalctl -k -b -1 --no-pager 2>/dev/null |
		grep -iE "wcss|ccpm|srpll|remoteproc|crash detected|rcu|soft lockup|hung task|watchdog" |
		tail -30
	echo "=== previous boot: camera evidence ==="
	journalctl -k -b -1 --no-pager 2>/dev/null |
		grep -iE "camss|cci|ov8856|csiphy|media" | tail -20
	echo "=== verdict hints ==="
	echo "  WCSS/CCPM lines right before the end  -> modem crash loop came back"
	echo "  only camss/ov8856 lines at the end     -> the async link is the culprit"
	echo "  qcom_wdt / soft lockup                 -> the watchdog fired and rebooted"
}

stage_preflight() {
	# never let a modem wedge masquerade as a camera failure
	echo "modem state: $(cat /sys/class/remoteproc/remoteproc1/state 2>/dev/null)"
	echo "guard unit : $(systemctl is-active raphael-stop-modem 2>/dev/null)"
	if [ "$(cat /sys/class/remoteproc/remoteproc1/state 2>/dev/null)" != "offline" ]; then
		echo "REFUSING to run camera tests with the modem running" >&2
		return 1
	fi
	echo "uptime: $(uptime | sed 's/.*up //;s/,.*//')"
	dmesg | tail -3
}

stage_regress() {
	# Objective requirement: a camera DTB change must not break the peripherals
	# that were already verified (sensors, NFC, haptics, audio, modem guard).
	local fail=0
	echo "=== sensors (SDSP/ADSP via libssc) ==="
	if [ -d /sys/bus/iio/devices ] && ls /sys/bus/iio/devices/iio:device* >/dev/null 2>&1; then
		for d in /sys/bus/iio/devices/iio:device*; do
			[ -f "$d/in_accel_x_raw" ] && echo "accel: $(cat "$d/name" 2>/dev/null) raw=$(cat "$d/in_accel_x_raw")"
			[ -f "$d/in_illuminance_raw" ] && echo "light: $(cat "$d/name" 2>/dev/null) raw=$(cat "$d/in_illuminance_raw")"
		done
	else
		echo "no iio devices"; fail=1
	fi
	echo "=== NFC (PN553 @ i2c SE9 0x28) ==="
	if [ -e /sys/class/nfc/nfc0 ]; then echo "nfc0 present"; else echo "NFC MISSING"; fail=1; fi
	echo "=== haptics (PM8150B) ==="
	if grep -qi pm8150b_haptics /proc/bus/input/devices 2>/dev/null; then
		echo "haptics input present"
	else
		echo "HAPTICS MISSING"; fail=1
	fi
	echo "=== audio (WCD9340 + TFA9874) ==="
	if grep -qi sm8150_raphael /proc/asound/cards 2>/dev/null; then
		echo "sound card: $(grep -i sm8150_raphael /proc/asound/cards | head -1 | tr -s ' ')"
	else
		echo "SOUND CARD MISSING"; fail=1
	fi
	grep -qi "headphone\|Headset" /proc/bus/input/devices 2>/dev/null &&
		echo "jack input present" || echo "jack input missing (may load later)"
	echo "=== modem guard ==="
	echo "remoteproc1: $(cat /sys/class/remoteproc/remoteproc1/state 2>/dev/null)"
	echo "guard unit : $(systemctl is-active raphael-stop-modem 2>/dev/null)"
	echo "crash lines: $(dmesg | grep -ci 'crash detected')"
	echo "=== verdict: $([ $fail -eq 0 ] && echo PASS || echo FAIL) ==="
	return $fail
}

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

stage_front-build() {
	# compile the S5K3T2 bring-up driver (power + chip id, no streaming)
	mkdir -p /root/s5k3t2
	cp "$HERE"/../camera/drivers/s5k3t2.c /root/s5k3t2/s5k3t2.c
	printf 'obj-m += s5k3t2.o\n' > /root/s5k3t2/Makefile
	make -C /lib/modules/$(uname -r)/build M=/root/s5k3t2 \
	     CC=/usr/local/bin/gcc-noclang LD=/usr/local/bin/ld-nollvm modules 2>&1 | tail -25
	ls -la /root/s5k3t2/s5k3t2.ko 2>/dev/null
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

stage_ultra-scan() {
	# ultrawide: needs patches 0007 (CCI1) + 0010 (node) in the installed DTB.
	# After boot, the third Qualcomm-CCI adapter should exist; scan it.
	local adapters
	adapters=$(for d in /sys/bus/i2c/devices/i2c-*; do
		[ "$(cat "$d/name" 2>/dev/null)" = "Qualcomm-CCI" ] && basename "$d"
	done | tr '\n' ' ')
	echo "CCI adapters: $adapters"
	echo "(expect three once patch 0007 is in: two for CCI0, one for CCI1 master0)"
	"$HERE"/wdt 60 hold & local w=$!
	insmod /root/s5k3t2/s5k3t2.ko keep_power=1 2>&1 | head -3
	sleep 6
	kill $w 2>/dev/null
	for a in $adapters; do
		num=${a#i2c-}
		echo "--- /dev/i2c-$num ---"
		"$HERE"/probew "$num"
	done
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
