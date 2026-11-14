# Next mainline session - run these in order

The v1.1 CSIPHY work is finished offline; everything below is ready to apply.
Nothing here has been compiled or run yet.

## 1. Switch the CSIPHY driver to the v1.1 variant

Copy three files into the kernel tree:

```
drivers/media/platform/qcom/camss/camss-csiphy-2ph-1-1.c
drivers/media/platform/qcom/camss/camss-csiphy-2ph-1-1.h
drivers/media/platform/qcom/camss/csiphy_v1_1_tables.h
```

then apply `../../patches/current/0016-camss-csiphy-2ph-1-1.patch`, which only
adds the object to the Makefile, declares the ops in camss-csiphy.h and switches
the four `csiphy_res_sm8150[]` entries from `csiphy_ops_2ph_1_0` to
`csiphy_ops_2ph_1_1`.

Build, load, and run the usual 1640x1232 pipeline on the tele:

```
modprobe i2c-qcom-cci
modprobe qcom-camss
insmod /root/ovcomb/ov8856.ko
/tmp/mcam /dev/v4l-subdev2 fmt 0 1640 1232 0x300a     # csiphy2
...
```

Success: `bytes > 0` on /dev/video2, or at least `msm_csid0` interrupts rising.

## 2. If it still receives nothing

Dump these four registers right after `lanes_enable` and compare with the table:

```
0x82c  should read 0xff   (first IRQ mask)
0x800  should read 0x00   (reset released)
0x000  should read 0x90   (data lane 0 block, last write)
0x700  should read 0x80   (clock lane block)
```

If they match, the PHY is programmed exactly as the vendor programs it and the
remaining difference is outside the PHY (CSID input select or the sensor itself).
If they do not match, something overwrote them - which would point at the order of
`reset()` (called at power-on) versus `lanes_enable()`.

## 3. Motor (independent of the camera)

Add `../dtb/raphael-camera-v11-additions.dtsi` (the pwms@bc00 node and the
popup-motor node), rebuild the dtb, then build
`../../motor/driver/raphael-popup-motor.c`.

`echo 1 > /sys/.../position` should extend the camera.  Watch it the first time;
the DIR polarity and the M0/M1 combination are the two assumptions.

## 4. What is still unknown (documented, not guessed)

* `csiphy_common_reg_1_1` decode - the values do not look like write pairs, so it
  is not used anywhere;
* which record carries the lane enable mask - the driver passes the mask to every
  type-1 record and there are none in the four blocks we have, so the mask is
  currently only used for the block base writes;
* `csiphy->field_508` semantics in the vendor reset-table switch (the driver treats
  every record as unconditional).
