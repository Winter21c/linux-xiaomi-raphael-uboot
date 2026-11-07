# Raphael (Redmi K20 Pro / SM8150) peripheral bring-up

Everything in this directory adds the board peripherals that the current
`sm8150-xiaomi-raphael` mainline device tree and drivers are still missing,
**without touching the boot device tree**.  A broken overlay therefore can never
make the phone unbootable.

Tested on: Debian 13 trixie, kernel `7.3.0-rc5-sm8150-gc033da1fc4fe`
(branch `raphael-v7.3.0`, built with clang + ThinLTO).

## What is implemented

| Peripheral | Implementation | Verification |
|---|---|---|
| SDSP sensors (accelerometer, gyroscope, ambient light, proximity, magnetometer) | `scripts/raphael-setup-sensors.sh` populates the hexagonrpcd file server root from the Android `vendor`/`persist` partitions (droid-juicer layout: `acdb/`, `dsp/`, `sensors/`, `socinfo/`); `libssc` + `iio-sensor-proxy` consume it over QRTR | `ssccli --sensor accelerometer --timeout 3` returns live data; `HasAccelerometer/HasAmbientLight/HasProximity = true` |
| NXP PN553 NFC | `drivers/dts/raphael.dts` enables the SE9 GPI DMA + `i2c@a84000`, adds `nfc@28`; `dtbo-loader` applies it at runtime and instantiates the devices | `/sys/class/nfc/nfc0`, `2-0028` bound to `nxp-nci_i2c`, netlink `NFC_CMD_DEV_UP` + `NFC_CMD_START_POLL` succeed (`tools/nfc-up.c`) |
| PM8150B haptics (ERM) | `drivers/input/pm8150b-haptics/` driver for the haptics block on the **secondary SPMI die (USID 3, address 0xc000)** | `tools/ff-test.c` -> registers read back `EN_CTL1=0x80`, `STATUS1` busy bit set; vibration felt on the phone |
| Audio playback | UCM profile `alsa/ucm/HiFi.conf` (Speaker on QUAT_MI2S/TFA9874, Headphone on SLIMBUS_0_RX/WCD9340); `scripts/raphael-extras.sh` sets the Q6 front-end -> back-end routing at boot | 660 Hz tone audible on the speaker (`hw:0,1`) and on the headphones (`hw:0,0`) |
| Headset jack | WCD9340 MBHC | jack input device reports `SW_HEADPHONE_INSERT=1` when a headset is plugged in |
| Microphone | UCM capture devices `Mic`/`HeadsetMic`, plus the boot-time routing (`MultiMedia1 Mixer SLIMBUS_0_TX`, `AIF1_CAP Mixer SLIM TX6`, `DEC6`, `ADC MUX6=AMIC`, `AMIC MUX6=ADC1`) | `arecord -D hw:0,0` shows signal correlated with speech (peak > 4000) |

## Known gaps

* **Camera** – the boot DTB has no `camss`/`csiphy`/`cci` nodes, `qcom-camss.ko`
  has no SM8150 compatible, and the tree has no drivers for the K20 Pro sensors
  (IMX586, S5K3L6, OV8856, S5K3T2 — only OV8856 exists upstream).  The verified
  hardware map, register bases, a DT fragment and the CAMSS patch are in
  `camera/`; this needs a kernel rebuild, it cannot be done as a runtime
  overlay.
* **GNSS** – `gnss.ko` exists but there is no DT node and no `/dev/gnss0`.
* **Fingerprint** – Goodix under-display sensor, no mainline driver.
* **Desktop audio** – the ALSA layer is fully functional, but WirePlumber's ACP
  backend is fragile on this card: it probes capture on `hw:0,1` as well, and
  until that route exists the whole card profile is aborted.  The boot script
  now creates that route; `scripts/raphael-audio-profile.sh` (systemd unit
  `raphael-audio-profile.service`) additionally waits for the card and selects
  the usable profile and the speaker as default sink.

## How the pieces fit

```
                    ┌──────────────────────────────┐
 userspace          │ libssc ──► iio-sensor-proxy  │  (QRTR/QMI to the SDSP)
                    │ hexagonrpcd -R /var/lib/...  │  (file server for the DSP)
                    └───────────────┬──────────────┘
                                    │
                    ┌───────────────▼──────────────┐
 kernel             │ remotes, geni-i2c, PMIC, ASoC│
                    │ dtbo-loader, pm8150b-haptics │
                    └──────────────────────────────┘
```

## Layout (distribution agnostic)

```
drivers/                     kernel modules, buildable against any distro's headers
├── input/pm8150b-haptics/   PM8150B haptics driver (.c, Makefile, prebuilt .ko)
├── of/dtbo-loader/          runtime device-tree overlay loader (.c, Makefile, .ko)
└── dts/                     raphael-peripherals.dts + compiled .dtbo, camera dtsi
camera/                      SM8150 camera port notes + CAMSS patch
scripts/                     POSIX sh bring-up scripts (sensors tree, routing, …)
systemd/                     units and drop-ins
alsa/ucm/                    ALSA UCM profile (Speaker/Headphone/Mic/HeadsetMic)
tools/                       test programs (ff-test, nfc-up) and the build wrapper
install.sh                   copies everything into place
```

Nothing here is Debian specific: the modules build against `/lib/modules/$(uname -r)/build`
(any distribution, any kernel with `CONFIG_OF_OVERLAY`), the scripts are plain
POSIX `sh`, and the UCM/systemd files are standard ALSA/systemd configuration.

## Build

The kernel is built with clang + ThinLTO, so external modules need a small
wrapper (`tools/gcc-noclang`) that strips clang-only flags:

```
cd drivers && make CC=/usr/local/bin/gcc-noclang
dtc -@ -I dts -O dtb -o drivers/dts/raphael.dtbo drivers/dts/raphael.dts
```

Prebuilt artefacts for `7.3.0-rc5-sm8150-gc033da1fc4fe` are included.

## Install

```
sudo ./install.sh
sudo reboot
```

## Test

```
ssccli --sensor accelerometer --timeout 3
ls /sys/class/nfc/
ff-test /dev/input/event5 1500          # search the pm8150b_haptics event node
arecord -D hw:0,0 -f S16_LE -r 48000 -c 1 -d 3 /tmp/mic.wav
aplay -D hw:0,1 /usr/share/sounds/alsa/Front_Center.wav
```

## Notes

* The sensor configuration is copied from the phone's own Android `vendor` and
  `persist` partitions, so the calibration matches the unit.
* The hexagonrpcd root must contain `socinfo/hw_platform` (the mainline
  `qcom_socinfo` driver does not expose it); without it the SensorsPD registry
  never comes up.
* `fwupd` is masked on purpose: `seccomp_attach_filter()` NULL-derefs on this
  kernel when `fwupdmgr` starts, which wedges the machine.
