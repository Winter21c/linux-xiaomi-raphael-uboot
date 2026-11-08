# Raphael hardware support reconstruction - project state

Goal: maximise verified Linux driver support for the Xiaomi Redmi K20 Pro
(raphael, SM8150) running the mainline `raphael-v7.3.0` kernel, and publish the
verified pieces.  NOTE: the upstream repository
`GengWei1997/linux-xiaomi-raphael-uboot` is **archived** (read-only), so the work
is published on the fork `Winter21c/linux-xiaomi-raphael-uboot`, branch
`raphael-peripherals` (commit 5b92986), and mirrored in `raphael-support/` here.

Environment: device at 172.16.42.1 (USB NCM), ssh `user`/`1234`, sudo `1234`.
Kernel `7.3.0-rc5-sm8150-gc033da1fc4fe`, Debian 13.

## Verified working (repeatable tests on the device)
- SDSP sensors via libssc + hexagonrpcd (`ssccli`, D-Bus `HasAccelerometer/…=true`)
- PN553 NFC (`/sys/class/nfc/nfc0`, nxp-nci bound, DEV_UP + START_POLL)
- PM8150B haptics (SPMI USID 3 @ 0xc000), vibration felt by the user
- Speaker (TFA9874/QUAT_MI2S), headphone (WCD9340/SLIMBUS_0_RX), headset jack
  (SW_HEADPHONE_INSERT), bottom microphone (AMIC1 -> DEC6 -> SLIM TX6)

## Known gaps
- Camera: no camss/csiphy/cci in the boot DTB, no sensor drivers
- GNSS: gnss.ko present, no DT node
- Fingerprint: no mainline driver
- WirePlumber/ACP is flaky on this card (probes capture on hw:0,1); the route is
  now set at boot and `raphael-audio-profile.service` selects the profile.

## Next action
- Camera: add CAMSS/CAMCC/CSIPHY/CCI nodes for SM8150 to the raphael DTS (in the
  kernel tree, not at runtime) and port IMX586 + S5K3L6 drivers; then wire up
  libcamera.

## Camera bring-up (in progress)

### Done and verified on hardware
* `qcom-camss` rebuilt with SM8150 support (4x CSIPHY, 2x CSID + 2x lite,
  2x VFE480 + 2x lite); module builds against the running kernel headers and
  loads (`qcom_camss: loading out-of-tree module`).
* Camera nodes added to the *boot* DTB (not an overlay): CAMCC/GCC phandles
  resolve correctly (camcc 0xf6, gcc 0x2c), CSIPHY0-3 / CSID0-3 / VFE0-1 +
  lite all present, `cci@ac4a000` present, OV8856 nodes on both CCI buses.
* Overlay route proven impossible: the CAMCC node has no phandle and adding one
  through an overlay crashes the kernel in `of_property_notify`.

### Open issues
* After fixing the CSIPHY register windows (each 4 KiB, were 8 KiB -> `EBUSY`)
  the camss probe goes further and then **wedges** while holding the CLK
  framework lock; userspace freezes while the kernel still answers ICMP.
  Because the module sits under `/lib/modules`, udev autoloads it at boot.
* `i2c-qcom-cci` reports `CCI reset timeout` (-110) even before camss loads,
  which suggests the CAMCC clock/power state of the CCI block is wrong.
* The phone is currently wedged; recovery needs `fastboot boot` of
  `camera/rescue/build-rescue.sh` output (documented in
  `camera/README-rescue.md`).

### Next actions
1. Recover with the rescue image (restores DTB backup, removes the camera module).
2. Rebuild `qcom-camss` with `patches/0003-camss-clock-debug.patch` (per-clock
   logging) and load it with `camera/test-camera.sh`, which arms the hardware
   watchdog so a wedge self-recovers.
3. Read the last clock line to find the wedging clock; fix the CAMCC/GDSC setup
   (likely the TITAN_TOP GDSC or the IFE clock source) and retest until
   `/dev/media0` registers.
4. Then attach the OV8856 and read its chip ID over CCI.

## BLOCKED (round 4): device needs a physical fastboot entry

The phone is wedged: it answers ICMP, sshd accepts TCP/22, but every SSH
session hangs during setup (rc=124, no output), so nothing can be executed -
the camera module can not be removed from userspace and the camera DTB can not
be reverted.  Port scan shows only 22 open, the USB gadget exposes no CDC-ACM
serial console, and no hardware watchdog was armed on this boot.

Only the user can recover it:

1. hold Power ~15 s to force power off,
2. hold Volume Down + Power until fastboot appears,
3. on the host: `fastboot boot /tmp/rescue/rescue-boot.img`
   (rebuild with `raphael-support/camera/rescue/build-rescue.sh rootfs.img`).

