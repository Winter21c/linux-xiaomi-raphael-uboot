# raphael mainline camera bring-up - final status

Date of last measurement: round 52.
Branch: `raphael-peripherals` (`Winter21c/linux-xiaomi-raphael-uboot`).
Base kernel: `GengWei1997/linux` `raphael-v7.3.0`.

## One-line summary

The camera stack is ported to SM8150 and everything up to the CSI-2 receiver is
verified working on the device, but **no camera has ever produced a frame** because
the SM8150 CSIPHY never delivers a packet to the CSID, and that last step needs a
vendor-only initialisation sequence.

## Verified working (measured on the device)

| item | evidence |
|---|---|
| display / DRM | `drm=5`, panel `samsung,ams639rq08`, desktop visible |
| modem, WiFi, Bluetooth | `modem=running`, bluetooth stack loaded |
| audio | `card0` with working PCM/control devices |
| touch, NFC, haptics | goodix_gtx8, nxp_nci_i2c, pm8150b_haptics all loaded |
| qcom-camss on SM8150 | `/dev/media0`, 16 video nodes, 23 subdevs |
| i2c-qcom-cci | both CCI0 masters register as `Qualcomm-CCI` |
| OV8856 (tele) | binds at `3-0036`, chip id read, `s_stream(1)` returns 0 |
| sensor really streaming | reads back `0x0100=0x01`, `0x4800=0x44` |
| CSID0 | configured (`rx_cfg0=0x00032103`), reset completes, no errors reported |
| VFE0 | `hw_version=0x10020000`, writes stick, SPI 497 fires |
| CSIPHY2 | vendor 103-record D-PHY v1.1 sequence applied and holds |
| front pop-up motor pins | 8 gpio exposed as `/sys/class/leds/motor:*`, driven and confirmed at pin level |

## The decisive discovery (rounds 53-55)

The vendor `camera.ko` keeps a complete descriptor per PHY version.  Resolving the
relocations of the v2.1.1 descriptor revealed **the register block mainline never
touched**:

| table | base | offsets |
|---|---|---|
| `csiphy_reset_enter/exit_reg` | phy_base **+ 0x1000** | 0x1000 |
| `csiphy_common_reg` | phy_base + 0x1000 | 0x1084 / 0x1018 / 0x101c |
| `csiphy_irq_reg` | phy_base + 0x1000 | 0x102c .. 0x1040 |
| `csiphy_lane_en_reg` | phy_base + 0x1000 | **0x1014** |
| `csiphy_2ph_v2_1_1_reg` | phy_base (lane page) | 0x000 .. 0x7ff |
| `datarate_211_{1p2..5}Gsps` (12) | phy_base (lane page) | 0x214 .. 0xa90 |

The DT only maps 0x1000, so mainline programmed the lane registers but **never the
reset / IRQ / common / lane-enable block** - that is why the PHY output was never
enabled.  Measured with a live stream, the second page is live and writable
(`global[0x014] <- 0x3` sticks, `global[0x000]` reads 0 so it is not a lane block).

The driver now maps that page at probe (`devm_ioremap`, `IS_ERR` checked) and runs
the vendor sequence at lane-enable time; the log confirms
`0x1000=0x0e 0x1014=0x03 0x1018=0x01`.

Still open, and now concrete instead of speculative:

1. the correct `lane_en` mask semantics (0x1/0x3/0x7/0xf/0x1f/0x3f all tried),
2. which of the twelve PLL tables matches this mode - applying one currently aborts
   the kernel in `csiphy_lanes_enable` (Oops 96000047), so that path is opt-in
   (`camss_phy_rate`, default -1) and does not affect the shipped configuration,
3. the three unused `csiphy_v2_1_1` hw_info offsets 0x1028 / 0x10b0 / 0x1058.

## Not working

* **CSI-2 data**: the CSID receives zero packets and reports zero errors, i.e. the
  PHY output is never enabled.  No frame has been captured from any of the four
  cameras.
