# The CSIPHY global register block lives at `base + 0x1000`

## How it was found

The vendor `camera.ko` (HyperOS, SM8150) keeps a complete per-version descriptor.
Resolving the relocations of that descriptor for the v2.1.1 PHY gives the full set
of register tables the vendor driver uses:

```
0x783f0: csiphy_v2_1_1              (hw_info: lane stride 0x400, offsets 0x1028/0x10b0/0x1058)
0x783f8: csiphy_common_reg_2_1_1    (0x1084 = 0, 0x1018 = 1, 0x101c = 0x7a)
0x78400: csiphy_irq_reg_2_1_1       (0x102c..0x1040 = 0xff/0xff/0xfb/0xff/0x7f/0xff)
0x78408: csiphy_reset_enter_reg_2_1_1 (0x1000 = 0x1, mask 0x1)
0x78410: csiphy_reset_exit_reg_2_1_1  (0x1000 = 0x2, 0x0, 0xe mask 0xbe8)
0x78418: csiphy_lane_en_reg_2_1_1     (0x1014 = lane mask)
0x78428: csiphy_2ph_v2_1_1_reg        (the 103-record lane init already ported)
0x78458: data_rate_delta_table_2_1_1
0x783b0: status_regs_2_1_1            (0x340/0xc0/0x358/0x758/0xb58/0x140018)
0x768f0.. datarate_211_{1p2,1p5,1p7,2p1,2p35,2p6,2p8,3p3,3p5,4,4p5,5}Gsps
```

Every one of those offsets is **0x1000 or above**, i.e. outside the 0x1000 window
mainline maps from the device tree.  That is why mainline never enabled the PHY:
the reset/IRQ/common/lane-enable and the PLL programming all live in a second page.

## Measured proof

With a stream running (so the PHY is powered) and only the DT window mapped, the
second page is live and writable:

```
glob: lane blk[0x000]=0x0000008e          (lane registers, first page)
glob: global[0x014] before=0x00000000
glob: global[0x014] after write=0x00000003   <- write sticks
glob: global[0x000]=0x00000000            (not a lane block: it is the global page)
```

## What was implemented

`camss-csiphy-2ph-1-0.c` now ioremaps `base + 0x1000` (the physical address is
stashed in `csiphy->base_global_phys` by `camss-csiphy.c`) and, at lane-enable time,
runs the vendor sequence:

1. reset enter (0x1000 = 0x1), reset exit (0x2 -> 0x0 -> 0xe with the vendor delays),
2. common registers (0x1084/0x1018/0x101c),
3. IRQ enables (0x102c..0x1040),
4. **per-data-rate PLL table** (12 tables extracted verbatim into `rate_tables.h`,
   264 records; selected with `camss_phy_rate`),
5. lane enable (0x1014 <- `camss_phy_lane_mask`).

Driver log confirms it runs:

```
csiphy gbl before: 0x1000=0x00000000 0x1014=0x00000000 0x1018=0x00000000
csiphy gbl after:  0x1000=0x0000000e 0x1014=0x00000003 0x1018=0x00000001
```

## Status

The block is now programmed but the CSID still receives nothing (`bytes=0`,
`msm_csid0=0`).  Remaining unknowns, in order:

1. the correct `lane_en` mask semantics (swept 0x1/0x3/0x7/0xf/0x1f/0x3f - none worked);
2. which of the 12 PLL tables matches 1640x1232 at 360 Mbit/s/lane (0.72 Gsps is
   below the smallest table, 1.2 Gsps, so the vendor's rate for this mode may be
   higher than the DT link-frequency suggests);
3. the three `csiphy_v2_1_1` hw_info offsets (0x1028/0x10b0/0x1058) which are not in
   any of the tables above and are probably the "PHY start / PLL lock" controls.

This is the first time the vendor's complete PHY register set has been identified for
this SoC, so the remaining work is now concrete instead of speculative.

## Round 54: the two register spaces are different, and that difference crashes

The vendor tables target **two different base addresses**, and mixing them up takes
the kernel down:

* `csiphy_reset_*`, `csiphy_common_reg`, `csiphy_irq_reg`, `csiphy_lane_en_reg` are
  addressed from the **global page**, i.e. `phy_base + 0x1000`;
* `csiphy_2ph_*_reg` and the twelve `datarate_211_*` tables are addressed from the
  **lane page**, i.e. `phy_base` (their offsets are 0x000..0x7ff, e.g. 0x274,
  0x674, 0x214 - block 0x400 is data lane 1).

Applying the data-rate table to the global pointer made the kernel oops inside
`call_s_stream` (the offsets 0x6xx land in an unpowered page).  The driver now
writes the rate records through `csiphy->base` and the control records through the
global mapping.

Remaining verification is mechanical: with the split in place the sequence is

```
global: reset enter/exit, common, IRQ, lane enable
lane:   vendor 103-record init, then the per-rate PLL table
```

and the open questions are the same three: the correct `lane_en` mask, which of the
twelve PLL tables matches 1640x1232, and the three unused `hw_info` offsets
(0x1028 / 0x10b0 / 0x1058).
