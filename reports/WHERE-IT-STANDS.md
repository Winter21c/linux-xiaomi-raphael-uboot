# Where this stands, round 80

## Done and verified on the handset

| deliverable | evidence |
|---|---|
| (1) v1.1 register map + full programming sequence | proven from the phone's own vendor Image; tables replayed; readbacks match the vendor values (`0x000=0x90`, `0x700=0x80`, `0x82c=0xff`, error area `0x820..0x828` zero) |
| (2) mainline CSIPHY driver on base+0x800 | builds against the running kernel, loaded, programmed the PHY on device; sources now in `raphael-support/camera/drivers/` with `BUILD.md` |
| (3) DTB matching the vendor wiring | deployed at `/boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb`; both CCI controllers, all four per-camera vdig enables (90/93/95/34), all four camera MCLK pads muxed, motor node on pm8150l LPG channel 1; source synced to `raphael-support/camera/dtb/raphael-camera-final.dts` |
| (4) pop-up motor driver | builds, loads, probes, and **physically extends and retracts the front camera** |
| (5) commit | **not done** - the host cannot execute any command |

## The one thing that does not work

No camera produces a frame.  For the tele module (OV8856 on csiphy2) every software
layer has been measured correct:

* CSID mapped at the real 0xacb3000, answering (`hw_version=0x10000000`, `rx_cfg0`
  write reads back);
* the CSID can be shown to receive - its own test generator produces
  `rxirq=0x004000ff` and VFE interrupts;
* the CSIPHY is programmed byte-for-byte like the vendor, reports no error bits, and
  behaves identically whichever of the four PHYs it is attached to;
* the sensor is powered (avdd/dovdd/dvdd on), clocked (19.2 MHz MCLK1), and configured
  (`0x0100=0x01`, PLL written, `0x4800=0x44`, colour-bar test pattern on).

No byte crosses the D-PHY.  The front module is worse: with its rails, MCLK (held with
`cam-mclk-hold`) and reset all asserted, it does not even acknowledge on I2C - and
neither does the wide IMX586 on the same bus.

That combination is not a software signature.  Two of three camera modules are silent
at the I2C level and the third is silent at the MIPI level while looking healthy in
every register.

## What would move it forward

**The handset (needs physical access):**

1. reseat the tele module's flex cable, then re-run the round-66 pipeline - if it
   streams, the software is proven end-to-end and the fault was the connector;
2. if it does not, the module or the SoC's CSI pins are at fault; a scope on the
   module's MIPI clock/data pads would settle it;
3. the front/wide modules need the same treatment (their I2C is dead before MIPI even
   matters).

**The host (blocks deliverable 5):**

`bash`, `grep`, `glob` and the REA/Ghidra provider all fail with
`Unknown system error -122 ... write`, and a probe confirmed commands never actually
run.  Freeing space in `/tmp` (`rm -rf /tmp/lsrc /tmp/camsssrc /tmp/*.tar.gz`) or
restarting the session should clear it; after that a single
`git add -A && git commit` in the fork completes (5).

## Tools left behind on the phone

Everything needed to continue is in `/root` (which survives reboots):

```
/root/camss-v11/     patched camss tree + qcom-camss.ko
/root/ovmod/         OV8856 driver + ov8856.ko
/root/motor/         popup motor driver + raphael-popup-motor.ko
/root/mclk/          cam-mclk-hold + cam-power-hold
/root/dtbwork/       cam.dts and the built DTBs
/root/dtc/           dtc 1.7.2 (ld path /root/dtc/usr/lib)
/root/tools/         gpioset, i2cdetect (ld path /root/tools/usr/lib/aarch64-linux-gnu)
```

`/tmp` is cleared on every reboot, so those packages have to be re-extracted from
`/home/user/*.deb` after a restart.
