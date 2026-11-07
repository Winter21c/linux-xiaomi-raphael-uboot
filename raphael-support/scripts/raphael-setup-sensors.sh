#!/bin/bash
# Xiaomi Redmi K20 Pro (raphael) - SDSP sensor file tree
#
# hexagonrpcd serves this tree to the SLPI/SDSP SensorsPD.  The expected
# layout is the one droid-juicer produces (relative to the hexagonrpcd -R root):
#
#   acdb/                     audio calibration (vendor/etc/acdbdata)
#   dsp/{adsp,cdsp,sdsp}/     DSP libraries (vendor/dsp)
#   sensors/config/           sensor configuration JSON (vendor/etc/sensors/config)
#   sensors/sns_reg.conf      registry configuration (vendor/etc/sensors/sns_reg_config)
#   sensors/registry/         calibration data (persist/sensors/registry/registry)
#   socinfo/                  Qualcomm socinfo attributes (sysfs)
#
# Everything is copied from the Android partitions of the very same device, so
# the calibration matches this unit.
set -e
ROOT=/var/lib/droid-juicer/sensors

log() { echo "raphael-sensors: $*"; }

if [ ! -e /dev/disk/by-partlabel/vendor ]; then
	log "Android vendor partition not found, keeping existing tree"
	exit 0
fi

mkdir -p /mnt/vendor_ro
mountpoint -q /mnt/vendor_ro || mount -o ro /dev/disk/by-partlabel/vendor /mnt/vendor_ro || exit 0
if [ -e /dev/disk/by-partlabel/persist ]; then
	mkdir -p /mnt/persist
	mountpoint -q /mnt/persist || mount -o ro /dev/disk/by-partlabel/persist /mnt/persist || true
fi

mkdir -p "$ROOT"/acdb "$ROOT"/dsp "$ROOT"/sensors/config "$ROOT"/sensors/registry "$ROOT"/socinfo

# Calibration / registry (persist), only if not populated yet
if [ -z "$(ls -A "$ROOT"/sensors/registry 2>/dev/null)" ] && [ -d /mnt/persist/sensors/registry/registry ]; then
	log "copying sensor registry from persist"
	cp -a /mnt/persist/sensors/registry/registry/. "$ROOT"/sensors/registry/ 2>/dev/null || true
fi

# Sensor configuration (vendor)
if [ -z "$(ls -A "$ROOT"/sensors/config 2>/dev/null)" ] && [ -d /mnt/vendor_ro/etc/sensors/config ]; then
	log "copying sensor configuration from vendor"
	cp -a /mnt/vendor_ro/etc/sensors/config/. "$ROOT"/sensors/config/ 2>/dev/null || true
	cp -a /mnt/vendor_ro/etc/sensors/sns_reg_config "$ROOT"/sensors/sns_reg.conf 2>/dev/null || true
fi

if [ -z "$(ls -A "$ROOT"/acdb 2>/dev/null)" ] && [ -d /mnt/vendor_ro/etc/acdbdata ]; then
	cp -a /mnt/vendor_ro/etc/acdbdata/. "$ROOT"/acdb/ 2>/dev/null || true
fi
if [ -z "$(ls -A "$ROOT"/dsp 2>/dev/null)" ] && [ -d /mnt/vendor_ro/dsp ]; then
	cp -a /mnt/vendor_ro/dsp/. "$ROOT"/dsp/ 2>/dev/null || true
fi

# socinfo: the mainline qcom_socinfo driver does not expose hw_platform and
# platform_version, but the SensorsPD firmware probes them at startup.
[ -e "$ROOT"/socinfo/hw_platform ] || printf 'MTP' > "$ROOT"/socinfo/hw_platform
[ -e "$ROOT"/socinfo/platform_version ] || : > "$ROOT"/socinfo/platform_version
for f in soc_id family machine revision serial_number; do
	if [ -e "/sys/devices/soc0/$f" ]; then
		cat "/sys/devices/soc0/$f" > "$ROOT/socinfo/$f" 2>/dev/null || true
	fi
done

chown -R fastrpc:fastrpc "$ROOT" 2>/dev/null || true
log "sensor tree ready ($(du -sh "$ROOT" | cut -f1))"
