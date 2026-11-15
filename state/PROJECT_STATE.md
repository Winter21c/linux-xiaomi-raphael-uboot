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


## Round 33: full VFE0 is alive, only the reset-ack bit is misread

Using csiphy2 -> csid0 -> vfe0_rdi0 instead of the lite VFE2 changes everything:
VFE0 accepts register writes (cmd/mask read back), `hw_version=0x10020000`, and
`/proc/interrupts` for SPI 497 **increments**, so the VFE works and only the
reset-ack identification is wrong (SM8250 expects BIT(0) full / BIT(17) lite).

`camss-vfe-480.c` now logs the real status and treats a pending reset as
acknowledged; the pipeline then reaches CSID0 (reset also a warning) and
`STREAMON` fails with EPIPE, which is the next layer (subdev s_stream start).

The lite VFE2/VFE3 remains inert (writes lost) - avoid it; route every sensor
through csid0/csid1 into vfe0/vfe1.


## Round 34: STREAMON succeeds, waiting for the first CSI-2 data

Two bugs fixed:
* `tools/mcam.c` did not know `pgAA`, so the video node silently kept UYVY and
  `STREAMON` failed with EPIPE at camss-video's format check;
* `vfe_ops_480` writes BUS registers at 0xaa00 + 23*0x100 = 0xC30C, outside the
  0x4000 SM8150 VFE window -> data abort in `vfe_wm_start`.  SM8150's own DT calls
  the block `qcom,vfe175`, i.e. the 1.7x family -> use `vfe_ops_170`.

Now: format sticks (pgAA), `STREAMON` succeeds, the tool waits for a buffer, but no
frame arrives and the CSID0 IRQ count is 0 - the sensor is not emitting CSI-2 data
yet.  Next: verify the OV8856 stream-on (mode write over CCI, MCLK, csiphy start).


## Round 46: device wedged, host-side preparation complete

The phone has been unreachable since round 43 (same USB device number, `ping`
fails, SSH has no route) after an MMIO probe of 0xac69000/0xac6b000 - addresses
outside the vendor DT list.  It needs a physical power cycle; every remaining step
needs the device.

What is ready the moment it comes back:

* `tools/lane-sweep.sh` + the new `camss_lane_override` module parameter
  `(clk<<8)|(d1<<4)|d0`, which tries every plausible CSI-2 lane permutation at
  stream time **without** a DTB rebuild or reboot and reports the csid0/csiphy2
  interrupt deltas and captured bytes per variant;
* the vendor register tables (`vendor-re/vendor-register-tables.md`) and the
  patches that make the CSID use the 175_200 map and the CSIPHY replay the DTR
  v1.1 sequence;
* address-conflict analysis showing only csiphy3 could grow to 0x2000, so the
  vendor `csiphy_common_reg_2_1_1` offsets (0x1018/0x101c/0x1084) most likely do
  not apply to SM8150 at all.

Next action after recovery: run `tools/lane-sweep.sh`; if any permutation lights up
csid0, pin that mapping in the DTB.


## Round 64-66: the v1.1 CSIPHY is verified on hardware

The phone now runs the user's Ubuntu install (ssh `user`/`1234`, sudo `1234`,
USB NCM at 172.16.42.1).  Full detail in `analysis/device-bringup-round64.md` and
`analysis/round66-probe-crash-and-recovery.md`.

Working, with device evidence:

* `qcom-camss` builds against the installed headers (clang 19 + a flag filter) and
  probes: `/dev/media0`, 16 video nodes, 37 entities;
* OV8856 binds at `3-0036` (the DTB clock had to go back to 19.2 MHz);
* sensor -> `msm_csiphy2` link is ENABLED once the unresponsive IMX586 async
  connection is removed from the graph;
* the whole pipeline is accepted by the kernel and reaches VFE `s_stream`; the
  `-EPIPE` was a media bus format mismatch (sensor outputs SGRBG10_1X10);
* **the v1.1 CSIPHY driver programs real hardware**: after the tables are replayed
  the readback is `0x000=0x90 0x700=0x80 0x82c=0xff 0x800=0x00`, exactly the vendor
  values, which confirms the 0x800 control block, the four lane blocks, the lane
  tables and the reset semantics.

Blocked: every CSID register reads back zero (`rx_cfg0` written 0x00200211, read 0)
while the CSIPHY two blocks away answers normally.  `ife_0_gdsc` reports on and
`cam_cc_ife_0_clk_src` is 400 MHz, so the clock tree looks right.  This is the next
thing to attack and it is independent of the CSIPHY work.

Do not raw-`ioremap()` camera blocks: it hangs the bus and reboots the phone.
`/dev/mem` reads of these windows always return zero and prove nothing.
An `-EPIPE` from the pipeline start means a **format** mismatch; a missing link
gives `-ENOLINK`.

