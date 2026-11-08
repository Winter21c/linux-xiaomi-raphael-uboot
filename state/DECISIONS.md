# Decisions and findings

1. All device-tree changes are delivered as a **runtime overlay** applied by
   `dtbo-loader`.  The boot DTB in the rootfs image has no `__symbols__`, and a
   broken boot DTB would need a full 8 GiB reflash, so it is never touched.
   Numeric phandles are used (TLMM = 0x55).
2. The hexagonrpcd root must follow the droid-juicer layout
   (`acdb/ dsp/ sensors/{config,sns_reg.conf,registry} socinfo/`), not Android
   absolute paths; `strinsp hexagonrpcd` shows `/sensors/config/`,
   `/sensors/registry/`, `/socinfo/`, `/acdb/`, `/dsp/`.
3. `socinfo/hw_platform` must exist *before* hexagonrpcd attaches, otherwise the
   SensorsPD registry never initialises (symptom: `'registry' sensor timed out`
   in iio-sensor-proxy, `ssccli` silent).
4. The PM8150B haptics block is on the secondary SPMI die: USID 3, address
   0xc000 (see vendor `pm8150b.dtsi`: `pmic@3 / qcom,haptics@c000`).  Writing to
   USID 0x13 hits the charger's USB peripheral instead.
5. `ff_memless` play callbacks run with the input event spinlock held, so the
   driver must defer all regmap/SPMI access to a workqueue.
6. `seccomp_attach_filter()` NULL-derefs when `fwupdmgr` runs on this kernel and
   wedges the system (RCU stalls, dead USB net).  fwupd is masked.
7. The desktop session on this image is the **greeter** (`_greetd`, uid 101);
   `user` was not in the `audio` group, so PipeWire could not open `/dev/snd`.

## 2026-11-08 camera bring-up: what is measured and what was rejected

### R1. The CCI timeouts had two independent causes (both fixed)
- CSIPHY register windows overlapped (0x2000 instead of 0x1000) -> `CCI reset timeout (-110)`.
- No pinctrl at all on the CCI node: pads must be muxed to `cci_i2c` (gpio17-20) and
  CAMCC MCLK pads to `cam_mclk` (gpio13/14).  Ground truth from
  `camera/reference/raphael-pinctrl.dtsi` and the live
  `/sys/kernel/debug/pinctrl/3100000.pinctrl/pinmux-functions`.
- The pads must be claimed by the CCI node, not by the sensor node: a sensor
  `pinctrl-0` asking for gpio13 fails with `pin GPIO_13 already requested`.

### R2. The kernel we run lacks an upstream CCI fix
`195f3d74 i2c: qcom-cci: Fix HALT_ACK reset request being dropped` is in the branch
head but NOT in `c033da1fc4fe` (compare API: 813 ahead / 161 behind).  Rebuilt
`i2c-qcom-cci.ko` from the branch head; both masters enumerate and transfers are
stable.  Keep building this module from branch-head source until the kernel is
rebuilt.

### R3. The sensors were unpowered, not unreachable
Raw write-probes on both masters ACKed only 0x54 (camera EEPROM, VIO-only) until
the PM8150 **SMPS5 (2.0 V)** and **LDO17 (3.1 V)** rails were added.  The stock
camera DT (`camera/reference/raphael-sm8150-camera-sensor.dtsi`) calls them
`cam_v_custom1`/`cam_v_custom2`.  After adding them (as optional `vs5-supply` /
`vl17-supply` consumed by the ov8856 driver, NOT as `regulator-always-on`):

```
/dev/i2c-3: 0x1a ACK, 0x54 ACK
/dev/i2c-4: 0x36 ACK, 0x21 ACK
```

### R4. Rejected: `regulator-always-on` on PMIC rails
Twice bricked the boot (no USB, no fastboot).  SMPS5 is the parent of
`vdd-l7-l12-l14-l15-supply`, so it is not a camera-private rail.  Recovery was a
full `fastboot flash userdata rootfs.img`.  Rule adopted: camera DTBs are built
from `.orig` + patch 0002 + the intended delta only, and the *compiled* DTB is
grepped for `regulator-always-on` before flashing (only the three base-board
regulators are acceptable).

### R5. Sensor identity: addresses from the stock DT are wrong for this board
- wide IMX586: CCI0 master0 **0x1a** (stock DT implies csiphy0/cci0-m0, so the
  mapping matches, only the address differs from the assumed 0x10),
- tele OV8856: CCI0 master1 **0x36** (not 0x10).  With a single-sensor DTB built
  for exactly that, the driver binds and `ov8856_identify_module()` accepts the
  chip id (it only returns success for 0x8856),
- 0x21 on master1 returns the same register data as 0x36 - identity unresolved,
  do not assert it is a second sensor without evidence.

### R6. Open problem: the async link freezes userspace (round 19)
`camss_parse_ports()` needs the **CAMSS** endpoint's `remote-endpoint` to create
the async entry.  A DTB that only adds that (plus a phandle on the sensor
endpoint; verified as a two-property diff of the decompiled trees) boots fine, but
loading camss and then the ov8856 module freezes userspace exactly like the old
modem crash loop (ping + TCP/22 answer, every SSH session hangs in auth).
`camss_subdev_notifier_bound()` itself only stores `csiphy->cfg.csi2` and
`subdev->host_priv`; `complete()` creates the pad link and then calls
`v4l2_device_register_subdev_nodes()`, so the deadlock (if it is one) is either
there or in the sensor probe/runtime-PM path it triggers.  Next experiments, in
order: reverse module load order, CAMSS-side link only (drop the sensor-side
`remote-endpoint`), then instrument the notifier path.

