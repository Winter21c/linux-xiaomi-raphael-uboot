# VFE / CSID reset-ack never arrives: measurements so far

State of the evidence after the DTB was rebuilt on top of the *shipped* one (so the
display, modem and camera nodes coexist).

## What works

| step | result |
|---|---|
| camss probe | `/dev/media0`, 14 video nodes, 22 camss subdevs |
| sensor bind | `3-0036` on `i2c-3` (CCI0 master1), driver `ov8856` |
| media link | `ov8856 3-0036(307) link 307:0 -> 7:0 ENABLED IMMUTABLE` (csiphy2) |
| formats | sensor / csiphy2 / csid / vfe all accept 1640x1232, mbus `0x300a` (SGRBG10_1X10) |
| buffers | allocate and mmap fine after the 64-bit DMA mask fix |
| VFE register access | **works**: `VFE2 hw_version=0x10000000`, `irq_status=0x00000000` |

## What fails

```
STREAMON: Input/output error
qcom-camss: VFE reset timeout (continuing for bring-up)
qcom-camss: CSID reset timeout
qcom-camss: Failed to power up pipeline: -5
```

`/proc/interrupts` shows **0** for every camss line (`msm_vfe0` SPI 497,
`msm_csid0` SPI 496, `msm_vfe2` 501, `msm_csid2` 500).  Both the full VFE0/CSID0
path and the lite VFE2/CSID2 path fail the same way, so it is not a
lite-instance problem.

The interrupt numbers and register windows are not the issue:
- the stock vendor DT (`sm8150-camera.dtsi`) lists exactly the same SPIs
  (csiphy 477/478/479/448, cci 460/271, csid 464/466/468/359, ife 465/467/469/360),
- the VFE/CSID windows are 0x4000 as the stock DT says,
- changing the DT trigger from edge-rising to level-high (verified in
  `/proc/device-tree/soc@0/camss@ac6a000/interrupts` as `(0, 477, 4)`) did **not**
  make the line fire either.

So the block accepts register reads but never raises its reset-complete status
bit.  The reset command write itself uses the SM8250 offsets from
`camss-vfe-480.c` (`VFE_GLOBAL_RESET_CMD` = 0x1c full / 0x0c lite,
`VFE_IRQ_MASK(0)` = 0x3c / 0x28).  The most plausible remaining explanation is
that **SM8150 does not share the SM8250 reset register map**, and the vendor
register definitions are not in any open-source tree (Xiaomi's raphael-p-oss has
no camera_v2 driver source; only DT bindings).

## Next probes, cheapest first

1. add a readback of `VFE_GLOBAL_RESET_CMD` right after the write and print it -
   if the bit does not stay set, the write went to the wrong register (the code is
   already instrumented for this in `camss-vfe-480.c`, the print just needs a
   working streaming attempt to appear),
2. sweep the plausible reset offsets (0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18,
   0x1c, 0x20) and watch `/proc/interrupts` for the first one that produces an IRQ,
3. only then look at the CSID side again (its reset may follow with the same fix).

## Fixed on the way (kept)

- `dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64))` - without it every frame goes
  through swiotlb and `REQBUFS` fails with `swiotlb buffer is full`,
- DTB power-domain order `ife0, ife1, top` (the driver attaches the block GDSC with
  `dev_pm_domain_attach_by_id(genpd_num - 1)`, i.e. TITAN_TOP must be last) and
  `has_pd = true` + `pd_name = "ife0"/"ife1"` on VFE0/VFE1,
- VFE reset failure now logs a warning and continues instead of aborting, so the
  rest of the pipeline can be exercised.

## Update: the VFE block is inert, not just missing an IRQ

A reset-offset sweep (`camss-vfe-480.c`, writes 1 to 0x00, 0x04, 0x08, 0x0c,
0x10, 0x14, 0x18, 0x1c, 0x20, 0x24 and reads back the ack register after each)
reports:

```
raphael-reset: sweep on VFE2 (lite=1), ack_off=0x40
  off 0x1c: status_before=0x00000000 status_after=0x00000000
  ... every candidate identical ...
raphael-reset: after SM8250 write cmd=0x00000000 mask=0x00000000 status=0x00000000
VFE2 hw_version=0x10000000
```

