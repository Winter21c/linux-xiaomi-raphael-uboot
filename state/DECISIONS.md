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
