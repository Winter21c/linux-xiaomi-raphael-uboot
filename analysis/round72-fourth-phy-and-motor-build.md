# Round 72: the fourth PHY is silent too, and the motor driver now builds

## PHY sweep finished

| forced PHY | csiphy interrupts | csid0 interrupts | bytes |
|---|---|---|---|
| 2 (device tree) | 1 per stream | 0 | 0 |
| 1 | 1 per stream | 0 | 0 |
| 0 | 1 per stream | 0 | 0 |
| 3 | 1 per stream | 0 | 0 |

All four are identical, so the physical PHY index is not the fault.  The single
interrupt each one raises when the stream starts is an internal PHY event, not
received traffic - it fires even with no sensor data.

## The MCLK hypothesis is dead

`ov8856.c` has a single clock constant, `OV8856_XVCLK_19_2 19200000`, so its mode
tables and PLL values are computed for exactly the 19.2 MHz the board provides.
There is no 24 MHz/19.2 MHz mismatch.

The sensor readback also confirms everything the driver writes sticks:

```
0100=01 0103=00 0300=00 0301=00 0302=4b 0303=01 0304=03 0305=01 0306=01 0307=00
300a=00 300b=88 4800=44 4801=00 4805=00
```

What remains unexplained is `0x4801 = 0` and `0x4805 = 0`: the mainline driver never
writes a MIPI lane-count register, and a sensor that believes it drives no lanes
would look exactly like this (perfect over I2C, silent on the pads).  The sensor's
own colour-bar test pattern (`test_pattern=1 (Standard Color Bar)`) also produces
nothing, which is consistent with the pads never being enabled.

## Deliverable (4) is now met

The pop-up motor driver builds against the running kernel and loads:

```
/root/motor/raphael-popup-motor.c
  -> raphael-popup-motor.ko   282112 bytes
insmod -> 0, module loaded, no probe (no DT node yet)
```

The only defect was an API rename: this kernel provides
`pwm_apply_might_sleep()`, not `pwm_apply_state()`.  Three call sites were fixed and
the source in `raphael-support/motor/driver/` carries the fix.

## Environment

Host `bash`/`grep`/`glob`/REA still fail with `Unknown system error -122 ... write`,
so nothing can be committed and all cross-checks are done by uploading files to the
phone and using its objdump/grep/make.
