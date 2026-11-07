# Recovering a wedged raphael install

## Symptom

The phone answers ping and TCP port 22 accepts connections, but no SSH command
returns and the display is frozen.  This is a kernel-side deadlock, not a
network problem.

## Cause (seen during camera bring-up)

`qcom-camss` was installed under `/lib/modules/<ver>/extra/raphael/`.  udev
autoloads it at boot through the `qcom,sm8150-camss` modalias; the probe wedged
inside a clock/PHY wait while holding the CLK framework lock, which freezes every
userspace process that touches a clock (logind, PAM, systemd).  The module can
then no longer be removed from userspace.

## Recovery: boot a rescue image with fastboot

```
# 1. force power off (hold Power ~15 s), then hold Volume Down + Power
fastboot devices
# 2. build (needs rootfs.img with the nested GPT) and boot it
./build-rescue.sh /path/to/rootfs.img /tmp/rescue-boot.img
fastboot boot /tmp/rescue-boot.img
```

The image is the phone's own kernel plus its initramfs with one extra
`scripts/local-top/zz-raphael-rescue` hook, which

1. reuses the initramfs' `raphael-subpartition` result (loop devices for the
   nested GPT inside userdata),
2. mounts the root filesystem read-write,
3. restores `sm8150-xiaomi-raphael.dtb` from the `.bak` written when the camera
   DTB was installed,
4. deletes every `/lib/modules/*/extra/raphael/qcom-camss.ko`,
5. `sync`s and reboots.

## Preventing it

* never install `qcom-camss.ko` under `/lib/modules`; keep it in `/root` and
  load it with `insmod` (`test-camera.sh` does this),
* `install.sh` does not ship the camera module for this reason,
* `test-camera.sh` logs to `/root/camera-test.log` so a wedged run still leaves
  the last probe messages behind.
