#!/bin/sh
# Install the Raphael peripheral bring-up on a running system.
#
# Run as root inside the device:
#     ./install.sh
#
# It installs the kernel modules, the device-tree overlay, the systemd units
# and the ALSA UCM configuration, then enables the bring-up service.
set -e

SRC=$(cd "$(dirname "$0")" && pwd)
KREL=$(uname -r)
KMOD="/lib/modules/$KREL/extra/raphael"

echo "==> installing kernel modules into $KMOD"
mkdir -p "$KMOD"
for m in dtbo-loader pm8150b-haptics; do
	[ -f "$SRC/$m/$m.ko" ] || {
		echo "missing $SRC/$m/$m.ko - build it first (see README)" >&2
		exit 1
	}
	cp "$SRC/$m/$m.ko" "$KMOD/"
done
depmod -a

echo "==> installing device tree overlay"
mkdir -p /lib/firmware/raphael
[ -f "$SRC/overlays/raphael.dtbo" ] && cp "$SRC/overlays/raphael.dtbo" /lib/firmware/raphael/

echo "==> installing helper scripts"
install -m 0755 "$SRC/raphael-setup-sensors.sh" /usr/local/sbin/
install -m 0755 "$SRC/raphael-sensors-wait.sh" /usr/local/sbin/
install -m 0755 "$SRC/raphael-extras.sh" /usr/local/sbin/
install -m 0755 "$SRC/audio/raphael-audio-profile.sh" /usr/local/sbin/

echo "==> installing systemd units"
install -m 0644 "$SRC/raphael-extras.service" /etc/systemd/system/
install -m 0644 "$SRC/audio/raphael-audio-profile.service" /etc/systemd/system/
mkdir -p /etc/systemd/system/iio-sensor-proxy.service.d
install -m 0644 "$SRC/iio-sensor-proxy-raphael.conf" \
	/etc/systemd/system/iio-sensor-proxy.service.d/raphael.conf

echo "==> installing ALSA UCM configuration"
mkdir -p /usr/share/alsa/ucm2/conf.d/sm8150_raphael /usr/share/alsa/ucm2/conf.d/Raphael \
	 /usr/share/alsa/ucm2/Raphael
install -m 0644 "$SRC/ucm/HiFi.conf" /usr/share/alsa/ucm2/conf.d/sm8150_raphael/HiFi.conf
for d in /usr/share/alsa/ucm2/conf.d/Raphael /usr/share/alsa/ucm2/Raphael; do
	install -m 0644 "$SRC/ucm/HiFi.conf" "$d/HiFi.conf"
	printf 'Syntax 3\n\nSectionUseCase."HiFi" {\n\tFile "HiFi.conf"\n\tComment "HiFi quality Music"\n}\n' \
		> "$d/$(basename "$d").conf"
done
rm -rf /var/cache/alsa /home/user/.cache/alsa

echo "==> keeping fwupd away from the buggy seccomp path"
systemctl mask fwupd.service fwupd-refresh.service fwupd-refresh.timer 2>/dev/null || true

systemctl daemon-reload
systemctl enable raphael-extras.service raphael-audio-profile.service 2>/dev/null || true
systemctl restart raphael-extras.service

echo "==> done"
echo "Sensors:   ssccli --sensor accelerometer --timeout 3"
echo "NFC:       /sys/class/nfc/ and nxp-nci binding"
echo "Haptics:   ./tools/ff-test /dev/input/eventX 1500"
