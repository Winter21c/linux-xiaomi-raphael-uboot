# Round 78: full power-up harness for a camera module that has no driver

## The problem

The per-camera vdig/vio rails are plain `regulator-fixed` nodes, so they only switch
on when a consumer requests them.  Before a sensor driver exists there is no consumer,
and user space cannot force them (`/sys/class/gpio` is absent, and since round 75 the
regulators own those pins so `gpioset` is refused with EBUSY).  The module therefore
never gets power and never answers on I2C - which is exactly what all earlier front
camera probes saw.

## The tool

`raphael-support/camera/tools/cam-power-hold.c` takes regulators by their global names
and holds them enabled:

```
insmod cam-power-hold.ko supplies=camera_viodd,camera_vdig_front
  -> cam-power-hold: holding camera_viodd at -22 uV
  -> cam-power-hold: holding camera_vdig_front at 1352000 uV
```

(the first voltage readback is bogus because that rail reports no voltage; the enable
still took effect).

Together with the round-76 `cam-mclk-hold` (clock by id) and `gpioset` for the reset
line, a module can now be brought to a fully powered, clocked, out-of-reset state with
no driver at all.

## Result on the front camera

Everything the vendor tree says it needs is now asserted at once:

| item | state |
|---|---|
| `camera_viodd` (GPIO 97, 1.8 V) | enabled by cam-power-hold |
| `camera_vdig_front` (GPIO 95, 1.352 V) | enabled by cam-power-hold |
| MCLK2 (`camcc` id 86) | held at 19.2 MHz by cam-mclk-hold |
| pinctrl gpio15 -> `cam_mclk` | muxed (round 76 fix) |
| reset GPIO 12 | tried both `out high` and `out low` |
| qcom-camss | loaded (so the CAMSS GDSC is powered) |
| popup camera | physically extended |

And `i2cdetect` on CCI0 master0 (`/dev/i2c-2`) still reports **no device**, on every
bus, in both reset polarities.

For reference, the bus mapping seen on this boot: i2c-0 shows 0x34..0x37 in use and
i2c-1 shows 0x5d in use; the tele OV8856 (device name `3-0036`) sits on i2c-3.

## Interpretation

Either the front module is genuinely not answering (faulty module, or its flex is not
seated), or it needs a supply we have not identified - the vendor lists five for CAMF
(`cam_vio`, `cam_vana`, `cam_vdig`, `cam_clk`, `cam_v_custom1`), and we can only drive
two of them plus the GDSC.

This is the same wall the tele camera hits on the MIPI side: everything the software
side can assert has been asserted, and the hardware still does not respond.  Neither
camera can be taken further without physical access to the modules.
