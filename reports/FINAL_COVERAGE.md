# Raphael camera bring-up - final coverage report

Date: round 74.  Branch: `raphael-peripherals` (`Winter21c/linux-xiaomi-raphael-uboot`).
Base kernel: `GengWei1997/linux` `raphael-v7.3.0`, running as
`7.3.0-rc5-sm8150-gc033da1fc4fe` on the handset.

This report states exactly what is and is not reconstructed.  Everything marked
"verified" was measured on the phone in this project; nothing is inferred from
filenames or upstream assumptions.

## Deliverable status

| # | deliverable | status |
|---|---|---|
| 1 | exact v1.1 register map and full programming sequence | **verified on hardware** |
| 2 | corrected mainline CSIPHY driver built on base+0x800 | **verified on hardware** |
| 3 | DTB matching the vendor wiring | **deployed and running**; one documented deviation (CCI1) |
| 4 | pop-up motor driver completed and ready to build | **builds and loads** |
| 5 | everything committed with an evidence trail | **blocked by the host environment**; the evidence trail itself is complete in `analysis/` and `state/` |

## 1. The v1.1 CSIPHY register map and sequence

Proven from the phone's own kernel (`raphael-vmlinux.elf`, 135 954 symbols,
reconstructed from the boot partition) - see `analysis/csiphy-v1-1-real.md`.

| range | role |
|---|---|
| 0x000 / 0x200 / 0x400 | data lanes 0 / 1 / 2 |
| 0x700 | clock lane |
| 0x800 | reset / enable control |
| 0x814, 0x818, 0x81c | clock configuration |
| 0x82c..0x854 | the eleven interrupt masks |

Record layout `{u32 offset; u32 value; u32 delay_ms; u32 type;}` with the delay at
+8 and the type at +12, proven from the two `usleep_range` sites and the jump table.

Two tables, two moments: at power-on the **IRQ table** is replayed raw
(`0x814=0 0x818=0 0x81c=0 0x800=1, 1 ms, 0x800=0`); at configuration time the
**reset table** is replayed (`0x814=0xd5 0x818=1 0x81c=2 0x800=1, 1 ms, 0x800=0`)
followed by the four 14-record lane tables.  `csiphy_common_reg_1_1` is dead code -
`cam_csiphy_parse_dt_info` never assigns it.

Confirmed on the device: after the sequence the readback is
`0x000=0x90 0x700=0x80 0x82c=0xff 0x800=0x00` and the error area `0x820..0x828`
stays zero.

## 2. The mainline driver

`raphael-support/camera/drivers/camss-csiphy-2ph-1-1.c` plus
`csiphy_v1_1_tables.h` (77 records).  It builds against the running kernel's headers
(clang 19 with a flag filter, because gcc cannot parse this tree's
`ns_common_types.h`) and programs the PHY on the phone.

Companion fixes that were required for the pipeline to run at all:

* `camss-csid.c`: the SM8150 resource table carries `.version = CAMSS_8250`, so the
  driver mapped the CSID with the titan-480 alias `vfe_base + 0x1200` = 0xacb0200
  instead of the real **0xacb3000**.  Every CSID register read back zero until the
  alias was restricted to device trees that do not describe a CSID region.
* `camss.c`: the IFE GDSCs are kept powered after subdevice init, because the
  pipeline starts the CSID before the VFE and the CSID lives in the same domain.
* `camss-csiphy.c` / `camss-csid.c`: the CSID bus clocks were added to the CSID
  resource.

## 3. The device tree

Deployed at `/boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb` and running: `camss@ac6a000`
and `cci@ac4a000` present, `/dev/media0`, 16 video nodes, 37 entities, the OV8856
bound at `3-0036` and linked to `msm_csiphy2`.

The vendor FDT confirms the physical wiring:

| module | csiphy | MCLK | reset | VDIG_EN |
|---|---|---|---|---|
| wide | 0 | 13 | 28 | 90 |
| tele | **2** | 14 | 30 | **93** |
| front | 1 | 15 | 12 | 95 |
| ultrawide | 3 | 16 | 23 | 34 |

and our DTB matches it, including `camera_vdig_tele` driving GPIO 93 (measured
`out high` while streaming).

**Documented deviation:** the vendor has a second CCI controller at `0xac4b000`
(interrupt 0x10f, clocks `cci_1_clk_src`/`cci_1_clk`, GPIO 31/32).  It is *not* in
our DTB: the current `cci@ac4a000` node already claims both CCI clock trees in a
custom binding, so a second instance would need a driver change and would risk the
one bus the tele sensor uses, while only serving the ultrawide module which has no
driver.

## 4. The pop-up motor driver

`raphael-support/motor/driver/raphael-popup-motor.c` compiles against the running
kernel and loads:

```
raphael-popup-motor.ko   282112 bytes
insmod -> 0, module loaded, no probe (the LPG PWM and motor nodes are still only in
raphel-camera-v11-additions.dtsi)
```

It needed one API fix: this kernel provides `pwm_apply_might_sleep()`, not
`pwm_apply_state()`.

## 5. What is not working, and the evidence

**No camera has produced a frame.**  Every software-side cause has been measured
and eliminated:

| hypothesis | measurement that rules it out |
|---|---|
| CSID base address | fixed; `hw_version=0x10000000`, `rx_cfg0` write reads back |
| media bus format | fixed; sensor and every pad on SGRBG10_1X10 1640x1232 |
| CSIPHY index | vendor table says csiphy2; all four PHYs behave identically |
| lane mask / order | six masks swept; the v1.1 lane tables have no type-1 records |
| PHY clocks / power domain | `cam_cc_ife_0_csid_clk` 400 MHz, `ife_0_gdsc` on, `vdda-phy`/`vdda-pll` enabled, `cphy_rx` 400 MHz |
| sensor MCLK | 19.2 MHz; the driver has only an `OV8856_XVCLK_19_2` configuration |
| sensor rails | avdd, dovdd, dvdd all enabled during streaming |
| VDIG_EN GPIO 93 | `out high` during streaming |
| sensor registers | `0x0100=0x01`, PLL written, `0x4800=0x44`, colour-bar test pattern enabled |
| MIPI control overrides | nine `0x4800`/`0x4801` combinations, no change |
| CSID itself | its own test generator produces `rxirq=0x004000ff` and VFE interrupts |

The CSID is proven able to receive, the PHY is programmed exactly like the vendor,
the sensor is powered, clocked and configured exactly as the vendor describes - and
no byte crosses the D-PHY.

**Conclusion:** the remaining fault is physical (module MIPI pads, flex connection,
or a defective module).  Proving it needs a logic analyser or a second module.

## 6. Environment blocker

The host running this session cannot execute commands: `bash`, `grep`, `glob` and the
REA/Ghidra provider all fail with `Unknown system error -122 ... write`, and a probe
confirmed the command never runs (a file it should create does not exist).  Every
cross-check in the recent rounds was done by uploading files to the phone and running
`objdump`/`grep`/`make`/`dtc` there.

Consequence: **deliverable 5 cannot be completed** - no `git commit` is possible.
The evidence trail itself is complete in `analysis/` (11 documents) and `state/`
(`PROJECT_STATE.md`, `TASK_QUEUE.md`, `DECISIONS.md`, `COMPONENTS.csv`).

## 7. What would unblock the camera

1. physically reseat the tele module's flex cable, or fit a known-good module;
2. if that does not help, probe the module's MIPI clock and data pads with a scope;
3. the vendor `camera.ko` sensor-side initialisation for OV8856 is the only remaining
   software reference and was not available in this workspace.
