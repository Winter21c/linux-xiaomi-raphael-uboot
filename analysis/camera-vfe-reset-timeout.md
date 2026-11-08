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

## CSID is programmed correctly by the driver but its registers read 0

`csid_set_stream()` now prints the driver-side state and the hardware registers:

```
csid0 state: en_vc=0x1 need_vc=0 lane_cnt=2 lane_assign=0x21 csiphy_id=2 phy_ok=1
csid0 rx:    cfg0=0x00000000 rxirq=0x00000000 top=0x00000000 phycfg=0x00000000
csid0 hw_version reg(0x0)=0x00000000
```

So the driver knows everything it needs (one virtual channel, 2 lanes, lane map
0x21, fed by csiphy2) and `configure_stream()` was even forced to run
unconditionally, yet `CSID_CSI2_RX_CFG0` reads back 0 - the write never lands.

`csid_res_sm8150` was tried with **both** `csid_ops_gen2` (SM8250) and
`csid_ops_4_7`: gen2 never issues a reset at all (it only masks IRQs and waits),
4_7 issues one and times out, and in both cases every CSID register still reads 0
while the interrupt counter stays at 0.

Same signature as the lite VFE2/VFE3 earlier: block reads 0, writes lost, no IRQ.
The full VFE0 is the only camss block that behaves like real hardware
(`hw_version=0x10020000`, writes stick, IRQ 497 fires).

Working hypothesis: on SM8150 the CSID and the lite VFE sit behind an additional
gate that mainline camss never opens (the vendor's `qcom,cam-cpas@ac40000` /
`camnoc` block at 0xac40000 has no mainline driver, and the vendor CSID node lists
`cphy_rx_clk_src` + `ife_cphy_rx_clk` for a *second* domain).  Until that gate is
found, the CSI-2 path cannot carry data even though the sensor, PHY power and all
clocks look correct from the driver's point of view.

## Raw register peek confirms: CSID drops writes, CPAS cannot be touched

`tools/camera_peek.c` (a module that ioremaps blocks, prints every non-zero word and
then refuses to stay loaded) was used for two measurements:

1. **idle**: every camera block reads back 0 - the blocks are power collapsed, which
   also explains why reading `cpas_top` (0xac40000) or `camnoc` (0xac42000) in that
   state raised an external abort and rebooted the phone twice;
2. **during a stream**: VFE0 holds real content (`hw_version = 0x10020000`) while
   CSID0 still reads 0 and silently drops writes
   (`csid0 core_ctrl0: wrote=0x00000211 readback=0x00000000`).

So the CSID is not misconfigured by the driver - it does not accept register writes
at all, with `ife_0_csid_clk` enabled at 400 MHz and `ife_0_gdsc` + `titan_top_gdsc`
powered.  The vendor's `qcom,cam-cpas@ac40000` block (cam_cpas_top + cam_camnoc),
which mainline camss never drives, remains the prime suspect for the missing gate,
but it cannot be probed while collapsed.

## Definitive: CSID0 rejects every register write (method validated on VFE0)

A probe module writes `tag | offset` into each register of a block and reads it back.
Validation on the block we know works:

```
poke: vfe0[0x5c] HOLDS 0x5a5a005c
poke: vfe0 held 1 offsets in [0x0,0x60)
poke: csid0 held 0 offsets in [0x0,0x60)
```

So the method is sound (VFE0 keeps a written value) and **CSID0 keeps nothing** -
0 of 24 offsets - while running a stream, with `cam_cc_ife_0_csid_clk` enabled at
400 MHz, `cam_cc_ife_0_clk` at 400 MHz, `ife_0_gdsc` and `titan_top_gdsc` powered
and the driver-side CSID state fully populated (`en_vc=0x1`, `lane_cnt=2`,
`lane_assign=0x21`, `csiphy_id=2`).

Additional negative results from this round:

* the **CSID test-pattern generator** (V4L2_CID_TEST_PATTERN = 0x009f0903 set to 1)
  produces no frame and no interrupt either, i.e. the CSID cannot even source data
  from its own TPG;
* the vendor's `qcom,cam-cpas@ac40000` (cam_cpas_top) and `camnoc`@0xac42000 read
  **0/32 non-zero words** while the camera domain is powered, so they are not
  configured (and touching them while collapsed raises an external abort);
* the four CAMCC clock sets for IFE0 are all on during the stream, so this is not a
  clock-enable problem.