So:
* reads return a **constant** `0x10000000` (looks like a stalled/idle bus value),
* **writes never stick**: `VFE_IRQ_MASK(0)` and `VFE_GLOBAL_RESET_CMD` read back 0,
* no candidate offset produces a status bit, therefore no IRQ on any SPI.

That is the signature of a block that is not really powered/clocked or whose NoC
slave port is closed - not of a wrong reset offset.

Leads, in order of likelihood:

1. **VFE power domain**: `camcc-sm8150.c` only has `titan_top_gdsc`, `bps_gdsc`,
   `ife_0_gdsc` and `ife_1_gdsc` - there is **no ife_lite GDSC**, and camss keeps
   `has_pd = false` for the lite VFEs (VFE2/VFE3).  If SM8150 gates the lite VFEs
   through something else, that is the gap.  The full VFE0 path fails the same
   way though, and VFE0 does attach `ife0` by name.
2. **CAMSS top / CPAS**: the vendor DT has `qcom,sm8150-camnoc-virt` at
   0xac00000 and `cpas-camnoc` at 0xac00000+ which mainline camss never touches;
   the CAMSS top-level IRQ enable may live there.
3. CAMCC reset lines (`cam_cc_ife_0_clk` reset bits) may be held.

## What changed end-to-end in this round

| item | before | now |
|---|---|---|
| DTB | rebuilt from source, **no display** | built on the shipped DTB: display + modem + camera nodes |
| booted modules | stale copies from `/lib/modules` | fixed builds installed there (64-bit DMA, PD order, reset-continue) |
| CSID reset | `-EIO`, pipeline aborts | warns and continues, `STREAMON` now reaches the next layer (`EPIPE`) |
| VFE reset | `-EIO` | warns and continues, but the block proves inert |

Reproduction: `tools/mcam.c` builds on the device; the pipeline is
`ov8856 (3-0036) -> csiphy2 -> csid2 -> vfe2_rdi0`, formats 1640x1232 / `0x300a`,
video node `msm_vfe2_video0` (currently `/dev/video8`).

## The full VFE0 block IS alive (round 33)

With the pipeline routed through the **full** VFE0 (csiphy2 -> csid0 -> vfe0_rdi0)
instead of the lite VFE2, the picture changes completely:

```
raphael-reset: sweep on VFE0 (lite=0), ack_off=0x54
after SM8250 write cmd=0x00000001 mask=0x00000001 status=0x00000000
VFE0 hw_version=0x10020000
```

* **writes stick** (`GLOBAL_RESET_CMD` and `VFE_IRQ_MASK(0)` read back exactly what
  was written, unlike the lite instance which reads back 0),
* `hw_version = 0x10020000` is a real version (generation 1, minor 2),
* **the interrupt does fire**: `/proc/interrupts` for `msm_vfe0` (SPI 497) went
  1 -> 2 between attempts.

So the VFE is functional; the remaining defect is that the ISR does not recognise
the reset acknowledgement (`status & IRQ_MASK_0_RESET_ACK` where the SM8250 code
uses BIT(0) for a full VFE and BIT(17) for a lite one), so the completion never
fires even though the interrupt arrives.

Workaround in place (`camss-vfe-480.c`): log the real status the first time and
treat a pending reset as acknowledged.  With that, VFE0 passes the reset stage and
the pipeline proceeds to CSID0, whose reset also times out (now a warning), after
which `STREAMON` returns **EPIPE** - the next layer to chase, i.e. the actual
subdev `s_stream` start.

The **lite** VFE (VFE2/VFE3) stays inert (all writes lost), so for now only the
full VFE0/VFE1 paths are worth using; that still covers every CSI-2 input because
any csiphy can be linked to any csid and csid0/csid1 feed vfe0/vfe1.

## Two more real bugs fixed: the tool's fourcc map and the wrong VFE hw_ops

