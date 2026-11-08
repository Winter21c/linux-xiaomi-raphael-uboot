# Where the camera bring-up stands, and the next concrete steps

## Established by measurement

| item | evidence |
|---|---|
| sensor transmits | `0x0100=0x01`, `0x4800=0x44` read back after stream-on |
| CSID alive | `hw_version=0x10000000`, `cgc_ovd` write reads back 1 |
| CSID map corrected | vendor `cam_ife_csid_175_200_cmn_reg_info` -> cgc_ovd 0x04, core_cfg 0x08, core_ctrl 0x0c, rst_cmd 0x10, rst_strobes 0x14 |
| CSIPHY lane blocks live | vendor 103-record sequence lands; `0x94=0xd7` persists in block 0x800 |
| VFE0 fully alive | `hw_version=0x10020000`, writes stick, SPI 497 fires |
| no CSI-2 traffic | `msm_csid0` (SPI 496) and `msm_csiphy2` (SPI 511) counters stay 0 |

## Ruled out

* sensor not streaming (registers prove it is),
* CSID/CSIPHY register *maps* (both replaced with the vendor tables),
* CSIPHY 0xE00 group (dead in all four PHY windows; the vendor's
  `csiphy_common_reg_2_1_1` offsets 0x1018/0x101c/0x1084 cannot be reached in a
  0x1000 window, and only csiphy3 could even be enlarged without overlapping a
  neighbour - so that table most likely does not apply to SM8150),
* CAMCC block resets (all BCRs read 0 = released),
* clocks (ife_0_csid/cphy_rx/clk all enabled at 400 MHz during a stream),
* CSID test-pattern generator (produces no interrupt).

## Next steps, in order

1. **lane permutation sweep** - `tools/lane-sweep.sh` uses the new
   `camss_lane_override` module parameter `(clk<<8)|(d1<<4)|d0` to try every
   plausible wiring at runtime, no DTB rebuild and no reboot.  This is the last
   cheap hypothesis for "everything reads fine but nothing arrives".
2. if a permutation lights up `msm_csid0`, pin the winning mapping in the DTB.
3. otherwise compare the vendor `cam_csiphy_config_dev` disassembly against our
   programming step by step (it is the only remaining source of truth).

## Device note

The phone wedged after an out-of-range MMIO probe (0xac69000/0xac6b000) and needs a
physical power cycle.  Never touch addresses outside the vendor DT list again.