Conclusion: on SM8150 the CSID block needs an initialisation that mainline camss
does not perform and that is not derivable from the public tree (the vendor's
camera driver is absent from raphael-p-oss).  VFE0 alone is not enough to capture
CSI-2, so **no camera can stream on mainline until the CSID is brought up**.

## CORRECTION + breakthrough: the CSID is alive; mainline used the wrong register map

The vendor camera module `camera.ko` was extracted from the HyperOS (Mi 9 / dada,
also SM8150) `vendor_dlkm` partition and its register tables dumped into
`raphael-support/camera/vendor-re/vendor-register-tables.md`.

With the block powered (a stream running), the CSID answers:

```
csid_gate: initial         hw_ver=0x10000000  cgc_ovd=0x00000000
csid_gate: after cgc_ovd=1 hw_ver=0x10000000  cgc_ovd=0x00000001
```

So **`CSID_HW_VERSION` reads 0x10000000 and register writes do land** - the
earlier "CSID rejects every write" conclusion was a false negative: the probe wrote
`0xA5A50004` into registers with masked fields (0x04 only implements a couple of
bits), so a mismatch was expected even on a healthy block.

### The vendor 175_200 map vs upstream gen2

| register | vendor `cam_ife_csid_175_200_cmn_reg_info` | upstream `camss-csid-gen2.c` |
|---|---|---|
| hw_version | 0x00 | 0x00 |
| **cgc_ovd** | **0x04** | *missing* |
| **core_cfg** | **0x08** | *missing* |
| **core_ctrl** | **0x0c** | *missing* (upstream never programs lane count/assign in core_ctrl) |
| **rst_cmd** | **0x10** | *missing* (gen2 never triggers a reset) |
| **rst_strobes** | **0x14** | **0x10** (i.e. upstream writes strobes into rst_cmd) |
| csi2 irq status/mask/clear/cmd | 0x20/0x24/0x28/0x2c | 0x20/.. matches |
| csi2 rx_cfg0/1 | 0x100/0x104 | matches |
| top irq status/mask/clear | 0x70/0x74/0x78 | matches |

`camss-csid-gen2.c` was therefore patched to the vendor map (cgc_ovd, core_cfg,
core_ctrl, rst_cmd, rst_strobes at the vendor offsets, plus the lane
count/assignment write into `CORE_CTRL`).  `csid_res_sm8150` uses `csid_ops_gen2`
again.

### Also extracted

`csiphy_2ph_v2_1_1_reg` - the vendor's **CSIPHY D-PHY v1.1** initialisation
sequence: 103 records of `{offset, value, mask, delay}`, listed in
`vendor-re/vendor-register-tables.md`.  Upstream's `csiphy_ops_2ph_1_0` uses a
different layout (it writes `GLBL_PWR_CFG` 0x144, `GLBL_RESET` 0x140, lane
`CFG2` 0x004+0x40n), so porting this table is the next step for the PHY.

## Vendor CSID map + vendor CSIPHY sequence applied; traffic still zero

This round integrated both vendor artefacts:

1. `camss-csid-gen2.c` now uses the **vendor 175_200 map** (cgc_ovd 0x04,
   core_cfg 0x08, core_ctrl 0x0c with the lane count/assignment, rst_cmd 0x10,
   rst_strobes 0x14 instead of 0x10).
2. `camss-csiphy-2ph-1-0.c` now replays the **vendor D-PHY v1.1 init sequence**
   (103 records, `csiphy_2ph_1_1_seq`) after its own writes, since SM8150 uses a
   0x400 lane stride and a 0xe00 global block instead of upstream's 0x40 stride.

Evidence the sequence really runs (post-program dump changed):

```
csiphy post pwr=0x0000000d rst=0x0 irqcmd=0x0
            ln0cfg2=0x0000000c   <- vendor record 0x04=0xc landed (was 0x10)
            ln1cfg2=0x00000010 ln2cfg2=0x00000010 clkcfg2=0x00000000
```

But `msm_csiphy2` (SPI 511) and `msm_csid0` (SPI 496) counters **remain 0**, and
`STREAMON` still returns no buffer.  So neither the CSID nor the PHY register map
was the last blocker - the link carries nothing even though:

* the sensor reports `start_streaming -> 0` and answers I2C,
* MCLK1 runs at 19.2 MHz and all sensor rails are on,
* CSID reads `hw_version=0x10000000` and its core_ctrl now holds the lane map,
* the PHY accepts and keeps the vendor init values.

