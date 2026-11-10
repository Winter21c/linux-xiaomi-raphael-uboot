# Camera bring-up for the Redmi K20 Pro (raphael / SM8150) on mainline Linux

Branch: `raphael-peripherals` of `Winter21c/linux-xiaomi-raphael-uboot`
Kernel: `raphael-v7.3.0` (`7.3.0-rc5-sm8150-gc033da1fc4fe`)

## Honest status

**No camera produces a frame yet.**  Everything up to the CSI-2 receiver is
verified working; the CSIPHY never delivers a packet to the CSID, and upstream
camss has no SM8150 support at all (`grep -c sm8150 camss.c` = 0), so there is no
reference configuration to copy.

What is verified on the device, with the evidence recorded in `analysis/`:

| layer | state | evidence |
|---|---|---|
| CCI (i2c-2, i2c-3) | working | both masters register; OV8856 binds at `3-0036` |
| OV8856 driver | working | chip id read, rails and MCLK 19.2 MHz enabled, `s_stream(1)` returns 0 |
| sensor transmitting | verified | reads back `0x0100=0x01` (streaming) and `0x4800=0x44` (MIPI output enabled) |
| CSID0 | configured and live | `rx_cfg0=0x00032103` (2 lanes, assign 0x21, PHY 2), `rx_cfg1=1`, reset completes |
| VFE0 | fully working | `hw_version=0x10020000`, writes stick, SPI 497 fires |
| CSIPHY2 | programmed | vendor 103-record D-PHY v1.1 sequence lands in the lane blocks |
| **CSI-2 data** | **never arrives** | `msm_csid0` stays at 0 packets even with the RX interrupt mask forced on |

## What was fixed along the way

1. **qcom-camss ported to SM8150** (`0011`): 4 CSIPHY + 4 CSID + VFE0/1 +
   VFE_lite0/1 register sets, CAMCC clocks, GDSCs, interrupt map.  Registers
   `/dev/media0`, 16 video nodes and 23 subdevs.
2. **VFE ops**: SM8150 is the `1.7x` hardware (`qcom,vfe175`), so `vfe_ops_170` is
   correct.  `vfe_ops_480` writes the bus registers at 0xaa00 + 23 which lands
   outside the 0x4000 window and oopses.
3. **CSID register map** (`0012`): the vendor `cam_ife_csid_175_200_cmn_reg_info`
   table (extracted from the HyperOS `camera.ko`) puts `cgc_ovd` at 0x04,
   `core_cfg` 0x08, `core_ctrl` 0x0c, `rst_cmd` 0x10 and `rst_strobes` 0x14.
   Upstream gen2 has no cgc_ovd/core_cfg/core_ctrl, never triggers a reset, and
   writes the strobes into 0x10 which is `rst_cmd` here.
4. **CSIPHY map** (`0013`): SM8150 uses a **0x400 per-lane stride** and a 0xE00
   global block, not upstream's 0x40.  The vendor's 103-record D-PHY v1.1
   initialisation sequence is replayed verbatim.
5. **DTB**: node added by patching the shipped DTB (`raphael-support/camera/dtb/`),
   never by rebuilding from source - the source tree has the wrong panel
   compatible (`samsung,ea8076` instead of `samsung,ams639rq08`) and rebuilding
   loses the display.

## Bring-up knobs (module parameters)

Added for the remaining investigation, all safe to leave at their defaults:

```
camss_lane_override   (clk<<8)|(d1<<4)|d0   override the DT CSI-2 lane map
camss_vendor_seq_only 0/1                   skip the SM8250 per-lane CFG2 writes
camss_csid_rst_cmd    value                 RST_CMD value, -1 skips the reset
camss_seq_lane14      value                 per-lane settle register override
```

## What is still missing

The vendor driver performs a dynamic PHY enable (lane power / PLL) that is not in
any register table we could extract.  `camera.ko` keeps its symbols, so the
remaining route is to reverse `cam_csiphy_config_dev` / 
`cam_csiphy_update_lane_selection` completely and port that sequence.

Already eliminated, each with measurements in `analysis/`:

* sensor not streaming, wrong rails or wrong MCLK,
* CSID/CSIPHY register *maps* (both replaced),
* CSID stuck in reset, CSID output-path mux (only exists on SM8550+),
* every plausible CSI-2 lane permutation (6 swept at runtime),
* every plausible settle value (8 swept),
* CAMCC block resets (all BCRs read 0 = released) and clocks (all 400 MHz, on),
* the CSID test-pattern generator (produces no interrupt either).

## Contents

```
patches/          clean diffs against raphael-v7.3.0 (0001..0010 DTS, 0011..0015 driver)
vendor-re/        register tables extracted from the HyperOS camera.ko
drivers/          imx586.c / s5k3t2 / s5k3l6 sources for the other three cameras
dtb/              the working DTB blob and its patch recipe
tools/            camera_peek, lane-sweep, the mcu control tool and helpers
```

## Safety notes for anyone continuing

* never rebuild the DTB from the kernel sources - patch the shipped blob;
* never ioremap a camera block unless it is powered: reading a gated Qualcomm
  block raises an external abort and takes the phone down (it happened twice,
  once from `0xac40000` and once from `cpas`/`camnoc` neighbours);
* the camera modules need `modprobe i2c-qcom-cci` before `modprobe qcom-camss`.

## Final state (round 51)

The patch series is regenerated against `raphael-v7.3.0` from the current sources
(`patches/current/0011..0015`), and the device tree currently flashed is
`dtb/raphael-camera-final.dtb` (built from `dtb/raphael-camera-final.dts`):

* camera nodes for the tele (OV8856, working up to `STREAMON`) and a probe node for
  the wide (IMX586 at 0x1a, which never acknowledges),
* the five motor outputs plus hall reset and the front/wide LDO enables exposed as
  `/sys/class/leds/motor:*`,
* display and modem re-verified after every DTB change (`drm=5`, `modem=running`).

### Verified working on the device right now

| peripheral | state |
|---|---|
| display (DRM) | 5 connectors, panel `samsung,ams639rq08` |
| modem / WiFi / BT | `modem=running` |
| audio, touch, NFC, haptics | untouched, working |
| CCI + OV8856 + VFE0 | sensor streams, CSID configured, VFE alive |
| front pop-up motor pins | driven and verified at the pin level |
| **camera frames** | **none - CSI-2 never reaches the CSID** |

### The one remaining blocker, stated plainly

The CSIPHY register file accepts the vendor D-PHY v1.1 sequence but the CSID
receives no packets and reports no errors, and the CSID test-pattern generator does
not produce an interrupt either.  Since upstream has no SM8150 camss support, the
missing piece is the vendor driver's *dynamic* PHY enable, which is only available
as compiled code in `vendor_dlkm/camera.ko` (kept its symbols; the relevant
functions are listed in `analysis/camera-vfe-reset-timeout.md`).