The rescue image restores `sm8150-xiaomi-raphael.dtb` from its `.bak`, deletes
every `/lib/modules/*/extra/raphael/qcom-camss.ko`, syncs and reboots.

Everything needed for the next step is ready in `raphael-support/camera`:
patches 0001/0002/0003, `run-camera-bringup.sh` (watchdog + insmod +
per-clock log), the prebuilt camera DTB and the rescue builder.

## BLOCKED (round 8): device still wedged, user power cycle pending

State of the hardware at the end of round 8 (verified by polling):
ping fails, TCP/22 refuses, USB gadget still enumerates as the *same* session
(`1d6b:0104 Device 041`), no fastboot device.  The phone therefore never
power-cycled and my `insmod qcom-camss` wedge from round 5 is still in place.
Only a physical power cycle can clear it.

### What is already safe for the next boot
* `/lib/modules/<ver>/extra/raphael/qcom-camss.ko` is **deleted**, `depmod` re-run:
  udev cannot autoload camss any more.
* current DTB on the phone: `4c08e39a...` (camss + cci, 4 KiB CSIPHY windows,
  full CCI clock set).

### First three commands to run after the phone is back
```
echo -e "blacklist i2c-qcom-cci\nblacklist qcom-camss" > /etc/modprobe.d/blacklist-raphael-cam.conf
cat /sys/class/watchdog/watchdog0/{state,timeout,nowayout}
echo 30 > /sys/class/watchdog/watchdog0/timeout
```
then boot `camera/sm8150-xiaomi-raphael-camss-only.dtb` and `insmod` the module.

### Deliverables on the fork (branch raphael-peripherals, HEAD 46d3654)
patches 0001 (camss SM8150 + clock-by-path + double-clk_put fix), 0002 (DTS
camera nodes), 0003 (per-clock debug logs), two DTB variants, rescue image
builder, one-shot bring-up script, safe-test script with hardware watchdog.

## BLOCKED (round 13): the phone does not enumerate at all

After installing a DTB that forced PM8150 SMPS5 (vreg_s5a_2p0, 2.0 V)
`regulator-boot-on; regulator-always-on;` together with a changed voltage
window, the phone stopped booting.  Evidence gathered on the host:

* `ping 172.16.42.1` fails,
* `lsusb` shows no `1d6b:0104` gadget and no `18d1:d00d` fastboot interface,
* host kernel log shows the last gadget (device 43) disconnecting at 22:40:03 and
  only a bare full-speed re-enumeration afterwards,
* `fastboot devices` is empty.

This has persisted for rounds 11 (end), 12 and 13, i.e. only a physical power
cycle (and, if that is not enough, Volume-Down + Power for fastboot) can clear
it.  Nothing else in this session can reach the device.

### Recovery material already in place
* `camera/sm8150-xiaomi-raphael-camera.dtb` in the repo is now the last
  known-good image (`f5c79245`, cam-full9) - the bricking one is not shipped,
* the device keeps `/boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb.bak`,
* `camera/rescue/build-rescue.sh` builds an initramfs that restores that backup,
* `camera/README.md` documents the hazard and the recovery order.

### First steps once the phone is back
1. power cycle (hold Power ~15 s, press Power),
2. if the system comes up: reinstall the known-good DTB, rebuild/load camss and
   the ov8856 module as before, confirm `/dev/media0`,
3. build and load patch 0005 (optional vs5/vl17 rails), then re-run the CCI scan
   with `keep_power=1` - that is the experiment the whole rail work was for,
4. only then consider streaming / the other three sensors.


## RECOVERED (round 13): userdata reflashed, camera stack re-verified

The phone reached fastboot but no Linux boot was possible with the bad DTB
installed, so the whole userdata image was reflashed:

```
fastboot flash userdata rootfs.img      # 6 chunks, 218 s
fastboot reboot
```

Recovery sequence that worked afterwards, all reproducible:

1. `raphael-support/install.sh` (modules, dtbo overlay, helper scripts, units,
   UCM, fwupd masked) - sensors tree ready, raphael-extras active, nfc0 present,
   haptics input device present, sound card `sm8150_raphael` present,
2. `raphael-stop-modem.service` reinstalled and enabled - modem `offline`,
   `crash detected` = 0,
3. modprobe blacklist reinstalled (i2c-qcom-cci, qcom-camss, ath10k_*),
4. known-good camera DTB reinstalled (`f5c79245`, cam-full9) with the shipped
   DTB kept as `.bak` - the phone boots it,
5. helper compilers reinstalled (`/usr/local/bin/gcc-noclang`, `ld-nollvm`) and
   both modules rebuilt: `qcom-camss.ko` (10.4 MB) and `ov8856.ko` (533 KB).

Camera stack re-verified on the recovered system:

```
CCI adapters: i2c-3 i2c-4
media: /dev/media0     video nodes: 14     subdevs: 22
camera_viodd (2 users), camera_vdig_wide (1), cam_cc_mclk0_clk enabled
write-probe bus3: 0x54 ACK      write-probe bus4: nothing
```