## 2026-11-08 R7. Sources searched for the remaining sensor drivers

Searched (trees + file-level) for `imx586`, `ov8856`, `s5k3l6`, `s5k3t2`:

| source | result |
|---|---|
| `MiCode/Xiaomi_Kernel_OpenSource` branch `raphael-p-oss` (69 949 files) | 0 hits; camera dirs hold only the framework |
| same repo `cepheus-q-oss` (SM8150 sibling) | 0 hits |
| `LineageOS/android_kernel_xiaomi_sm8150` (74 120 files, truncated) | 0 hits; only `ais/cam_sensor_module` framework |
| `Butterfingerss/Kernel_xiaomi_raphael` | 0 hits |
| GitHub repo search `s5k3t2`, `s5k3l6` | 0 repositories |
| public IMX586 V4L2 drivers | **3 found** (youjunl, Thomas-Li-AVSS, amelieko) |

Adopted `youjunl/imx586-v4l2-driver` (GPL-2.0 driver, MIT repo): complete
register tables, chip id 0x30dc == 0x32.  Ported offline to the 7.3 API in
`camera/drivers/imx586.c` (state accessors + optional vs5/vl17 rails).

## 2026-11-08 R8. The ultrawide is not reachable with the current CCI driver

`i2c-qcom-cci.c` (branch head) has `#define NUM_MASTERS 2` and maps
`i2c-bus@<n>` to `cci->master[n]`; our single `cci@ac4a000` node therefore exposes
CCI0 master0/master1 = `/dev/i2c-3` and `/dev/i2c-4`.  The ultrawide (stock DT
`qcom,cam-sensor@3`, `CAMU_*`) is on **CCI1 master0** (i2c@ac4b000), so it cannot
be probed until the driver learns four masters or a second instance is created
(both would need the shared `cci_top` region handled).

## 2026-11-08 R9. Front camera mapping corrected

The stock DT tags `qcom,cam-sensor@2` with `CAMF_RESET`/`CAMF_VDIG_EN` and `@3`
with `CAMU_*`, i.e. **front = CSIPHY1 / CCI0 master0 / MCLK2 gpio15 / reset
gpio12 / VDIG_EN gpio95** and ultrawide = CSIPHY3 / CCI1 master0 / MCLK3 gpio16 /
reset gpio23 / VDIG_EN gpio34.  An earlier note in this file had the two swapped;
this entry supersedes it.


## 2026-11-08 R10. The frozen phone was the modem, not the camera

`journalctl -k -b -1` from a wedged boot shows RCU stalls and
"Sending NMI from CPU 6 to CPUs 3 / After 10 seconds, these CPUS still haven't
responded to the NMI: 3" with **no WCSS lines in the tail** - but the decisive
evidence came the other way round: a *fresh* boot with no camera module loaded
froze again within ~2 minutes, while the same DTB with `&remoteproc_mpss {
status = "disabled"; }` stayed up for 11+ minutes with normal load.

So the modem (WCSS) crash loop comes back whenever the guard service cannot stop
it fast enough, and it hard-locks a CPU.  With mpss disabled in the DT the system
is stable and the whole camera stack (cci + camss + ov8856) loads with
`v4l-subdev22 = ov8856 4-0036` linked to csiphy2 - the async link that was
blamed for the freeze was innocent.

Cost of disabling mpss: no WiFi / BT / cellular until a working WLAN firmware
path exists.  `kernel.panic = 15` is now persisted via
`/etc/sysctl.d/99-raphael-panic.conf`, so a real panic reboots the phone by
itself (it did twice).

## 2026-11-08 R11. Streaming blockers found by actually trying it

1. **32-bit DMA mask**: `camss.c` used `dma_set_mask_and_coherent(dev,
   0xffffffff)`, which pushed every multi-MB frame buffer through the swiotlb
   bounce buffer -> `swiotlb buffer is full (sz: 2097152 bytes)`.  Changed to
   `DMA_BIT_MASK(64)`; buffers now allocate.
2. **Wrong power-domain order**: the driver attaches the block GDSC with
   `dev_pm_domain_attach_by_id(genpd_num - 1)`, i.e. it requires TITAN_TOP to be
   *last*, and attaches the per-IFE domains by name.  The DTB listed
   `"top", "ife0", "ife1"` (top first), so id 2 resolved to ife1 and no IFE GDSC
   was ever enabled.  Fixed the DTB order to `ife0, ife1, top` and gave VFE0/VFE1
   `has_pd = true` + `pd_name = "ife0"/"ife1"`.
3. **Still open**: `VFE reset timeout` on every attempt, and the clock log shows
   the *lite* VFE's clock pair (`ife_lite`, `ife_lite_axi`) being enabled even for
   `/dev/video0` (`msm_vfe0_video0`), i.e. the video node seems bound to a lite
   VFE resource while its name says vfe0.  That mismatch is the next thing to
   chase; the video node format also comes back as UYVY even when the packed Bayer
   format matching the sensor (`0x300a` = MEDIA_BUS_FMT_SGRBG10_1X10 -> `pgAA`) is
   requested.

Tool written for this: `tools/mcam.c` (builds on the device) - links,
link enable, subdev get/fmt, video caps/enumfmt/mfmt/mstream, because the image
has neither media-ctl nor v4l2-ctl.
