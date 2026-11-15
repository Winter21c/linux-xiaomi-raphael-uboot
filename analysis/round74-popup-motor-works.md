# Round 74: the pop-up camera motor works on the phone

This round finished deliverables (3) and (4) and produced the first physical
actuation this project has achieved.

## What was found

The device tree already carried most of the motor support:

* `pwm { compatible = "qcom,pm8150l-lpg"; #pwm-cells = <2>; ... }` under `pmic@5`,
  with two children labelled `popup-camera-left` / `popup-camera-right`;
* `camera_vdig_tele` driving GPIO 93 (verified high during streaming).

Runtime state before the fix:

```
/sys/class/pwm/pwmchip0  npwm=5  DRIVER=qcom-spmi-lpg  c440000.spmi:pmic@5:pwm
```

so the LPG PWM provider was alive with five channels all along.

## Three defects, all fixed

1. **No motor node.**  Added `popup-motor` next to the tele regulator, with
   `pwms = <0x7f0 0x01 0x0f4240>` and the vendor GPIOs
   (mode0 21, mode1 33, sleep 35, dir 9, pwren 130), plus `phandle = <0x7f0>` on the
   LPG node so it can be referenced.
2. **Wrong PWM cell count.**  `#pwm-cells = <2>` means `<channel period>`, so the
   first attempt had one cell too many and `devm_pwm_get()` failed with
   `-EBUSY: no pwm`.
3. **LPG channel 1 was taken.**  The LPG LED children claimed channels 1 and 2
   (`export` of 0/3/4 worked, 1/2 failed), so the motor could never own the channel
   the hardware wires it to.  Both LED children are now `status = "disabled"`.

On top of that the device sat in the deferred-probe list with
`raphael-popup-motor: gpios`, because a leftover `motor_gpios: gpio-leds` node from
the original pin-level bring-up still claimed GPIOs 130/35/9/21/33/11/90.  That node
is now disabled too.

## Result, measured on the handset

```
raphael-popup-motor soc@0:popup-motor: popup motor ready (52083 ns cruise, 720 ms)

echo 1 > /sys/bus/platform/devices/soc@0:popup-motor/position
  -> position=extended
  -> raphael-popup-motor soc@0:popup-motor: moved to extended

echo 0 > /sys/bus/platform/devices/soc@0:popup-motor/position
  -> position=retracted
  -> raphael-popup-motor soc@0:popup-motor: moved to retracted
```

**The front camera physically extended and retracted**, driven by our driver through
the pm8150l LPG channel 1 and the DRV8846.  The handset was left retracted.

## Device tree state

`/boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb` (116 455 bytes, built from
`/root/dtbwork/cam.dts` with dtc 1.7.2 on the phone) boots cleanly; the previous
device tree is kept as `sm8150-xiaomi-raphael.dtb.bak-1794725008`.

Diff against the previously deployed tree:

* `+ /soc@0/popup-motor` (compatible `xiaomi,raphael-popup-motor`);
* `+ phandle = <0x7f0>` on the pm8150l LPG node;
* `status = "disabled"` on `led@1` / `led@2` of the LPG and on `motor_gpios`.

## Note on tooling

`dtc` is not on this system; it was fetched with `apt-get download
device-tree-compiler` and unpacked into `/root/dtc` (the phone's apt state is
damaged, so `dpkg-deb -x` is used rather than installing).  `libfdt.so.1` comes with
it, so `fdtget`/`fdtdump` need `LD_LIBRARY_PATH=/root/dtc/usr/lib`.

## Still open

The camera itself: everything on the sensor and CSI receiver side is verified
correct (see `reports/FINAL_COVERAGE.md`), yet no CSI-2 data crosses the D-PHY.
That now looks physical rather than software.
