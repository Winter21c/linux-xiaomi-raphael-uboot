# Honest audit before flashing back to Linux

Three buckets: what is measured, what is inferred, and what has never been built
or run.

## A. Measured on the phone (solid)

| claim | where it comes from |
|---|---|
| tele path = csiphy[2] -> csid[0] -> ife[0] | vendor dmesg `cam_cpas_hw_start: AXI client=[2][csiphy][2]`, `[6][csid][0]`, `[10][ife][0]` |
| wide path = csiphy[0] -> csid[1] -> ife[1] | same log, clients 0 / 7 / 11 |
| MCLK0 and MCLK1 = 19.2 MHz | `/sys/kernel/debug/clk/clk_summary` |
| csiphy0/2 = 400 MHz, csi2phytimer = 300 MHz, cphy_rx = 400 MHz, ife0_csid = 400 MHz, ife0 = 400 MHz | same |
| sensors: 0x586 @0x34(=0x1a), 0x885a @0x6c(=0x36), 0x30c6 @0x20(=0x10) | vendor `CAM-SENSOR` logs |
| csiphy/CCI/mclk/reset/vdig-en GPIO per camera | `/sys/firmware/fdt` cam-sensor nodes |
| supplies bob/s8/viodd/l17/s5 + voltages | same |
| CCI controller 1 exists at 0xac4b000 | `cam_cci1: qcom,cci@ac4b000` |
| motor = TI DRV8846, STEP from pm8150l `qcom,pwms@bc00` channel 1, DIR=9, M0=21, M1=33, SLEEP=35, PWREN=130, 19.2 kHz / 720 ms / 50 ms ramps | `ti,drv8846` node |
| motor pins are claimed outputs on MIUI, unclaimed inputs on mainline | `/sys/kernel/debug/gpio` both systems |
| front camera activates csiphy1 | vendor dmesg `[csiphy][1]` |

## B. Inferred, not proven

1. **`0x14 |= 0x10` per lane.** The disassembly *proves* that
   `cam_csiphy_update_lane_selection` does a read-modify-write ORing **0x10** into a
   per-lane register, and ORs a mask into another per-lane register.  Which
   register that is (0x14) comes from the `csiphy_v2_1_1` hw_info layout
   (field +0x20 = 0x14), **not** from tracing the actual write.  The driver now
   exposes `camss_lane_en_off` / `camss_lane_en_bit` so the test can sweep
   0x04 / 0x14 / 0x18 / 0x1c without a rebuild.
2. **That this bit is what stops the packets.** Plausible, untested.
3. **Motor DIR polarity (1 = extend)** and **M0=1 / M1=0 for `step-mode = 2`** -
   both guessed; the driver can be flipped in one line.
4. **Front sensor id 0x3142** was seen but not matched to a specific part.
5. **`laneAssign` = 0** in the vendor OV8856 blob (read directly), which suggests
   default lane assignment, but I could not decode the rest of that container.

## C. Written but never compiled or run

1. `drivers/camss-csiphy-2ph-1-0.c.vendor-global` with `csiphy_lane_selection()`.
   **It has not been compiled.**  It may need small fixes (the file is a full copy
   of the driver source, so build it in place in the kernel tree).
2. `../motor/driver/raphael-popup-motor.c` - never compiled, never loaded.
   API risk: `pwm_init_state()` / `pwm_apply_state()` on 7.3, and
   `device_create_file()` on the platform device.

## D. Known gaps for the motor

The motor's PWM comes from `qcom,pwms@bc00` **under pm8150l@4**, which our device
tree does **not** have (we only have the flash `leds@d800` node).  Before the motor
driver can bind, that node has to be added with `#pwm-cells = <2>` and at least two
channels.  Nothing else about the motor is missing.

## E. Statement I should soften

"All PHY status registers read 0, therefore the PHY receives no clock" - the
measurement is solid, the conclusion is an interpretation.  It is equally
consistent with the lane outputs being disabled, which is exactly what the
lane-enable hypothesis says.
