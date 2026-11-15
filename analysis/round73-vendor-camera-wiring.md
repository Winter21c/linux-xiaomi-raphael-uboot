# Round 73: the vendor camera wiring table, and the vdig GPIO is fine

## The vendor's camera description (from the shipped device tree)

`qcom,cam-sensor@N` nodes in the vendor FDT give the whole physical wiring.  The
`gpios` array is `<&tlmm pin flags>` triples and `gpio-vdig` indexes into it:

| module | csiphy-sd-index | MCLK | reset | **VDIG_EN** | label |
|---|---|---|---|---|---|
| CAMW (wide) | 0 | 13 | 28 | **90** | `CAMW_RESET`, `CAMW_VDIG_EN` |
| CAMT (tele) | **2** | 14 | 30 | **93** | `CAMT_RESET`, `CAMT_VDIG_EN` |
| CAMF (front) | 1 | 15 | 12 | **95** | `CAMF_RESET`, `CAMF_VDIG_EN` |
| CAMU (ultrawide) | 3 | 16 | 23 | **34** | `CAMU_RESET`, `CAMU_VDIG_EN` |

This confirms two things this project had only assumed:

* the tele module really is on **csiphy2** - the forced-PHY sweeps were chasing a
  problem that does not exist;
* every camera has its own **VDIG_EN GPIO** (90/93/95/34), exactly as the objective
  said.

It also confirms the two CCI controllers: `qcom,cci@ac4a000` (CCI0) and
`qcom,cci@ac4b000` (CCI1, interrupt 0x10f, clocks `cci_1_clk_src`/`cci_1_clk`,
GPIO 31/32 for `CCI_I2C_DATA2`/`CLK2`).  CCI1 has **not** been added to our DTB on
purpose: both CCI clock trees are already owned by the single `cci@ac4a000` node the
working tele sensor uses, so adding a second node risks the one bus that works, and
it would only serve the ultrawide module which has no driver yet.

## The vdig enable is actually driven

Our DTB already models it correctly:

```
camera_vdig_tele: regulator-camera-vdig-tele {
        compatible = "regulator-fixed";
        regulator-min-microvolt = <1352000>;
        regulator-max-microvolt = <1352000>;
        enable-active-high;
        gpio = <0x55 93 0>;        /* tlmm phandle 85, GPIO 93, active high */
        vin-supply = <0xff>;
};
```

and it works at runtime - measured **during** a stream:

```
gpio93  : out high func0 2mA pull down
camera_vdig_tele: state=enabled
```

So the sensor's digital core is powered; this was the last plausible "silent but
healthy over I2C" explanation and it is ruled out.

## What has now been ruled out, with measurements

| hypothesis | verdict |
|---|---|
| wrong CSID base | fixed in round 67 |
| media bus format mismatch | fixed in round 65 |
| wrong CSIPHY index | tele is csiphy2 (vendor table), and all four PHYs behave identically |
| lane mask / lane order | six masks swept, no change; the v1.1 lane tables carry no type-1 records at all |
| PHY power domain / clocks | `cam_cc_ife_0_csid_clk` 400 MHz, `ife_0_gdsc` on, `vdda-phy`/`vdda-pll` enabled |
| sensor MCLK | 19.2 MHz, and the driver has only an `OV8856_XVCLK_19_2` configuration |
| sensor rails | avdd / dovdd / dvdd all enabled during streaming |
| sensor VDIG_EN GPIO 93 | **`out high` during streaming** |
| sensor register state | `0x0100=0x01`, PLL written, `0x4800=0x44`, test pattern enabled |
| MIPI control overrides | nine `0x4800`/`0x4801` combinations, no change |

## Conclusion

The sensor is configured and powered exactly as the vendor describes, the PHY is
programmed byte-for-byte like the vendor, the CSID is proven able to receive (it
produces traffic from its own generator), and yet not one byte crosses the D-PHY.

The remaining explanations are physical: the module's MIPI output pads, the flex
connection, or a fault in the module.  Proving that needs a logic analyser or a
second camera module - neither is available in this session.
