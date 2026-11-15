# What has been verified, and how

## Compiles (syntax level)

Both drivers were compiled with `gcc -fsyntax-only -Wall` against a stub header set
that reproduces the exact declarations of this tree (taken from
`camss-csiphy.h` / `camss-csiphy.c` of GengWei1997/linux raphael-v7.3.0):

| file | result |
|---|---|
| `camss-csiphy-2ph-1-1.c` | **passes**, only `-Wsign-compare` on `ARRAY_SIZE` loops |
| `motor/driver/raphael-popup-motor.c` | **passes**, no warnings |

This is not a kernel build - it proves the code is consistent with the declared
API and free of typos, not that it links.  The device test is still required.

Two defects were found this way and fixed:

1. `csiphy_1_1_reset[]` (table) and `csiphy_1_1_reset()` (function) collided - the
   function is now `csiphy_1_1_hw_reset()`;
2. the register-record struct had `type` and `delay_ms` swapped.

## API cross-check against the real tree

Verified field by field in `camss-csiphy.h` / `camss-csiphy.c`:

* `struct csiphy_hw_ops` has **no** `.regs` member in this kernel, and
  `lanes_enable` takes `(csiphy, cfg, s64 link_freq, u8 lane_mask)`;
* `get_lane_mask(lane_cfg)` produces the mask that is passed in;
* `reset()`, `hw_version_read()` and `init()` are called **unconditionally**, so
  all three are implemented (`init()` runs before the ioremap and touches nothing);
* `csiphy->base`, `csiphy->id`, `csiphy->camss->dev`, `csiphy->cfg.csi2->lane_cfg`
  with `num_data` / `data[i].pos` - all confirmed against the header.

## Verified at runtime (round 65)

The driver is now loaded on the phone and programs the real PHY.  Read back
immediately after the lane and IRQ tables are replayed:

```
csiphy2 v1.1 readback: 0x000=00000090 0x700=00000080 0x82c=000000ff 0x800=00000000
```

| offset | value | what it proves |
|---|---|---|
| 0x000 | 0x90 | data lane 0 block, exactly the vendor table value |
| 0x700 | 0x80 | the clock lane really is at 0x700 (not 0x600 as in 2.1.1) |
| 0x82c | 0xff | first of the eleven interrupt mask registers |
| 0x800 | 0x00 | reset released (the 0x814/0x818/0x81c/0x800 sequence ran) |

The CSID downstream of it still reads zero, so no frame has been captured yet; the
PHY work itself is confirmed.

## Not verified

* end-to-end frame capture (blocked by the CSID, see `../../camera/vendor-capture/`);
* the motor driver has never been loaded, and the LPG PWM node it needs is not in
  our device tree yet (the node is in `../dtb/raphael-camera-v11-additions.dtsi`).

## The pop-up motor driver builds against the real kernel (round 72)

Compiled on the phone against `/lib/modules/7.3.0-rc5-sm8150-.../build` with clang:

```
/root/motor/raphael-popup-motor.c
  -> raphael-popup-motor.ko   282112 bytes
insmod raphael-popup-motor.ko  -> 0, module stays loaded, no probe log (no DT node)
```

One API fix was needed: this kernel has `pwm_apply_might_sleep()`, not
`pwm_apply_state()`.  The source in this directory carries the fix.

So deliverable (4) - "the pop-up motor driver completed and ready to build" - is met:
it builds without errors and loads.  It cannot probe yet because the pm8150l LPG PWM
node and the motor node are still only in the candidate dtsi.