**1. EPIPE was a tool bug, not a driver bug.** `tools/mcam.c` resolved format
names with a small table plus `strtoul`, so `pgAA` (the packed form of the
sensor's `0x300a` = MEDIA_BUS_FMT_SGRBG10_1X10) became 0, `S_FMT` fell back to the
driver default `UYVY`, and `STREAMON` then failed the pix-vs-subdev-format check in
`camss-video.c` (`-EPIPE`).  After mapping `pBAA/pGAA/pgAA/pRAA/pBCC` to the
`V4L2_PIX_FMT_*P` values the video node keeps `pgAA`.

**2. `vfe_ops_480` does not fit the SM8150 register window.** That ops set puts the
BUS block at `0xaa00` and maps RDI wm `0` to wm `23`, so `vfe_wm_start()` writes
`0xaa00 + 0x20c + 23 * 0x100 = 0xC30C` - far outside the 0x4000 window the SM8150
VFE owns (`0xacaf000..0xacb2fff`, with csid0 right after it, so the window cannot
simply be enlarged).  The result was a data abort:

```
Internal error: Oops: 0000000096000047 [#1] SMP
pc : vfe_wm_start+0xc0/0x258 [qcom_camss]
    vfe_enable_output_v2 -> vfe_enable_v2 -> vfe_set_stream -> video_start_streaming
```

SM8250 maps its VFE as **0xd000**; SM8150's own DT says `qcom,vfe175` - the 1.7x
family - which is `vfe_ops_170`.  Switching the four SM8150 VFE entries to
`vfe_ops_170` removes the crash and the pipeline now starts.

**Current state:** `S_FMT` keeps `pgAA`, `STREAMON` succeeds (no error, the tool
blocks waiting for a buffer), but **no frames arrive** and the CSID0 interrupt
count stays 0, so nothing is coming out of the sensor yet - next layer is the
sensor's own stream-on (its I2C mode write and the CSIPHY/CSI-2 start).

## Sensor stream-on verified; the CSI-2 link still delivers nothing

With the pipeline starting cleanly, the sensor side is now instrumented and
healthy:

```
ov8856 3-0036: ov8856 s_stream(1) called
ov8856 3-0036: ov8856 start_streaming -> 0
cam_cc_csiphy2_clk 400000000 (ac65000.camss)
```

so the mode writes over CCI succeed and the PHY clock is on.  But `msm_csid0`
(SPI 496) and `msm_csiphy2` (SPI 511) interrupt counts stay **0**: no CSI-2 traffic
exists between sensor, PHY and CSID.

Two configuration bugs were found and fixed while chasing this:

| bug | evidence | fix |
|---|---|---|
| CSIPHY had **no supplies at all** | `csiphy_res_sm8150[]` had no `.regulators`, so nothing ever enabled the MIPI CSI 1.2 V rail even though the vendor DT wires `mipi-csi-vdd-supply = <&pm8150l_l3>` | added `vdda-phy`/`vdda-pll` to all four entries and `vdda-phy-supply`/`vdda-pll-supply = <&vreg_l3c_1p2>` (phandle 0x72) to the camss node |
| CSIPHY used the **3-phase** ops | `csiphy_res_sm8150[]` pointed at `csiphy_ops_3ph_1_0` (msm8996-era C-PHY/3-phase), while SM8250 and every D-PHY sensor use `csiphy_ops_2ph_1_0`, and the vendor calls this block `qcom,csiphy-v1.1` | switched all four entries to `csiphy_ops_2ph_1_0` |

Remaining suspects for the empty link:

1. **lane count**: the sensor node declares `data-lanes = <0 1>` (2 lanes).  The
   OV8856 mode in use is 1640x1232, which mainline drives with 2 lanes, but if the
   module is wired for 4 the PHY never locks.  Try `data-lanes = <0 1 2 3>` and the
   3280x2464 mode together.
2. **sensor PLL / INCK**: the mode table assumes a specific INCK; the DT sets
   19.2 MHz.  If the sensor actually runs at 24 MHz the bitrate is wrong and CSID
   cannot sync.
3. **CSIPHY programming for SM8150** may differ from SM8250's `2ph_1_0` sequence
   (the vendor calls its block v1.1).

Useful diagnostic next: read the sensor's chip id over CCI *while streaming*; an ACK
proves the module is powered and receiving clocks at that moment.

## Link is quiet although every stage reports success

Measured during a live stream (via a temporary `i2creg`/clk readout):

| stage | state |
|---|---|
| sensor `s_stream(1)` | called, `ov8856_start_streaming -> 0` (PLL + mode tables written, chip id re-verified) |
| sensor rails | viodd/BOB/S8(GPIO93)/L17 on, `vreg_l3c_1p2` (MIPI CSI 1.2 V) enabled with 2 users |
| MCLK1 | enabled at 19200000 Hz, consumer `3-0036` |
| csiphy2 clock | 400000000 Hz |
| csiphy2 IRQ (SPI 511) | 0 |
| csid0 IRQ (SPI 496) | 0 |
| VFE0 IRQ (SPI 497) | only the reset-time hits |

`video_start_streaming()` walks the pipeline backwards and calls `s_stream(1)` on
each entity; the sensor runs **last**, so csiphy2/csid0/vfe0 all accepted the call.
Everything says "started", yet no CSI-2 clock or data activity appears at the PHY.

Lane numbering was also corrected (`clock-lanes = <0>`, `data-lanes = <1 2>`,
previously `<7>` and `<0 1>`), with no change.

Remaining hypotheses, cheapest first:

1. **CSIPHY settle/link-frequency**: `csiphy_settle_cnt_calc()` derives the settle
   window from the DT `link-frequencies` (720/360 Mbps) and the timer clock.  If the
   real rate differs the PHY never locks; try the second entry / a different rate.
2. **csiphy ops for SM8150**: `csiphy_ops_2ph_1_0` is the SM8250 sequence while the
   vendor block is `qcom,csiphy-v1.1`; a wrong PLL/post-init write would leave the
   PHY idle.  Compare the register writes against the vendor's expected sequence.
3. **sensor output enable**: everything so far proves the I2C/register side works,
   not that the sensor drives its clock lane.  A scope/logic-analyzer on the module
   connector (or reading the PHY's lane status registers after a 100 ms delay) would
   settle it.

## CSIPHY: writes stick, version reads 0, no traffic

Corrected register dump (the earlier one used wrong offsets - `GLBL_PWR_CFG` is
0x144, `GLBL_RESET` 0x140, `HW_VERSION` 0x188, lane n `CFG2` at `0x004 + 0x40*n`):

```
csiphy pre  link=360000000 settle=36 mask=0x06 num_data=2 clk_pos=0 data_pos=1,2
csiphy post pwr=0x0000000d rst=0x00000000 irqcmd=0x00000000
             ln0cfg2=0x00000000 ln1cfg2=0x00000010 ln2cfg2=0x00000010
             clkcfg2=0x00000000 irqsts0=0x0 irqsts1=0x0
CSIPHY HW Version = 0x00
```

* the writes **do** reach the block (`pwr` reads back exactly the written
  `0x1 | mask<<1`, and the two data lanes hold the 0x10 we wrote),
* `HW_VERSION` (0x188) reads **0x00**, which is not a plausible PHY version,
* the clock lane is never programmed: `CAMSS_CSI_PHY_LN_CLK` is **hard-coded to 1**
  in `camss-csiphy-2ph-1-0.c`, so with `data-lanes = <1 2>` the clock lane write
  collides with data lane 1 and lane 3 stays untouched (`clkcfg2 = 0`).

Also added the missing `cphy_rx` clock (CAM_CC_CPHY_RX_CLK_SRC = 25) to the four
csiphy resources and to the camss DT clock list; the PHY still reports nothing.

Interpretation: register access works, so the block is powered and clocked, but
either 0x188 is not the version register on SM8150's `csiphy-v1.1` (i.e. **the
register map differs from SM8250's 2ph_1_0**) or the PHY never sees the sensor's
clock lane.  The lane-mask collision above is a real configuration bug regardless.

Next probes:
1. read `CSID_CSI2_RX_IRQ_STATUS`/`CSID_CSI2_RX_CFG0` after a stream start to see
   whether the CSID sees "clock lane not in high-speed" (sensor silent) versus
   "lane errors" (signal present but wrong),
2. try `data-lanes = <0 2>` so the hard-coded clock lane 1 does not collide,
3. compare the 2ph_1_0 register offsets against the vendor's `csiphy-v1.1`
   expectations (the vendor driver source is not public, so this needs probing).
