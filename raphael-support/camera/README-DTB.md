# How to build a camera DTB for raphael (important)

**Never rebuild the device tree from the kernel source for this phone.**  The
branch source and the DTB the image ships with disagree about the panel:

| | working image DTB | source rebuild |
|---|---|---|
| panel compatible | `samsung,ams639rq08` (+ vdd3p3/vsn/vsp supplies) | `samsung,ea8076` (+ vcie) |

Booting a source-built DTB therefore leaves `/sys/class/drm/` empty - no
`card0`, no desktop.

The safe recipe, used for `dtb/sm8150-xiaomi-raphael-camera.dtb`:

1. take the DTB the image boots (`/boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb`),
2. decompile it (`dtc -I dtb -O dts`),
3. add *only* the camera nodes to that text, referring to existing nodes by raw
   phandle (they have no labels after decompiling):
   - `phandle = <0x132>` on `clock-controller@ad00000` (CAMCC),
   - `phandle = <0x133>` on `ldo17` (vreg_l17a_3p0),
   - `phandle = <0x140..0x143>` on four pinmux states inside `pinctrl@3100000`
     (`cci0-default-state`, `cci1-default-state`, `cam-mclk0-default-state`,
     `cam-mclk1-default-state`),
   - fixed regulators `camera_viodd` (gpio97) and `camera_vdig_tele` (gpio93),
   - `camss@ac6a000` and `cci@ac4a000` inside `soc@0`, plus the OV8856 node at
     `0x36` on `i2c-bus@1` with the endpoint pair (`0x146` <-> `0x147`),
4. compile and check that the panel compatible is still `samsung,ams639rq08`.

With this DTB the display (`card0`, `card0-DSI-1`, `renderD128`), the modem and
the camera nodes all coexist; the sensor binds at `3-0036`
(`/dev/v4l-subdev22 = ov8856 3-0036`) and is linked to `csiphy2`
(`307:0 -> 7:0 ENABLED IMMUTABLE`).

Known remaining blocker: `STREAMON` fails with `VFE reset timeout` - the VFE never
raises its reset-complete interrupt (IRQ count stays 0).  See
`analysis/camera-vfe-reset-timeout.md`.
