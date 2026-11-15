# Round 68: every block is now configured, the data path is still empty

## What was proven this round

| layer | evidence |
|---|---|
| CSID base address | fixed (see `round67-csid-base-address-fix.md`); `RAPHAEL RES csid0 start=0xacb3000` |
| CSID alive | `hw_version=0x10000000`, `rx_cfg0` write reads back 0x00200211 |
| CSIPHY programmed | `0x000=0x90 0x700=0x80 0x82c=0xff 0x800=0x00`, no error bits |
| PHY status block | `800..81c` config only, `820..828` all zero (**no PHY error**), `82c..83c` = the masks we wrote |
| clock lane block | `700=80 704=0c 708=04 70c=a5 710=52 714=60 718=00 71c=0a`, data lane 0 identical |
| sensor MCLK | `cam_cc_mclk1_clk` enabled at 19.2 MHz, consumer `3-0036 xvclk` |
| sensor rails | `3-0036-avdd`, `3-0036-dovdd` (camera_viodd_ldo 1.8 V), `3-0036-dvdd` (camera_vdig_tele 1352 mV) all enabled while streaming |
| sensor mode | driver logs `mode 1640x1232 vts=1514 link_freq_idx=1` and writes `0x0100=0x01` |

## What is still missing

`msm_csid0` receives nothing: `CSIDPOST rxirq=0x00000000`, zero interrupts, zero
captured bytes, no CSI-2 clock detected.

## Two dead ends worth recording

1. **`camss_lane_override` does not apply to this PHY.**  The parameter lives in
   `camss-csiphy-2ph-1-0.c`; the v1.1 driver has no such knob, so the eight-value
   lane sweep from round 67 changed nothing at all.  A lane override has to be
   added to `camss-csiphy-2ph-1-1.c` (and the CSID's `lane_assign` kept consistent)
   before lane permutations can be tested.
2. **`cgc_ovd` (0x04) does not fix `core_cfg`.**  Writing 0xffffffff to 0x04 before
   `core_cfg` (0x08) leaves both `core_cfg` and `core_ctrl` reading zero while
   `rx_cfg0` (0x100) and the RDI registers (0x300/0x304) hold their values.  Either
   those two registers are write-only on CSID 175, or the cause is elsewhere.

## Next candidates, in order

1. add a lane override to the 1-1 driver and sweep the data-lane permutation with
   the CSID `lane_assign` following it;
2. read the sensor's own MIPI/PLL registers back over I2C (PLL1/PLL2, `0x4800`
   lane enable) to confirm it is really driving the lanes;
3. check whether the CSID needs `CSID_CORE_CFG` through a different offset on
   CSID 175 by comparing against the vendor `cam_ife_csid_175_200` code.
