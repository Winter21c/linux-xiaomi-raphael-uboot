# Round 76: front camera bring-up started - MCLK held, pins muxed, still no ACK

## Deliverable (3) completed: all four per-camera vdig enables

Deployed and verified on the handset:

| regulator | GPIO | phandle | present |
|---|---|---|---|
| `camera_vdig_wide` | 90 | 0x7f1 | yes |
| `camera_vdig_tele` | 93 | 0x145 | yes |
| `camera_vdig_front` | 95 | 0x7f2 | yes |
| `camera_vdig_uw` | 34 | 0x7f3 | yes |

The motor driver still probes and still moves the camera with this tree.

## A new bring-up tool: `cam-mclk-hold`

A camera module only answers on I2C while its MCLK runs, and nothing requests that
clock until a driver binds.  User space cannot do it either - writing
`clk_enable_count` in debugfs returns EPERM.

`raphael-support/camera/tools/cam-mclk-hold.c` is a 60-line module that takes a CAMCC
clock id and holds it enabled:

```
insmod cam-mclk-hold.ko clk_id=86
  -> cam-mclk-hold: holding camcc id 86 at 19200000 Hz
```

The id comes from the same `<&camcc ID>` cell the device tree uses: the tele module
uses id **85** (MCLK1) and id **86** is MCLK2, the front module's clock.  The first
attempt used `of_clk_get(np, index)` and failed with `-ENOENT` for every index - that
API parses the node's *own* `clocks` property, not the provider's clock list; the fix
is to build an `of_phandle_args` against the camcc node and call
`of_clk_get_from_provider()`.

## The front camera's MCLK pin was never muxed

The CCI node's pinctrl only muxed two camera clocks:

```
pin 13 (GPIO_13): ac4a000.cci function cam_mclk   <- wide   MCLK0
pin 14 (GPIO_14): ac4a000.cci function cam_mclk   <- tele   MCLK1
pin 15 (GPIO_15): UNCLAIMED                       <- front  MCLK2
pin 16 (GPIO_16): UNCLAIMED                       <- ultra  MCLK3
```

Two new pinctrl groups (`mclk2_pins` on gpio15, `mclk3_pins` on gpio16, both
`function = "cam_mclk"`) are now appended to the CCI node's `pinctrl-0`, and after
deployment:

```
pin 15 (GPIO_15): device ac4a000.cci function cam_mclk group gpio15
pin 16 (GPIO_16): device ac4a000.cci function cam_mclk group gpio16
```

This is a real bug fixed regardless of whether the front sensor can be brought up:
without it the front and ultrawide modules could never receive a clock.

## Where the front sensor should be

From the vendor tree, `qcom,cam-sensor@1` (CAMF):

```
csiphy-sd-index = <0x01>;  cci-device = <0x00>;  cci-master = <0x00>;
```

so it sits on **CCI0 master 0**, which is our `/dev/i2c-2` (the tele on master 1 is
`/dev/i2c-3`).

## Probe result so far: no ACK

With the camera extended, MCLK2 held at 19.2 MHz, `camera_viodd` (GPIO 97) and
`camera_vdig_front` (GPIO 95) asserted and the reset line (GPIO 12) pulsed low->high:

| bus | devices |
|---|---|
| i2c-0 | 0x34..0x37 in use (tele side) |
| i2c-1 | 0x5d in use |
| **i2c-2** | **nothing** |
| i2c-3 | nothing |

Even the wide IMX586 at 0x1a does not answer on i2c-2, which matches the older note in
`STATUS.md` that no sensor except the tele has ever acknowledged.

So the next step for the front camera is the same shape as for the tele: the bus is
known, the clock is now available; what is missing is the correct power/reset
sequence and the sensor identity (no front-camera driver is even built - only
`ov5640` and `imx412` exist under `media/i2c` in this kernel).

## Deliverable status

| # | deliverable | status |
|---|---|---|
| 1 | v1.1 register map and sequence | verified on hardware |
| 2 | mainline driver on base+0x800 | built and verified on hardware |
| 3 | DTB matching the vendor wiring | **deployed**: both CCI controllers, all four vdig enables, all four MCLK pads muxed, motor node on LPG channel 1 |
| 4 | pop-up motor driver | builds, loads, probes, physically moves the camera |
| 5 | commit | blocked - the host cannot execute any command |
