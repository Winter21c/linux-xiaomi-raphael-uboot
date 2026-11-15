# Round 75: all four camera vdig enables, and the motor still works

## Device tree completion

The three missing per-camera vdig regulators are now in the deployed tree, so all
four enable GPIOs from the vendor table exist:

| regulator | GPIO | phandle | state |
|---|---|---|---|
| `camera_vdig_wide` | 90 | 0x7f1 | present, idle |
| `camera_vdig_tele` | 93 | 0x145 | present (measured high while streaming) |
| `camera_vdig_front` | 95 | 0x7f2 | present, idle |
| `camera_vdig_uw` | 34 | 0x7f3 | present, idle |

Verified on the handset after boot: all four appear under `/sys/class/regulator/` and
no regulator fails to register.

## The motor still works with the new tree

```
raphael-popup-motor soc@0:popup-motor: popup motor ready (52083 ns cruise, 720 ms)
retracted -> echo 1 -> extended   ("moved to extended")
extended  -> echo 0 -> retracted  ("moved to retracted")
```

Left retracted.

## A new experiment this round: probing the front camera while extended

With the popup working, the front module can be physically presented and its bus
probed.  `i2c-tools` was fetched and unpacked (`apt-get download i2c-tools`,
`dpkg-deb -x`), and all four CCI buses scanned with the camera extended and GPIO 95
(its VDIG_EN) held high:

| bus | devices seen |
|---|---|
| i2c-0 | 0x34,0x35,0x36,0x37 in use (tele OV8856 at 0x36 and neighbours) |
| i2c-1 | 0x5d in use |
| i2c-2 | none |
| i2c-3 | none |

The front sensor does not answer.  That is expected without its MCLK (the front
module uses `cam_cc_mclk2` on GPIO 15, which nothing requests until a driver binds),
and no front-camera driver is even built: the module tree has only `ov5640` and
`imx412` under `media/i2c`.

So bringing up the front camera is a separate piece of work: identify the sensor
(first candidate from earlier rounds: S5K3T2), enable its driver, add its DT node
with csiphy1 / MCLK2 / reset GPIO 12 / vdig GPIO 95, then stream from csiphy1.

## Deliverable status

| # | deliverable | status |
|---|---|---|
| 1 | v1.1 register map and sequence | verified on hardware |
| 2 | mainline driver on base+0x800 | built and verified on hardware |
| 3 | DTB matching the vendor wiring | **deployed**: both CCI controllers in the CCI node, all four vdig enables, motor node on LPG channel 1 |
| 4 | pop-up motor driver | **builds, loads, probes, and physically moves the camera** |
| 5 | commit | blocked - the host cannot execute any command |

Tele camera streaming remains the one open item, with every software layer verified
correct and the remaining fault isolated to the D-PHY path (physical).
