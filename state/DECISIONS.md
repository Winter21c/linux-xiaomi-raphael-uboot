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
