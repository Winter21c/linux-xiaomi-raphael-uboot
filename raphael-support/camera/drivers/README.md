# Sensor drivers for the raphael camera bring-up

| driver | source | license | state |
|---|---|---|---|
| `ov8856.c` | in-tree mainline `drivers/media/i2c/ov8856.c` + our patches 0004/0005 | GPL-2.0 | **binds** on the tele module at CCI0 master1 0x36 |
| `imx586-rpi-6.1.c` | https://github.com/youjunl/imx586-v4l2-driver (MIT repo, driver GPL-2.0), based on OCTOPUSCINEMA/soho/Will WHANG work, itself derived from the RPi `imx477` driver | GPL-2.0 | **not ported yet** |
| S5K3L6 (ultrawide) | - | - | no public source found |
| S5K3T2 (front) | - | - | no public source found |

## Why this file is here

`imx586-rpi-6.1.c` is a complete V4L2 driver for the IMX586 (2037 lines, mode
tables + `mode_common_regs` init sequence, chip id 0x30dc == 0x32, 12-bit linear
and Clear-HDR modes).  Mainline has no IMX586 driver, so this is the only known
open source with the register tables the wide camera needs.

It targets kernel 6.1 (Raspberry Pi tree).  It already uses
`struct v4l2_subdev_state` and pm_runtime, so it should be close to our 7.3 tree;
the port work is:

1. build it as `obj-m` against the device's kernel headers and fix whatever the
   7.3 API moved (`.s_stream` is deprecated but still supported; check
   `v4l2_subdev_get_try_format` and `media_entity_pads_init` signatures),
2. teach it our supplies: `dovdd`/`avdd`/`dvdd` plus the optional `vs5`/`vl17`
   rails the wide module needs (same pattern as patch 0005 for ov8856),
3. device tree: node at CCI0 master0 address **0x1a** (measured, not the stock
   DT's assumption), `clock-names = "xvclk"`, MCLK0 (gpio13), reset gpio28,
   `dvdd-supply = <&camera_vdig_wide>` (gpio90 enable), `vs5-supply`/`vl17-supply`,
   and link-frequencies matching CSIPHY0,
4. verify the chip id read (0x30dc == 0x32) proves the sensor is alive, then try
   a single 4056x3040 mode.

## Missing register tables for the other two sensors

Neither Xiaomi's public kernels (`MiCode/Xiaomi_Kernel_OpenSource` branches
`raphael-p-oss`, `cepheus-q-oss`), nor LineageOS'
`android_kernel_xiaomi_sm8150`, nor `Butterfingerss/Kernel_xiaomi_raphael`
contain the vendor per-sensor drivers - their camera directories hold only the
framework (`camera_v2/sensor/{cci,csid,csiphy,eeprom,flash,io,...}`), and the
tree searches for `imx586`, `ov8856`, `s5k3l6`, `s5k3t2` return zero hits.
So for S5K3L6 and S5K3T2 there is no public register-table source; the remaining
options are (a) the phone's own vendor partition if the camera drivers shipped as
modules (`/vendor/lib/modules/`), or (b) writing power/ID-only drivers and
discovering modes from the sensor's behaviour - recorded as a blocker, not an
assumption.


## Port progress on imx586.c (offline, not compiled yet)

`imx586.c` here is `imx586-rpi-6.1.c` after these changes:

1. **6.8+ state accessors**: `v4l2_subdev_get_try_format/crop/compose(sd, state,
   pad)` -> `v4l2_subdev_state_get_format/crop(state, pad)` (7 call sites).
   Verified against `include/media/v4l2-subdev.h` of the branch: the old helpers
   are gone and the macro `v4l2_subdev_state_get_format(state, pad, ...)` is the
   replacement.
2. **The wide module's extra rails**: optional `vs5` (PM8150 SMPS5 2.0 V) and
   `vl17` (LDO17 3.1 V) supplies, requested with
   `devm_regulator_get_optional()` and enabled in `imx586_power_on()` right after
   the clock, disabled in `imx586_power_off()` - the same shape as patch 0005 for
   ov8856, and deliberately without any `regulator-always-on`.
3. Paren/brace balance and the remaining API set (`devm_clk_get`,
   `__v4l2_ctrl_modify_range`, `__v4l2_ctrl_handler_setup`,
   `v4l2_async_register_subdev_sensor`, `.s_stream`) were checked against the
   branch headers; `.s_stream` still exists in 7.3 (deprecated but supported) and
   matches what `qcom-camss` calls.

Still to do, in order: build it as `obj-m` against the device's headers and fix
whatever else the 7.3 tree moved; add the DT node at CCI0 master0 **0x1a** with
supplies named `VANA-supply` / `VDIG-supply` / `VDDL-supply` (the driver looks up
those exact uppercase names) mapped to `vreg_bob` / `camera_vdig_wide` /
`camera_viodd_ldo`, plus `vs5-supply` / `vl17-supply`; then check the chip id
(`0x30dc == 0x32`) and try one 4056x3040 mode.

## The ultrawide (S5K3L6) is on CCI1, which this driver cannot reach

`drivers/i2c/busses/i2c-qcom-cci.c` in our tree has `#define NUM_MASTERS 2` and
maps `i2c-bus@<n>` to master `<n>`, so the single `cci@ac4a000` node exposes
**CCI0 master0/master1 only** (`/dev/i2c-3`, `/dev/i2c-4`).  The ultrawide sits on
**CCI1 master0** (i2c@ac4b000) and is therefore unreachable until either the
driver supports four masters or a second CCI node is instantiated (which needs
care: both nodes would map the shared `cci_top` region).