Remaining explanations, in order of likelihood:

1. the sensor does not actually transmit (its stream-on register may be written but
   the MIPI output stage left disabled, or the mode table does not match the module);
2. the CSI-2 lanes are wired differently from `data-lanes = <1 2>` (the PHY would
   then see nothing while everything reads back fine);
3. a CAMSS-top/CPAS gate that no mainline code opens (the vendor CPAS block still
   reads 0/32 and there is no mainline driver for it).

Cheapest next discriminator: bring up the wide IMX586 (same csiphy/csid/vfe path,
independent sensor) - if it also produces no traffic the fault is in the shared
path; if it does, the OV8856 mode/stream-on is at fault.

## The sensor really streams; the PHY's global block is inert

Sensor-side proof, read back through the driver right after stream-on:

```
ov8856 3-0036: stream state: 0x0100=0x01 0x4800=0x44 0x4801=0x00 0x4808=0x1a
```

`0x0100 = 0x01` is the streaming mode bit and `0x4800 = 0x44` has the MIPI output
enabled, so the module is transmitting (its chip id, rails, MCLK and I2C were
already proven).  Hypothesis "the sensor never started" is therefore dead.

CSIPHY2 census during that same stream (vendor block layout, 0x400 stride):

| block | non-zero registers | write test |
|---|---|---|
| 0x000 | 14 | writes to 0x90/0x94/0x98 **do not stick** |
| 0x400 | 12 | writes **do not stick** |
| 0x800 | 11 | writes stick (`0x94 = 0xd7` persists) |
| 0xC00 | 11 | partial |
| **0xE00** | **0** | not programmed at all |

So the vendor sequence lands only in the lane blocks whose clocks are open, and the
**global 0xE00 block is completely inert** - it holds none of the vendor's 18 global
records (0xE00/0xE04/0xE08/0xE0C/0xE10/0xE14/0xE1C/0xE20/0xE28/0xE2C/0xE30/0xE34/
0xE38/0xE3C/0xE90/0xE94/0xE98/0xEA0) and rejects writes.

That block most likely holds the PHY PLL / lane-clock enables, which explains why
only one lane block is live and no CSI-2 activity reaches CSID.  Next step: find
what gates 0xE00 (a separate "csiphy top" power/clock, or a CAMCC BCR/clock we have
not enabled - the vendor csiphy node lists `cphy_rx_clk_src`, `csiphyN_clk`,
`csiNphytimer_clk_src` and `csiNphytimer_clk`, and only the two gate clocks plus the
shared `cphy_rx` are currently taken).

## The 0xE00 group is not implemented in any CSIPHY window

Writability test of the CSIPHY global block, run with a live pipeline:

```
glb: [0x000] before=0x0000008e wrote=0x0000008e after=0x0000008e   OK
glb: [0x400] ... OK      glb: [0x800] ... OK      glb: [0xc00] ... OK
glb: [0xe00] before=0x00000000 wrote=0x00000002 after=0x00000000   dropped
glb: [0xe28] ... dropped     glb: [0xe90] ... dropped   glb: [0xea0] ... dropped
```

and the same offset is equally dead in all four PHY windows
(`csiphy0+0xE00` .. `csiphy3+0xE00` all read 0 and drop writes).  So the 18
`0xE00` records in the vendor table are not addressable at `csiphyN + 0xE00` on this
SoC - the table is shared across platforms and that group does not apply here.

Within a lane block, blocks 0x800 and 0xC00 accept the 0x90/0x94/0x98 group while
0x000 and 0x400 drop it, i.e. per-lane analogue registers are gated by the lane
power state, which is exactly the dynamic part the vendor driver writes at
stream-on and which is not in the table.

Conclusion for the next round: the static register tables are now exhausted.  The
missing piece is the **dynamic programming sequence** (lane power, settle, CSR
start) in the vendor's `cam_csiphy_*`/`cam_ife_csid_*` functions, which has to come
from disassembling `camera.ko` (it keeps its symbols: `cam_ife_csid_hw_ver1_hw_cfg`,
`cam_ife_csid_hw_ver2_init`, `cam_csiphy_*`).

## New lead: the CSIPHY "common" registers are at 0x1018/0x101c/0x1084

