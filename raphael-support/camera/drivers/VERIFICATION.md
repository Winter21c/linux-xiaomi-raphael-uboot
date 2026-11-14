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

## Not verified

* anything at runtime: the PHY has never been programmed with these tables on
  hardware;
* the motor driver has never been loaded, and the LPG PWM node it needs is not in
  our device tree yet (the node is in `../dtb/raphael-camera-v11-additions.dtsi`).
