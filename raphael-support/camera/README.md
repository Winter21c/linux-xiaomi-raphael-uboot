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