Disassembling the vendor `camera.ko` (it keeps its symbols) turned up the per-version
**common register list** (`csiphy_common_reg_2_1_1`, 3 records of
`{offset, value, mask, delay}`):

```
0x1084 = 0x00
0x1018 = 0x01
0x101c = 0x7a
```

and the code path that consumes it is `cam_csiphy_config_dev` -> `cam_io_w_mb()`
(the same helper the 103-record lane table goes through), with
`cam_csiphy_update_lane_selection()` doing a read-modify-write of a single lane mask
register.

Two consequences:

1. **the CSIPHY register window must be larger than the 0x1000 our DT gives it** -
   0x1018/0x101c/0x1084 are past the end of every `csiphyN` window, which is
   consistent with every 0xE00/0xE28/0xE90 read returning 0: the block is simply not
   mapped there;
2. those offsets look like they belong to a **"csiphy top" block at 0xac6a000**
   (the address our camss node uses only as a name and never maps).  A probe of
   `0xac6a000 + {0x10,0x18,0x1c,0x84}` could not be completed this round (the module
   produced no output), so this is the first thing to retry.

Relevant vendor symbols for the next round of RE:

```
cam_csiphy_config_dev             0x176af4   applies the lane table via cam_io_w_mb
cam_csiphy_update_lane_selection  0x17a5bc   RMW of the lane mask register
cam_csiphy_program_common_registers 0x178308  the common (0x10xx) records
cam_csiphy_get_settle_count       0x177b6c
cam_csiphy_release_from_reset_state 0x175144
cam_ife_csid_hw_ver1_hw_cfg       0x86cac
cam_ife_csid_hw_ver2_init         0x95b94
```

## 0xac6a000 is not a register block; probing neighbours wedges the phone

With the pipeline verifiably powered (VFE0 IRQ count 11 and climbing), a probe of
0xac6a000 read 0 and dropped writes at 0x000/0x010/0x018/0x01c/0x084, so that
address is not the CSIPHY top block either.

Extending the probe to the neighbouring candidates 0xac69000 and 0xac6b000
**hard-crashed the phone**: `ping` stops answering, SSH reports "no route to host",
the USB gadget still enumerates (stable device number) but the network interface
never returns.  Reading a clock-gated/unimplemented Qualcomm block raises an
external abort; this is the second time an out-of-range camera probe has taken the
machine down.

Safety rule for further probing: only ever touch addresses listed in the vendor DT
(`csiphy0..3` 0xac65000/66000/67000/68000, `csid0..3`, `vfe0/1`, `vfe_lite0/1`,
`cci`, `cpas` 0xac40000, `camnoc` 0xac42000) and only while titan_top is powered.
Unknown addresses must be treated as fatal.

Where the vendor `csiphy_common_reg_2_1_1` offsets (0x1018/0x101c/0x1084) point is
therefore still unknown.  Next attempt should avoid blind address sweeps: instead
disassemble `cam_csiphy_program_common_registers` (0x178308) and follow which base
pointer it adds the offsets to, since the driver holds the answer.

## Host-side RE while the phone is wedged

The phone still needs a power cycle, so this round was disassembly only.

`cam_csiphy_register_baseaddress` (0x17b554) simply stores `csiphy->base`
(`[x0,#1168]`) into a global per-instance array with a 0x78-byte stride; it does not
map anything itself, so the mapping comes from the DT `reg` (`0xac65000 + n*0x1000`,
size 0x1000) through `cam_soc_util_*`.

`__csiphy_prgm_common_data` (0x17c1f4) takes `(instance, table, count, ...)`, fetches
the same per-instance base pointer and walks 16-byte `{offset, value, mask, delay}`
records - i.e. the common-register table is applied to `csiphy->base` too.

That makes the `csiphy_common_reg_2_1_1` offsets (0x1018/0x101c/0x1084) impossible to
reach inside a 0x1000 window, so either:
* those records simply do not apply to SM8150 (the table is shared across SoC
  generations), or
* the vendor maps a larger window than its DT declares and the extra 0x18/0x1c/0x84
  after 0x1000 belong to a companion block that is not described in DT at all.

Blind probing already cost two wedges, so the next attempt must not sweep addresses.
The practical way forward is to give the CSIPHY a **0x2000 window in our own DTB**
(our driver maps what we tell it; nothing else claims 0xac67000..0xac68fff because
mainline camss uses 0x1000 strides) and re-apply the common records there - a single
targeted change instead of guessing.
