# Next mainline session: what to do with the MIUI evidence

Everything below is ready; none of it has been tested on mainline yet.

## 1. Test the per-lane enable bit (highest value, one change)

`camss-csiphy-2ph-1-0.c.vendor-global` now calls `csiphy_lane_selection()` right
after the vendor table replay.  It performs what the stock driver's
`cam_csiphy_update_lane_selection()` does:

```c
writel(readl(lane + 0x04) | 0xf, lane + 0x04);
writel(readl(lane + 0x14) | 0x10, lane + 0x14);   /* per-lane output enable */
```

Build it, load qcom-camss, run the usual 2x tele pipeline and watch for
`msm_csid0` SOF / non-zero bytes.

Success criteria: `bytes > 0` on /dev/video2, or at least `msm_csid0` interrupt
count rising.

If it fails, the next thing to try is the same bit on **all four** lane blocks
(the vendor loops over the configured lane count; worth trying 0..3 regardless).

## 2. Add the pop-up motor

`driver/raphael-popup-motor.c` + the DT node in `../motor/README.md`.
Needs the LPG PWM node (`qcom,pwm-lpg@bc00`, 2 channels) wired as
`pwms = <&pm8150l_lpg 1 1000000 0>`.  Then
`echo 1 > /sys/class/.../position` should extend the camera.

Watch it the first time - the DIR polarity assumption (1 = extend) may need
flipping, and the vendor's `step-mode = 2` combination may differ from the
M0=1/M1=0 the driver uses.

## 3. Fill the two wiring gaps

* **CCI controller 1** at `0xac4b000` - the ultrawide is the only camera on it, so
  without it that camera is unreachable.  Straightforward: add the node like
  `cci@ac4a000` with its own clocks/GDSC and a second `i2c-bus@0`.
* per-camera **vdig enable GPIOs**: 90 (wide), 93 (tele), 95 (front), 34
  (ultrawide) - modelled as `regulator-fixed` with `gpio` + `enable-active-high`,
  exactly like the existing tele one.

## 4. What is no longer worth investigating

* routing: the stock kernel proves tele = csiphy2 -> csid0 -> ife0;
* clocks: MCLK 19.2 MHz, csiphy 400 MHz, csi2phytimer 300 MHz, cphy_rx 400 MHz,
  ife0_csid 400 MHz - all identical to our resource tables;
* supplies: bob / s8 / viodd / l17 (and s5 for the wide) - also identical;
* lane count: the stock DT leaves lanes to the HAL, and the module is 2-lane.

So after the lane-enable bit the remaining suspects are only inside the CSIPHY
global page writes, which we already replay verbatim from the vendor tables.
