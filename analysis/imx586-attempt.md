# IMX586 (wide camera) bring-up attempt - results

Goal: use a *second* sensor on a *different* PHY as a discriminator between
"OV8856 module/mode problem" and "the SM8150 CSIPHY never receives".

## What was set up

* driver: the 2062-line `imx586.c` port (built as a module, needs
  `<linux/unaligned.h>` instead of `<asm/unaligned.h>` on this tree);
* device tree node under CCI0 master 0 (`i2c-bus@0`), address `0x1a`, linked to
  `csiphy0` (`data-lanes = <0 1 2 3>`, 4-lane);
* supplies taken from the shipped vendor node
  (`qcom,cam-sensor@0`): `VANA = pm8150l_bob` (3.01 V), `VDIG = pm8150l_s8`
  (1.35 V), `VDDL = camera_viodd` (1.8 V), plus the two extras the driver knows
  about, `vs5 = pm8150_s5` (1.9 V) and `vl17 = pm8150_l17` (2.86 V);
* `CAMW_VDIG_EN` (tlmm 90) exposed as `/sys/class/leds/motor:camw_ldo` and driven
  high before probing;
* MCLK0 at 24 MHz (the driver rejects 19.2 MHz, which is what the vendor DT uses).

## Results

The driver binds every candidate address and gets as far as the chip-id read:

```
imx586 2-001a: IMX586 mono option: 0
imx586 2-001a: failed to read chip id 32, with error -5      (EIO = no ACK)
```

Tried addresses `0x10`, `0x1a`, `0x20`, `0x2d` - **none** acknowledge.  Enabling the
module LDO and adding `vs5`/`vl17` did not change it.

Two DTB lessons learned the hard way in the process:

* a `regulator-fixed` on the same GPIO as a `gpio-leds` entry makes the whole
  gpio-leds node fail to probe (all `/sys/class/leds/motor:*` entries disappear);
* `assigned-clock-rates` must match what the sensor driver accepts, the IMX586
  driver here only supports 24 MHz.

## What this does *not* prove

An I2C scan of the CCI buses from a kernel module returns **zero devices even on
the bus where the working OV8856 sits**, so CCI does not answer a bare
`i2c_transfer` the way a normal adapter does and the scan cannot be used to look
for the camera.  Consequently "no ACK at 0x1a" cannot yet be distinguished from
"the wide module is simply not powered/enumerated in this unit".

Next step if this is pursued: find the address the vendor driver actually uses for
`qcom,cam-sensor@0` (its `qcom,slave-id` array is in the userspace HAL, not in DT),
or power the module from the vendor's own sequence and re-probe.