* **front camera motor**: the drive sequence is correct at the pin level
  (`en/sleep/dir/m0/m1` follow writes, nFAULT stays high) but the hall input never
  changes, so physical movement is unconfirmed.
* **IMX586 / S5K3L6 / S5K3T2**: drivers exist in the tree, the IMX586 node is in the
  DT, but the chip never acknowledges at 0x10/0x1a/0x20/0x2d.

## Why the CSIPHY is the blocker, and why it is hard

Upstream `camss` has **no SM8150 support at all** (`grep -c sm8150 camss.c` = 0), so
there is no reference configuration.  The only source of truth is the vendor
`camera.ko` extracted from a HyperOS `vendor_dlkm` image, from which the register
tables were recovered:

* `cam_ife_csid_175_200_cmn_reg_info` (CSID map, now applied),
* `csiphy_2ph_v2_1_1_reg` (103-record D-PHY v1.1 init, now applied),
* `csiphy_common_reg_2_1_1` (offsets 0x1018/0x101c/0x1084 - **unreachable**: the DT
  gives every CSIPHY a single 0x1000 window and the vendor DT confirms it).

What is still missing is the vendor's *dynamically programmed* PHY enable (lane
power / PLL).  That code exists only as ARM64 instructions in `camera.ko`; the
relevant symbols are recorded in `analysis/camera-vfe-reset-timeout.md`
(`cam_csiphy_config_dev`, `cam_csiphy_update_lane_selection`,
`cam_csiphy_program_common_registers`, `cam_ife_csid_hw_ver1_hw_cfg`,
`cam_ife_csid_hw_ver2_init`).

## Everything that was eliminated (each with measurements)

sensor not streaming / wrong rails / wrong MCLK; CSID and CSIPHY register maps;
CSID held in reset; CSID output-path mux (SM8550+ only); 12 CSI-2 lane permutations;
8 settle values; 3 CSID reset values; CAMCC block resets; all camera clocks;
CSID test-pattern generator; 3 sensor modes; 4 IMX586 I2C addresses.

## How to reproduce

```
git clone -b raphael-peripherals https://github.com/Winter21c/linux-xiaomi-raphael-uboot
# apply raphael-support/camera/patches/current/0011..0015 to a raphael-v7.3.0 tree
# flash raphael-support/camera/dtb/raphael-camera-final.dtb to
#   /boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb
modprobe i2c-qcom-cci
modprobe qcom-camss
insmod ov8856.ko
```

## Safety rules learned the hard way

1. Never rebuild the DTB from the kernel sources - the tree has the wrong panel
   compatible and the display disappears.  Patch the shipped blob.
2. Never ioremap a camera block unless it is powered: reading a gated Qualcomm block
   raises an external abort and takes the phone down (happened twice).
3. At most one consumer per GPIO - a regulator and a gpio-leds entry on the same pin
   makes the whole LED node fail to probe.
4. The IMX586 driver in this tree accepts only a 24 MHz xclk; the OV8856 expects
   19.2 MHz.

## Final evidence (round 59)

During a live stream the complete vendor PHY register set holds every value we write:

```
lane0 0x000 = 0x8e    lane1 0x400 = 0x8e
gbl   0x1000 = 0x0e   0x1014 = 0x07   0x1018 = 0x01   0x101c = 0x7a
```

and **every status register reads zero**:

```
status 0x340 = 0x358 = 0x758 = 0xb58 = 0
hw_info 0x1028 = 0x1058 = 0x10b0 = 0
```

A CSIPHY receiving even a clock lane reports at least one non-zero status bit, so the
remaining gap is almost certainly on the **sensor side of the link** - the module
reports streaming (`0x0100=0x01`, `0x4800=0x44`) but is not driving its MIPI clock.

The one experiment left unfinished is the `0x4800` clock-lane variant sweep: the knob
was added (`reg_4800` in the OV8856 driver) but the override did not take effect in
the build used, so the values 0x44/0x24/0x04/0x64 are untested.  To redo it, write
0x4800 **after** the mode register list is committed and **before** `0x0100 <- 0x01`,
then re-read the PHY status registers.
