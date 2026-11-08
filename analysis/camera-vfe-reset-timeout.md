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
