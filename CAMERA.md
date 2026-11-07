# Camera on the Redmi K20 Pro (raphael / SM8150), mainline kernel

Short, honest status of the camera work in `raphael-peripherals`.
Everything here was measured on the phone unless marked **untested**.

## Where it stands

| piece | state |
|---|---|
| `qcom-camss` on SM8150 | **working** - `/dev/media0`, 14 VFE video nodes, 22 subdevs, CAMCC clocks taken |
| `i2c-qcom-cci` both masters | **working** - `/dev/i2c-3`, `/dev/i2c-4`, first ACK (EEPROM 0x54), rebuilt with upstream HALT_ACK fix `195f3d74` |
| CCI + MCLK pinctrl | **working** - gpio17-20 muxed to `cci_i2c`, gpio13/14 to `cam_mclk`, claimed by the CCI node |
| sensor power | **working** - viodd 1.8 V, BOB, S8 through the VDIG_EN pads, plus PM8150 SMPS5 (2.0 V) and LDO17 (3.1 V) as optional supplies enabled by the driver |
| tele OV8856 | **bound and chip-id verified** at CCI0 master1 **0x36** (`identify_module` only accepts 0x8856) |
| wide IMX586 | responds at CCI0 master0 **0x1a**; public RPi driver **ported** to the 7.3 API (`camera/drivers/imx586.c`), not built yet |
| front S5K3T2 | bring-up driver written (`camera/drivers/s5k3t2.c`): power + chip id + subdev, no streaming |
| ultrawide S5K3L6 | same driver, node prepared; sits on **CCI1**, which needs `patches/0007` to exist at all |
| sensor in the media graph | **blocked** - the CAMSS-side `remote-endpoint` DTB boots but loading camss+ov8856 freezes userspace |
| frames captured | **no** |

## The four blockers that were solved

1. **CSIPHY register windows overlapped** (0x2000 instead of 0x1000) -> `CCI reset timeout (-110)`.
2. **No pinctrl**: CCI pads and CAMCC MCLK pads must be muxed and claimed by the CCI node; a sensor node claiming the same pad fails with EINVAL.
3. **The running kernel predates the upstream CCI fix** `195f3d74` (HALT_ACK reset request dropped); `i2c-qcom-cci.ko` is therefore built from branch-head source.
4. **The modules were unpowered.** They need PM8150 SMPS5 (2.0 V) and LDO17 (3.1 V) on top of viodd/BOB/S8. Forcing `regulator-always-on` on those PMIC rails bricks the boot (SMPS5 is the parent of `vdd-l7-l12-l14-l15-supply`) - it happened twice before the optional-supply route was adopted.

## Layout

```
camera/patches/0001..0005   camss SM8150 port, camera DT, clock debug, ov8856 keep_power/rails
camera/patches/0006..0010   front/ultrawide power + nodes, second CCI controller, IMX586 node
camera/drivers/             imx586.c (ported), s5k3t2.c (bring-up), notes
camera/reference/           the stock raphael camera DT + pinctrl (ground truth)
camera/*.dtb                booted DTB variants, with an md5 table in evidence/derived/
tools/                      i2c scans, gpioset, wdt (hardware watchdog), v4ltool, next-session.sh
evidence/{raw,derived}/     measured register dumps, DTB diffs
analysis/                   why the async link freezes userspace
reports/CAMERA_BRINGUP.md   full write-up
state/                      PROJECT_STATE, DECISIONS (R1-R9), TASK_QUEUE, COMPONENTS.csv
```

## What the next session does

`tools/next-session.sh` runs ten stages, each with the hardware watchdog armed so a
freeze reboots the phone instead of needing a power cycle:

```
post-mortem  which boot died and why (modem WCSS vs camera)
preflight    refuse to test the camera while the modem is running
regress      prove sensors/NFC/haptics/audio still work with the camera DTB
recover      reinstall the verified DTB
media-order  hypothesis 1: load the sensor before camss
media-variantC hypothesis 2: CAMSS-side remote-endpoint only
front-build  compile the S5K3T2/S5K3L6 driver
front-scan   power the front module, scan i2c-3 for its address
ultra-scan   enumerate the third CCI adapter and scan it
imx586-build compile the ported IMX586 driver
imx586-probe load it and check the chip id (0x30dc == 0x32)
```

## Rules learned the hard way

* never put `regulator-always-on` on a PMIC rail, and grep the *compiled* DTB for it
  before flashing (only `vreg_s4a_1p8`, `ts_avdd_3p3`, `ts_vddio_1p8` are acceptable),
* build camera DTBs from `sm8150-xiaomi-raphael.dts.orig` + a known patch list,
  never from a working copy that may carry an old experiment,
* arm the hardware watchdog before any camss/sensor experiment,
* a frozen phone (ping ok, TCP/22 accepts, SSH hangs) is not proof that the camera
  caused it - check the previous boot's journal for WCSS lines first.
