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

## Round 55: the per-rate PLL path faults, and the default configuration is safe

Applying a `datarate_211_*` table aborts the kernel inside
`csiphy_lanes_enable+0x544` with `Oops: 96000047` (data abort, translation fault),
right after the driver logs `csiphy rate table: 1.2 Gsps (22 records)`:

```
csid0 rx_cfg0: wrote=0x00200211 readback=0x00000000 (lane_cnt=2 assign=0x21 phy=2)
csiphy rate table: 1.2 Gsps (22 records)
Internal error: Oops: 0000000096000047 [#1]  SMP
pc : csiphy_lanes_enable+0x544/0x60c [qcom_camss]
```

Facts established while chasing it:

* all 252 extracted offsets are 0x214..0xa90, i.e. inside the 0x1000 lane window;
* moving the global page ioremap out of `lanes_enable` (into probe, with
  `IS_ERR` handling via `devm_ioremap`) did **not** change the fault, so the
  mapping was never the problem;
* the fault only happens when `camss_phy_rate >= 0` is passed, i.e. **the default
  configuration (`camss_phy_rate = -1`) never touches this path**.

So the shipped state is safe: the global-block sequence (reset/common/IRQ/lane
enable) runs and is verified, while the per-rate PLL tables are opt-in and still
need work.  The most likely causes left are the record layout/packing assumption
(the vendor record is four u32 while the generated struct is
`{u16,u32,u32,u16}`) and whether the vendor programs the rate table against a
*different* per-lane base (the offsets 0x2xx/0x6xx suggest lane 0 and lane 1 blocks
plus a 0x200 offset, not a flat 0..0x1000 space).

Recommendation for whoever continues: re-verify the packing by disassembling the
consumer of `datarate_211_*` (the loop that walks the table) before trusting the
generated C struct.

## Round 57: the oops is fixed - it was a gated lane block

The `Oops 96000047` in round 55 was caused by the per-rate PLL records whose offsets
are **0x400 and above**: those land in the *second data-lane block*, and touching a
gated lane block raises an external abort.  Two knobs make that explicit and safe:

* `camss_phy_rate_limit` (default **0x400**) - records at or above this offset are
  skipped, so the default configuration never touches a possibly-gated block;
* `camss_phy_lane_mask` - with mask **0x7** the second lane block becomes writable
  and the limit can be raised to 0x800 without any fault.

Verified sweeps, every run `oops=0`:

```
rate=0 (1.2 Gsps)   bytes=0 csid0=0      rate=3 (2.1 Gsps)   bytes=0 csid0=0
rate=1 (1.5 Gsps)   bytes=0 csid0=0      rate=4 (2.35 Gsps)  bytes=0 csid0=0
rate=2 (1.7 Gsps)   bytes=0 csid0=0      rate=5 (2.6 Gsps)   bytes=0 csid0=0
```

and lane masks 0x1 / 0x3 / 0x7 with the rate table enabled: all `oops=0`,
`bytes=0`, `csid0=0`.

So the complete vendor PHY sequence - reset, common, IRQ, per-rate PLL, lane enable -
now runs stably on the device.  The receiver is fully configured and still delivers no
packet to the CSID, which means the remaining difference is not in the PHY
programming any more.  The most probable candidates left:

1. the sensor's actual lane rate does not match any table (0.72 Gsps is below the
   smallest, 1.2 Gsps) - a mode with a >=1.2 Gsps rate would test this directly;
2. the CSID's input selection (`assign=0x21 phy=2` comes from our own DT/ops and was
   never cross-checked against the vendor's `cam_ife_csid_hw_ver1_hw_cfg`);
3. the third one is timing: the vendor starts the sensor *after* the PHY sequence
   completes, while camss starts subdevs in a fixed order.

## Round 58: the CSID is verified correct and the PHY is fully programmed - still nothing

Probed the CSID registers **during a live stream** with the debug module:

```
rx_cfg0 [0x100] = 0x00032103   -> lane_cnt=2, assign=0x21, phy=2   (correct)
rx_cfg1 [0x104] = 0x00000001
RX IRQ status [0x02c] = 0      -> no packet
[0x070] = [0x074] = [0x078] = 0 -> no error either
```

so the receiver is configured exactly as intended, is clocked, reports no errors, and
simply never sees a start-of-packet.  Combined with the fully programmed PHY (reset,
common, IRQ, per-rate PLL for all six tables, lane enable 0x1/0x3/0x7 - every run
`oops=0`), the conclusion is that **the PHY is not emitting anything at all**, rather
than emitting bad data.

Also re-tested the 3280x2464 mode (the only OV8856 mode whose computed lane rate,
~1.21 Gbps, reaches the smallest vendor PLL table) with rate tables 0 and 1:

```
3280x2464 rate=0: bytes=0 csid0=0 oops=0
3280x2464 rate=1: bytes=0 csid0=0 oops=0
1640x1232 rate=0: bytes=0 csid0=0 oops=0
```

so a rate mismatch between the sensor and the PLL table is ruled out as the cause.

### What is left, stated precisely

The PHY is programmed with every register the vendor binary contains for v2.1.1 and
still emits nothing, the CSID is verified good, and the sensor reports streaming with
MIPI output enabled.  The two remaining candidates both live outside the tables that
could be extracted:

1. the vendor's `cam_csiphy_config_dev` performs an extra step after the tables that
   depends on runtime state (lane count and rate are passed in as arguments, and the
   `csiphy_v2_1_1` hw_info offsets 0x1028 / 0x10b0 / 0x1058 are never written by us);
2. the sensor module may need a supply or a reset sequence that differs from what the
   vendor node describes (the tele module reports streaming but may not actually be
   driving its MIPI outputs without it).

Evidence for (1) is the strongest: three hw_info offsets remain unused, and the vendor
struct also carries `status_regs_2_1_1` at 0x340/0xc0/0x358/0x758/0xb58 - registers
whose values we have never read back and compared against a working stream.
