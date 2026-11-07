# Camera support for the Redmi K20 Pro (raphael, SM8150)

Status: **not working yet**.  This directory collects the verified hardware map,
the missing kernel pieces and a DT/patch starting point, so the port can be done
inside the kernel tree (it cannot be finished as a runtime overlay).

## Hardware map (from the stock Android device tree, `sm8150-camera.dtsi` +
## `raphael-sm8150-camera-sensor.dtsi`)

| Module | Function | CSIPHY | CCI | master | MCLK | reset | VDIG en |
|---|---|---|---|---|---|---|---|
| cam-sensor@0 | rear wide, 48 MP (IMX586) | 0 | 0 | 0 | gpio13 (MCLK0) | gpio28 (CAMW_RESET) | gpio90 (CAMW_VDIG_EN) |
| cam-sensor@1 | rear tele, 8 MP | 2 | 0 | 1 | gpio14 (MCLK1) | gpio30 (CAMT_RESET) | gpio93 (CAMT_VDIG_EN) |
| cam-sensor@2 | rear ultra-wide, 13 MP (S5K3L6) | 1 | 0 | 0 | gpio15 (MCLK2) | gpio12 (CAMF_RESET) | gpio95 (CAMF_VDIG_EN), gpio31/32 |
| cam-sensor@3 | pop-up front, 20 MP (S5K3T2) | 3 | 1 | 0 | gpio16 (MCLK3) | gpio23 (CAMU_RESET) | gpio34 (CAMU_VDIG_EN) |

Supplies (rear wide): `cam_vio` 1.8 V (fixed LDO), `cam_vana` PM8150L BOB
3.008–4.0 V, `cam_vdig` PM8150L S8 1.352 V, `cam_v_custom1` PM8150 S5
1.904–2.04 V, `cam_v_custom2` PM8150 L17 3.1 V, `cam_clk` = TITAN_TOP_GDSC,
MCLK 19.2 MHz from CAMCC (`CAM_CC_MCLK0..3_CLK`).

Register bases (SM8150):

```
CAMSS top      0x0ac00000
CCI0           0x0ac4a000      CCI1          0x0ac4b000
CSIPHY0        0x0ac65000      CSIPHY1       0x0ac66000
CSIPHY2        0x0ac67000      CSIPHY3       0x0ac68000
CSID0          0x0acb3000      CSID1         0x0acba000
CSID-LITE0     0x0acc8000      CSID-LITE1    0x0accf000
VFE0           0x0acaf000      VFE1          0x0acb6000
VFE-LITE0      0x0acc4000      VFE-LITE1     0x0accb000
CAMCC          0x0ad00000      (already present and bound in the current DTB)
```

## What is missing

1. **CAMSS driver has no SM8150 support.**  `qcom-camss.ko` matches
   `qcom,sm8250-camss`, `qcom,sm6150-camss`, `qcom,sdm845-camss`, ... but not
   SM8150.  `patches/0001-camss-add-sm8150-support.patch` adds the
   `qcom,sm8150-camss` compatible with SM8150 resource tables (4 CSIPHY, 2
   CSID + 2 lite, 2 VFE480 + 2 lite).  The module builds against the running
   kernel headers and loads.

   Two device-tree obstacles had to be worked around in that patch:

   * the CAMCC node (`/soc@0/clock-controller@ad00000`) has **no phandle**
     (nothing references it), and adding one through an overlay crashes the
     kernel in the OF property-change notifier chain (`of_property_notify`), so
     the driver resolves its clocks by node path instead (`camss_clk_get()`);
   * the same applies to `power-domains`, so the CAMSS power domains are made
     optional; the GDSC state is left as the bootloader set it up.
2. **The board DT has no CAMSS/CSIPHY/CCI nodes** (`sm8150-camera.dtsi` in
   `sm8150-camera-dt.dtsi` is not part of the mainline sm8150.dtsi).  The nodes
   in `sm8150-camera.dtsi` here are a mainline-binding translation of the stock
   nodes.
3. **Sensor drivers.**  The tree ships `ov8856.c`, `imx412.c`, `ov5640.c` ... but
   nothing for **IMX586**, **S5K3L6** or **S5K3T2**.  Only the OV8856 module
   (tele, CSIPHY2) can be brought up with an existing driver.
4. **Media controller pipeline**: after CAMSS + a sensor probe, `libcamera`
   needs a pipeline handler with the SM8150 CAMSS support (upstream libcamera
   has `simple`/`rkisp`/... but no CAMSS pipeline yet).

## Suggested order of work

1. Apply `0001-camss-add-sm8150-compatible.patch`, rebuild `qcom-camss.ko`
   against the running kernel headers, and add the CSIPHY/CCI/CAMSS nodes to
   the board DTS (in the kernel tree — a runtime overlay is not enough because
   CAMSS probes the whole media graph at boot).
2. Verify with `media-ctl -p` that CSIPHY0..3, CSID0/1 and VFE0/1 are registered
   and that the CAMCC clocks can be enabled.