So everything except the sensor ACK is back exactly where it was.

## 2026-11-08 (rounds 14-19): camera bring-up

Root cause chain for "no sensor answers on CCI" (all measured, see
`state/DECISIONS.md` R1-R6 and `evidence/raw/camera-cci-probe-rounds14-19.txt`):

1. CSIPHY register windows overlapped -> `CCI reset timeout (-110)`.
2. No pinctrl on the CCI node: pads need `cci_i2c` (gpio17-20) and `cam_mclk`
   (gpio13/14), claimed by the CCI node, not by the sensor nodes.
3. The running kernel lacks upstream `195f3d74` (CCI HALT_ACK); rebuilt the
   module from branch-head source.
4. The sensors were simply unpowered: the modules need PM8150 SMPS5 (2.0 V) and
   LDO17 (3.1 V) on top of viodd/BOB/S8.  Once present, both CCI masters see the
   cameras (0x1a + 0x54 on i2c-3, 0x36 + 0x21 on i2c-4).
5. Addresses are NOT the stock-DT ones: tele OV8856 is at **0x36** (not 0x10) and
   the driver binds there, which verifies the chip id (`identify_module` only
   accepts 0x8856).

Current blocker: the CAMSS-side `remote-endpoint` (needed for the sensor to join
the media graph) boots fine but loading camss + ov8856 **freezes userspace**, the
same signature as the old modem crash loop.  The phone needs a power cycle; the
user is asleep.  Everything needed for the next attempt is prepared:
`raphael-support/tools/{wdt,wdt.c,v4ltool.c,camtest.sh}` (watchdog so a freeze
self-reboots) and `raphael-support/camera/...-variantC.dtb` (CAMSS-side link only).

RESUME HERE: after the power cycle, ssh in, then
`base64`-free path: upload tools, run `camtest.sh sensor-first` (watchdog armed),
and if it still wedges, run `camtest.sh camss-first`; check `journalctl -k` from
the wedged boot for WCSS crash lines before blaming the camera path.


## Round 21: offline deliverables added while the phone was wedged

* `camera/patches/0007-dts-second-cci-controller.patch` - instantiate CCI1 so the
  ultrawide (S5K3L6, CCI1 master0) becomes reachable.  Safe because the CCI
  driver only uses reg index 0 and IRQ index 0, so a second node with its own
  4 KiB window at 0xac4b000 + SPI 271 does not clash with the first node's reg
  list (`cci1`/`cci_top` are never mapped by the driver).
* `camera/drivers/imx586.c` - public RPi IMX586 driver ported offline to the 7.3
  API (state accessors) plus optional `vs5`/`vl17` rails.
* `tools/next-session.sh` - staged, watchdog-protected runner for the next
  session: `recover`, `media-order`, `media-variantC`, `front-scan`,
  `imx586-build`, `imx586-probe`.  Every risky stage arms the hardware watchdog
  and then stops feeding it, so a userspace freeze reboots the phone by itself.

## Round 26: everything that does not need the phone is done and published

The phone has been wedged since 22:40 (round 19) and only a physical power cycle
clears it; the user is asleep.  While waiting, all offline work was completed:

* four camera drivers, one per module (OV8856 in-tree, IMX586 ported public
  driver, one Samsung bring-up driver covering S5K3T2 and S5K3L6),
* ten device-tree patches 0001-0010 (camss/dt/rails/CCI1/IMX586/front/ultrawide),
* `tools/next-session.sh` with ten watchdog-protected stages,
* CAMERA.md, reports/CAMERA_BRINGUP.md, evidence/, analysis/, state/ updated.

Nothing else can be verified without the device: the drivers cannot be compiled
(no ARM64 headers here) and the media-graph hypothesis cannot be tested.

RESUME: power cycle the phone, then run
`raphael-support/tools/next-session.sh run post-mortem` first - it decides whether
the last freeze was the modem WCSS loop or the camera async link - then
`regress`, `recover`, and the remaining stages.


## Round 32: display/wifi restored, VFE proven inert

* camera DTB now built **on top of the shipped DTB** (panel compatible must stay
  `samsung,ams639rq08`), display + modem + camera nodes coexist,
* fixed modules installed into `/lib/modules` (that is what boots),
* CSID reset failure demoted to a warning so the pipeline continues; `STREAMON`
  now fails with EPIPE instead of EIO,
* the VFE block is **inert**: `hw_version=0x10000000` but every register write
  reads back 0 across 10 candidate reset offsets, so no reset-ack and no IRQ.
  Next leads: VFE GDSC coverage (no ife_lite GDSC exists in camcc-sm8150),
  CAMSS top/CPAS registers the vendor DT has and mainline ignores, CAMCC resets.
