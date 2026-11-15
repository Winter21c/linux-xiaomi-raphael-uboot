# On-device bring-up, round 64 (first run of the v1.1 driver)

## What now works

| step | evidence |
|---|---|
| kernel module builds against the installed headers | needed clang 19 (installed by hand as .debs, apt was broken) plus a flag filter; gcc cannot parse `struct ns_tree;` in this tree's `ns_common_types.h` |
| camera device tree is live | `/boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb` is loaded by U-Boot; after replacing it `camss@ac6a000` and `cci@ac4a000` appear |
| `qcom-camss` probes | `/dev/media0`, 16 video nodes, 37 entities |
| OV8856 binds | `3-0036`, after the DTB clock was corrected from 24 MHz back to the vendor's 19.2 MHz |
| sensor -> CSIPHY link | `msm_csiphy2` pad0 shows `<- "ov8856 3-0036":0 [ENABLED,IMMUTABLE]` |

Two defects were found and fixed on the way:

* the sensor node carried `assigned-clock-rates = <24000000>`; the driver only
  supports 19.2 MHz and the vendor uses 19.2 MHz, so probe failed with `-EIO`;
* the wide camera (`imx586`) has no working driver yet, and its dangling async
  connection kept the notifier from ever completing, so **no** sensor link was
  created.  Removing the `remote-endpoint` from `csiphy0_ep` lets the tele link
  come up.

## What still blocks streaming

```
csid0 hw_version reg(0x0)=0x00000000
CSID0 dump: 0x0..0x104 all 00000000
CSID reset timeout
Failed to start media pipeline: -32 (EPIPE)
```

Every CSID register reads back zero even though:

* all three CSID clocks and the three bus clocks added for the test report
  `prepare_enable` + `enabled`;
* `ife_0_gdsc` and `titan_top_gdsc` both report **on** in
  `/sys/kernel/debug/pm_genpd/pm_genpd_summary` (the driver now keeps the
  already-attached VFE domains powered, since the VFE resources carry no
  `has_pd` and the pipeline starts the CSID before the VFE);
* the device tree `reg`/`reg-names` pairing is correct (`csid0 = 0xacb3000`,
  `vfe0 = 0xacaf000`, matching the vendor tree exactly).

## A trap worth recording

Raw `ioremap()` of a camera block from inside the driver **hangs the bus and
reboots the phone** (reproduced twice).  `/dev/mem` reads of the same windows
silently return zero and prove nothing.  Never probe these blocks that way; use
the driver's own mapping and keep the probe passive.

## Next candidates for the CSID

1. The CSID may need the CAMNOC/CPAS to be configured, not just clocked; the
   vendor enables it through a separate CAMNOC block we have not touched.
2. `camss_csid_rst_cmd` exists as a knob (the vendor reset value may differ from
   the mainline `0x1 << RST_CMD`); worth sweeping it while watching for a
   non-zero `hw_version`.
3. The `ife0_csid_clk_src` rate stays at 19.2 MHz in `clk_summary`, i.e.
   `clk_set_rate()` did not take effect; a block clocked at the XO rate may not
   finish its internal reset, which would look exactly like this.
