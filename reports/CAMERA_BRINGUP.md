# Camera bring-up on the Redmi K20 Pro (raphael / SM8150), mainline kernel

Status: **sensor communicating and driver-verified; not yet in the V4L2 media
graph, no frames captured.**  Everything below is measured on the device unless
explicitly marked as a hypothesis.

## 1. What the hardware is (evidence-based)

Stock camera device tree (`raphael-sm8150-camera-sensor.dtsi`, kept in
`raphael-support/camera/reference/`) plus the vendor partition still present on
the phone (`/vendor/lib64/camera/com.qti.sensor.*_raphael.so`,
`com.qti.sensormodule.*.bin`) identify four modules:

| module | sensor | csiphy | cci device/master | MCLK pad | reset | vdig enable | extra rails |
|---|---|---|---|---|---|---|---|
| wide | IMX586 | 0 | 0 / 0 | gpio13 | gpio28 | gpio90 | SMPS5 2.0 V + L17 3.1 V |
| tele | OV8856 | 2 | 0 / 1 | gpio14 | gpio30 | gpio93 | L17 3.1 V |
| ultrawide | S5K3L6 | 1 | 0 / 0 | gpio15 | gpio12 | gpio95 | gpio31/32 |
| front | S5K3T2 | 3 | 1 / 0 | gpio16 | gpio23 | gpio34 | - |

Common rails: `cam_vio` = camera_viodd_ldo 1.8 V, `cam_vana` = PM8150L BOB
3.008-4.0 V, `cam_vdig` = PM8150L S8 1.352 V.

Measured I2C addresses (differ from the stock DT's implicit 0x10):

```
CCI0 master 0 (/dev/i2c-3): 0x1a (wide, 0x0004=0x0aff 0x0008=0x0040), 0x54 (EEPROM)
CCI0 master 1 (/dev/i2c-4): 0x36 (tele OV8856), 0x21 (same register data as 0x36 - unresolved)
```

## 2. Why it took this long: four independent blockers

1. **CSIPHY register windows overlapped** (0x2000 instead of 0x1000 each) ->
   `CCI reset timeout (-110)`.
2. **No pinctrl**: the CCI pads must be muxed to `cci_i2c` (gpio17-20) and the
   CAMCC MCLK pads to `cam_mclk` (gpio13/14); the pads must be *claimed by the
   CCI node*, a sensor `pinctrl-0` on the same pad fails with EINVAL.
3. **Missing upstream CCI fix**: the kernel we run (`c033da1fc4fe`) predates
   `195f3d74 "i2c: qcom-cci: Fix HALT_ACK reset request being dropped"`.
   `i2c-qcom-cci.ko` is therefore built from branch-head source.
4. **The modules were unpowered**: they need PM8150 SMPS5 (2.0 V) and LDO17
   (3.1 V) in addition to viodd/BOB/S8.  Adding them as *optional supplies
   consumed by the sensor driver* (`vs5-supply`, `vl17-supply`) is what made the
   sensors answer.  Forcing `regulator-always-on` on the PMIC rails instead
   bricks the boot (SMPS5 is the parent of `vdd-l7-l12-l14-l15-supply`) - it
   happened twice and needed a full `fastboot flash userdata` to recover.

## 3. Driver state

`ov8856` binds at CCI0 master1 / 0x36 with `viodd + BOB + S8 + L17`:

```
/sys/bus/i2c/devices/4-0036/driver -> bus/i2c/drivers/ov8856
ov8856 ... Live
(no probe errors)
```

`ov8856_identify_module()` reads 0x300a as a 24-bit register and returns -ENXIO
unless the value is exactly `0x8856`, so a bound driver is the chip-id proof.

IMX586, S5K3L6 and S5K3T2 have **no mainline driver**; the public Xiaomi kernel
tree that this port is based on does not contain the vendor camera sensor drivers
either (its `camera_v2/sensor` directory has only the framework), so the register
tables needed for a port are not available offline - that is a recorded blocker,
not an assumption.

## 4. Media graph: the current blocker

The sensor must be linked to CSIPHY2 through the V4L2 async notifier, which
requires the **CAMSS endpoint** to carry `remote-endpoint`.  A DTB with exactly
that added (verified as a two-property diff of the decompiled trees) boots, but
loading `qcom-camss` and then `ov8856` **freezes userspace** (ping and TCP/22
answer, every SSH session hangs in authentication) - the same signature as the
old modem crash loop.  `camss_subdev_notifier_bound()` is innocent (it only
stores pointers); `complete()` creates the pad link and calls
`v4l2_device_register_subdev_nodes()`.

Next three experiments (prepared, see `analysis/camera-media-graph-wedge.md`):
reverse module order; CAMSS-side link only (`...-variantC.dtb`); and ruling out
the modem crash loop from the wedged boot's `journalctl -k`.

## 5. Reproduce

```sh
# on the device
insmod /root/ccifix/i2c-qcom-cci.ko          # branch-head CCI (HALT_ACK fix)
insmod /root/camss-final/qcom-camss.ko       # SM8150 camss port (patch 0001)
insmod /root/ovcomb/ov8856.ko                # ov8856 + optional vs5/vl17 rails
# /dev/media0, 14 msm_vfe*_video* nodes, sensor bound at 4-0036
```

Patches: `raphael-support/camera/patches/0001..0005`.  DTBs:
`raphael-support/camera/*.dtb` (see `evidence/derived/camera-dtb-variants.md`).
Tools: `raphael-support/tools/{i2cscan,i2cprobe8,i2cprobe16,probew,gpioset,v4ltool,wdt}`.
