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


## Round 65: the v1.1 CSIPHY is verified working on hardware

After matching every pad to the sensor's actual Bayer order the pipeline starts and
the PHY is programmed.  The driver's own readback, printed right after the lane and
IRQ tables are replayed:

```
csiphy2 v1.1 readback: 0x000=00000090 0x700=00000080 0x82c=000000ff 0x800=00000000
```

* `0x000 = 0x90` - data lane 0 block, exactly the vendor's table value
* `0x700 = 0x80` - clock lane block, confirms the clock lane really is at 0x700
* `0x82c = 0xff` - first of the eleven interrupt masks
* `0x800 = 0x00` - reset released

This confirms on real hardware everything the reverse engineering had claimed: the
0x800-based control block, the four lane blocks at 0x000/0x200/0x400/0x700, the
14-record lane tables, the reset table semantics and the IRQ mask block.

### The `-EPIPE` was a format mismatch, not a link or power problem

The kernel refuses to start a pipeline whose connected pads disagree on the media
bus format, and reports it as `-EPIPE` from `video_device_pipeline_alloc_start()`.
The sensor reports **SGRBG10_1X10** (its native order) and every downstream pad had
been configured for SBGGR10_1X10.  Setting csiphy2, csid0 and vfe0_rdi0 to
SGRBG10_1X10 and the video node to `pgAA` makes the pipeline start.

Worth remembering: an `-EPIPE` from the pipeline start is a **format** mismatch,
not a missing link; a missing link gives `-ENOLINK`.

### What is left: the CSID does not answer

The pipeline now starts and reaches `s_stream`:

```
csid0 rx_cfg0: wrote=0x00200211 readback=0x00000000
csid0 state: en_vc=0x1 lane_cnt=2 lane_assign=0x21 csiphy_id=2 phy_ok=1
```

The configuration the driver computes is right (2 lanes, csiphy2, lane assign 0x21),
but every CSID register reads back zero while the CSIPHY two blocks away answers
normally.  `cam_cc_ife_0_clk_src` is at 400 MHz and `ife_0_gdsc` reports on, so the
clock tree looks correct; the CSID window itself stays dead.  That is the next thing
to investigate, and it is independent of the CSIPHY work.