3. Add the OV8856 node (existing driver) and check I2C communication over CCI
   (`i2c-qcom-cci`); the sensor ID read proves the CCI/CSIPHY wiring.
4. Port an IMX586 driver (the sm8250 mainline tree has an out-of-tree IMX586
   driver that can be adapted) and wire it to CSIPHY0.
5. Write the libcamera pipeline handler.


## How the port is being done (updated)

The runtime-overlay route hits a hard wall: the CAMCC node
(`/soc@0/clock-controller@ad00000`) has **no phandle**, and adding one to a live
tree through an overlay crashes the kernel inside the OF property-change
notifier chain (`of_property_notify`).  Without that phandle the CAMSS/CCI and
sensor nodes cannot reference their clocks or power domains.

So the camera port uses the **boot device tree**:

1. `patches/0001-camss-add-sm8150-support.patch` - CAMSS SM8150 support
   (resource tables, compatible, optional power domains).  Build with
   `build-camss.sh`, which compiles the module against
   `/lib/modules/$(uname -r)/build`.
2. `patches/0002-dts-raphael-camera-nodes.patch` - adds `camss@ac6a000`,
   `cci@ac4a000` (with `i2c-bus@0/1` and OV8856 nodes) and the camera 1.8 V LDO
   to `sm8150-xiaomi-raphael.dts`.  Build with `build-dtb.sh`; the result is
   verified to contain the camera nodes and to reference the correct CAMCC/GCC
   phandles.

### Installing the DTB

```
# on the phone, as root
cp /boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb /boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb.bak
cp sm8150-xiaomi-raphael-camera.dtb /boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb
sync
reboot
```

U-Boot prints `Using standard DTB` when it loads the file and falls back to its
built-in legacy DTB (`!!! WARNING !!! Using legacy DTB`) when the file is
missing, so a *missing* DTB is recoverable; a *corrupt* file is not - always
verify the image with `dtc -I dtb` (the build script does) before copying it.
The original DTB is also kept in the rootfs image, and the phone can always be
re-flashed with `rootfs.img` via fastboot.

### Expected results after the reboot

```
dmesg | grep -iE "camss|cci|ov8856"
ls /dev/media*          # media device registered by camss
i2cdetect -l            # Qualcomm-CCI adapters
```

The OV8856 driver reads its chip ID at probe time; a message like
`ov8856 0-0010: chip ID 0x8856` proves that the CCI bus, the sensor supplies
and the MCLK from CAMCC all work.


## Safe testing (important)

The camera module must **not** be installed under `/lib/modules`: udev
autoloads it at boot and a wedged probe takes the whole system down (see
`README-rescue.md`).  Use

```
sudo ./test-camera.sh /root/camss-final/qcom-camss.ko
```

which `insmod`s the module from `/root`, logs to `/root/camera-test.log`, and
prints the media topology.  If the system wedges, follow `README-rescue.md`
(`build-rescue.sh` + `fastboot boot`).


## One-shot bring-up

```
# after a clean boot (camera DTB installed, no module under /lib/modules)
sudo ./run-camera-bringup.sh /root/camss-final
```

It patches (0001 + 0003), builds against the running kernel, dumps the CAMCC
clock state, reloads the CCI driver, `insmod`s camss from the source directory
and prints the media topology.  A hardware watchdog is armed first, so a wedged
probe resets the phone instead of leaving it stuck, and the log is written to
`/root/camera-bringup.log`.


## Bring-up order (why two DTB variants)

`i2c-qcom-cci` and `qcom-camss` are independent drivers.  CCI is autoloaded by
udev at boot and its probe currently fails/hangs (`CCI reset timeout`), which
made it hard to tell whether a wedge comes from CCI or from CAMSS.  Two DTB
builds are therefore provided:

* `sm8150-xiaomi-raphael-camera.dtb` - camss **and** cci (the full target),
* `sm8150-xiaomi-raphael-camss-only.dtb` - camss only, no `cci@ac4a000`, so
  `/dev/media0` can be validated without the CCI block in the picture.

Recommended sequence:

1. boot the camss-only DTB, `insmod` the module, check `/dev/media0` and the
   CSIPHY/CSID/VFE entities (`media-ctl -p`),
2. fix the CAMSS clock/power problem until that works,
3. switch to the full DTB and get CCI (and then the OV8856 chip ID) working.


## MILESTONE (verified on hardware)

CAMSS **registers** on the Redmi K20 Pro with the mainline driver:

```
/dev/media0                                          # media device
/sys/class/video4linux/video0..13                    # 14 VFE output nodes
  video0-2   msm_vfe0_video0..2     (VFE0)
  video3-5   msm_vfe1_video0..2     (VFE1)
  video6-9   msm_vfe2_video0..3     (VFE lite0)
  video10-13 msm_vfe3_video0..3     (VFE lite1)
```

and the CAMCC clocks are really taken:

```
cam_cc_ife_lite_0_csid_clk ... ac65000.camss  ife_lite_csid
```

Three more fixes were needed on top of the SM8150 tables (all in patch 0001):

1. **power domains are optional** - attaching the TITAN_TOP GDSC by index can
   fail; the probe now warns and continues with the GDSC state left by the
   bootloader instead of failing with -ENOTCONN,
2. **ports without a remote endpoint are skipped** - `camss_parse_ports()`
   returned -ENOTCONN and killed the probe whenever a CSIPHY port had no
   `remote-endpoint` (i.e. before any sensor is described),
3. the CAMSS clock lookup by node path (see above).

Bring-up log from the working load:

```
raphael-camss: probe begin (res ver 9)
raphael-camss: icc_get -> 0
raphael-camss: attaching top GDSC by id 2
raphael-camss: configure_pd -> 0 (genpd_num 3)
raphael-camss: init_subdevices -> 0
raphael-camss: dma_set_mask -> 0
raphael-camss: v4l2_device_register -> 0
raphael-camss: port without remote endpoint, skipped   (x4)
raphael-camss: parse_ports -> 0
-> /dev/media0 and 14 video nodes
```

Next: the sensors.  `i2c-qcom-cci` still needs work (it reported
`CCI reset timeout` with the old overlapping register windows), then the OV8856
node can be probed and its chip ID read.


## CCI transfers work (verified)

The last missing piece for the CCI bus was **pinctrl**: neither the CCI pads nor
the CAMCC MCLK pads were muxed, so the controller had no bus and the sensors had
no clock.  The kernel's own tables give the answer:

```
function 20: cam_mclk, groups = [ gpio13 gpio14 gpio15 gpio16 ]
function 22: cci_i2c,  groups = [ gpio17 gpio18 gpio19 gpio20 gpio31 .. gpio34 ]
```

Patch 0002 therefore adds `cci0` (gpio17/18), `cci1` (gpio19/20) and
`cam_mclk0/1` (gpio13/14) states.  The MCLK states are hung off the **CCI node**
instead of the sensor nodes on purpose: the driver core releases a sensor's
pinctrl state when its probe fails, which would take the MCLK pads away again
mid-scan.

Evidence:

```
pin 17..20: device ac4a000.cci function cci_i2c group gpio17..20
pin 13/14:  device ac4a000.cci function cam_mclk group gpio13/gpio14
/dev/i2c-3 scan: 0x54 ACK      # first successful CCI transfer
```

With `keep_power=1` on the ov8856 driver the rails and the MCLK stay on
(`camera_viodd`, `camera_vdig_wide/tele`, `cam_cc_mclk0/1_clk` all report one
user), yet **no sensor answers at any address 0x08-0x77 on either master** -
only the device at 0x54 (most likely the camera EEPROM).  So the remaining
question is sensor power/address, not the CCI path.

Tools used (in `tools/`): `i2cprobe8.c` (8-bit register probe), `i2cprobe16.c`
(16-bit register probe), `gpioset.c` (GPIO v2 char device helper).


## Ground truth: the stock camera device tree

The vendor partition is still on the phone, and Xiaomi publishes the kernel tree, so
the stock camera configuration can be read instead of guessed.
`camera/reference/raphael-sm8150-camera-sensor.dtsi` (from
`Butterfingerss/Kernel_xiaomi_raphael`, branch `14-dsp`) says:

| camera | csiphy | cci device/master | MCLK pad | reset | vdig_en | extra rails |
|---|---|---|---|---|---|---|
| wide IMX586 (cell0) | 0 | cci0 / master 0 | gpio13 | gpio28 | gpio90 | **pm8150_s5 1904-2040 mV**, **pm8150_l17 3100 mV** |
| tele OV8856 (cell1) | 2 | cci0 / master 1 | gpio14 | gpio30 | gpio93 | pm8150_l17 3100 mV |
| front S5K3T2 (cell2) | ? | cci0 / master 0 | gpio15 | ? | ? | ? |

Supplies always used: `cam_vio` = camera_viodd_ldo (1.8 V),
`cam_vana` = pm8150l_bob (3.008-4.0 V), `cam_vdig` = pm8150l_s8 (1.352 V).

That is the piece that was missing: the wide module additionally needs the PM8150
**S5 (2.0 V)** and **L17 (3.1 V)** rails, which this port never enabled.  The
initial attempt (fixed-regulator wrappers with `regulator-always-on` and
`vin-supply = <&vreg_s5a_2p0> / <&vreg_l17a_3p0>`) registers
`camera_vl17_3p1` but does not actually take the PMIC rail, so that still needs
to be fixed properly (set the rail in the pm8150 regulator node itself, or drive
it from a camera power helper).

`camera/reference/raphael-pinctrl.dtsi` confirms the pad usage:
`cam_sensor_active_wide` = gpio28 + gpio90, `cam_sensor_active_tele` = gpio30 +
gpio93, both plain GPIO function.
