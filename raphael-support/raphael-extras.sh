#!/bin/sh
# Xiaomi Redmi K20 Pro (raphael) peripheral bring-up
#
# The upstream sm8150-xiaomi-raphael device tree and mainline drivers still
# miss a few board peripherals.  This script brings them up at boot:
#
#   * SDSP sensor configuration/calibration tree (accelerometer, gyroscope,
#     ambient light, proximity, magnetometer through libssc + hexagonrpcd)
#   * NXP PN553 NFC controller on QUPv3 SE9 (geni-i2c @ a84000)
#   * PM8150B haptics on the secondary SPMI die (USID 3, address 0xc000)
#   * Q6 DSP audio routing needed before any PCM is opened
#
# Everything is applied at runtime; nothing here touches the boot DTB, so a
# failure can never make the device unbootable.
set -e

log() { echo "raphael-extras: $*"; }

# ---------------------------------------------------------------- sensors
# The SDSP (SensorsPD) reads its configuration and calibration from the
# Android partitions through the hexagonrpcd file server.
/usr/local/sbin/raphael-setup-sensors.sh || log "sensor setup failed"

# ------------------------------------------------------------------ audio
# The Q6 DSP needs the front-end -> back-end routing to exist before a PCM is
# opened, otherwise probing fails with
# "ASoC: no backend DAIs enabled for MultiMedia1, possibly missing ALSA
#  mixer-based routing or UCM profile".
amixer -c 0 cset "name='SLIMBUS_0_RX Audio Mixer MultiMedia1'" 1 >/dev/null 2>&1 || true
amixer -c 0 cset "name='QUAT_MI2S_RX Audio Mixer MultiMedia2'" 1 >/dev/null 2>&1 || true

# Capture: MultiMedia1 capture <- SLIMBUS_0_TX <- SLIM TX6 <- DEC6 <- ADC1 (AMIC1,
# the bottom microphone).  Set here so that a plain "hw:0,0" capture client
# works without a UCM verb.
amixer -c 0 cset "name='MultiMedia1 Mixer SLIMBUS_0_TX'" 1 >/dev/null 2>&1 || true
# PipeWire/ACP also probes capture on hw:0,1 (MultiMedia2) and aborts the whole
# card profile when that fails, so keep a capture route there too.
amixer -c 0 cset "name='MultiMedia2 Mixer SLIMBUS_0_TX'" 1 >/dev/null 2>&1 || true
amixer -c 0 cset "name='AIF1_CAP Mixer SLIM TX6'" 1 >/dev/null 2>&1 || true
amixer -c 0 cset "name='CDC_IF TX6 MUX'" DEC6 >/dev/null 2>&1 || true
amixer -c 0 cset "name='ADC MUX6'" AMIC >/dev/null 2>&1 || true
amixer -c 0 cset "name='AMIC MUX6'" ADC1 >/dev/null 2>&1 || true
amixer -c 0 cset "name='ADC1 Volume'" 8 >/dev/null 2>&1 || true
amixer -c 0 cset "name='DEC6 Volume'" 96 >/dev/null 2>&1 || true

# ----------------------------------------------------------------- haptics
modprobe -q ff-memless || log "ff-memless not available"
modprobe -q pm8150b-haptics || log "pm8150b-haptics not available"

# ---------------------------------------------------- NFC (overlay + QUP9)
# geni-i2c needs the GPI DMA instance of the same QUP wrapper, and the NFC
# child node must exist before the adapter is probed, hence the rebind below.
modprobe -q dtbo-loader overlay=raphael/raphael.dtbo \
	create_nodes=/soc@0/dma-controller@a00000,/soc@0/geniqup@ac0000/i2c@a84000,/soc@0/pm8150b-haptics \
	|| log "device tree overlay failed (NFC/haptics unavailable)"

sleep 1
if [ -e /sys/bus/platform/drivers/geni_i2c/a84000.i2c ]; then
	echo a84000.i2c > /sys/bus/platform/drivers/geni_i2c/unbind 2>/dev/null || true
	sleep 1
	echo a84000.i2c > /sys/bus/platform/drivers/geni_i2c/bind 2>/dev/null || true
fi

log "done"