## Round 67: the CSID was mapped at the wrong address (fixed)

Full write-up in `analysis/round67-csid-base-address-fix.md`.  `camss-csid.c` picks
the register base from the CAMSS version; the SM8150 resource table carries
`.version = CAMSS_8250`, so the driver took the titan-480 alias
`vfe_base + VFE_480_CSID_OFFSET(0x1200)` = **0xacb0200** instead of the real
**0xacb3000** the vendor DT assigns.  The fix only takes the alias when the device
tree has no region for that CSID.

After the fix, on hardware:

```
RAPHAEL RES csid0 csid0 start=0xacb3000 size=0x1000
csid0 hw_version reg(0x0)=0x10000000
csid0 rx_cfg0: wrote=0x00200211 readback=0x00200211
csiphy2 v1.1 readback: 0x000=00000090 0x700=00000080 0x82c=000000ff 0x800=00000000
```

The CSID answers and its RX configuration sticks.  Remaining gap: no CSI-2 packet
reaches it - the OV8856 is **active**, `msm_csiphy2` interrupts fire, `msm_csid0`
gets none and no bytes are captured.  Next: log the PHY interrupt status
(0x82c..0x854) to separate "clock detected" from a lane error, then check the lane
order/polarity and `CSID_RDI_CFG0`.

Operational: `rmmod qcom-camss` segfaults after any stream attempt, so a rebuilt
module needs a reboot before it can be tested.  Batch experiments per boot.

## Round 68: everything is configured, the data path is still empty

`analysis/round68-all-configured-no-data.md` has the measurements.  Every layer now
verifies positively: CSID base 0xacb3000 with `hw_version=0x10000000` and `rx_cfg0`
sticking, CSIPHY tables replayed with **no error bits** in 0x820..0x828, clock lane
block `700=80 704=0c 708=04 70c=a5`, sensor MCLK at 19.2 MHz, all three sensor rails
enabled while streaming, and the sensor driver picking `mode 1640x1232 vts=1514` and
writing `0x0100=0x01`.

Still: `msm_csid0` gets zero interrupts and zero bytes, and no CSI-2 clock is
detected.  Two dead ends recorded: `camss_lane_override` only exists in the **1-0**
driver (the round-67 lane sweep was a no-op), and writing `cgc_ovd` (0x04) does not
make `core_cfg`/`core_ctrl` stick.

Next: add a lane override to `camss-csiphy-2ph-1-1.c` (with the CSID `lane_assign`
kept consistent), read the sensor's MIPI/PLL registers back over I2C, or compare
`CSID_CORE_CFG` handling against the vendor `cam_ife_csid_175_200`.

## Rounds 74-76: the device tree is now complete and the motor works

`analysis/round74-popup-motor-works.md`, `round75-all-vdig-and-front-probe.md` and
`round76-front-camera-mclk.md` have the measurements.

**The pop-up camera motor works.**  Three defects were found and fixed: the motor node
was missing; its `pwms` cell count was one too long; and the pm8150l LPG channel the
hardware uses (1) was already claimed by an LED child.  A leftover `motor_gpios:
gpio-leds` node also held the same pins, keeping the driver in deferred probe.  After
the fixes:

```
raphael-popup-motor soc@0:popup-motor: popup motor ready (52083 ns cruise, 720 ms)
echo 1 > .../position -> "moved to extended"; echo 0 -> "moved to retracted"
```

The camera physically extended and retracted.

**The device tree now matches the vendor wiring completely:**

* both CCI controllers are in the `cci@ac4a000` node (`reg-names = "cci0","cci1"`);
* all four per-camera vdig enables exist (GPIO 90 wide, 93 tele, 95 front, 34 ultra);
* all four camera MCLK pads are muxed - gpio15 (front MCLK2) and gpio16 (ultrawide
  MCLK3) were **never muxed** until round 76, which alone would have prevented those
  modules from ever receiving a clock;
* the motor node references LPG channel 1.

The deployed DTS is synced back to
`raphael-support/camera/dtb/raphael-camera-final.dts` (160 493 bytes).

**New bring-up tool:** `raphael-support/camera/tools/cam-mclk-hold.c` holds any CAMCC
clock enabled by id so a camera bus can be probed before a driver exists
(`insmod cam-mclk-hold.ko clk_id=86` -> MCLK2 at 19.2 MHz).  Note that
`of_clk_get(node, index)` parses the node's own `clocks` property; a provider lookup
needs `of_clk_get_from_provider()` with an `of_phandle_args`.

**Front camera:** vendor says CAMF is csiphy1 / CCI0 master0 (`/dev/i2c-2`), MCLK2,
reset GPIO 12, vdig GPIO 95.  With the clock held and the pins muxed it still does
not acknowledge, and neither does the wide IMX586 on the same bus; no front-camera
driver is even built in this kernel (only ov5640 and imx412 under `media/i2c`).
