# Round 86: the CSIPHY window is completely inert during streaming

## The measurement

A probe samples every register in the CSIPHY window (0x000..0x83c) twice, 120 ms
apart, while the pipeline is streaming, and prints the ones that changed:

```
RAPHAEL SCAN changed:
```

**Nothing changed. Not one register in the whole block.**  The PHY is not seeing a
clock lane, a data lane, or any transition at all.  This is the cleanest statement yet
of what is wrong: the receiver is correctly programmed and simply never receives.

## What this rules in and out

Everything on the transmit side has been checked independently:

| item | state |
|---|---|
| sensor I2C at 3-0036 | answers, chip id correct |
| sensor registers after stream-on | `0x0100=0x01`, PLL written, `0x4800=0x44`, colour bar set |
| MCLK1 | 19.2 MHz, pad muxed as `cam_mclk` |
| avdd | `vreg_bob` at 3.03 V, enabled |
| dovdd | `camera_viodd_ldo` 1.8 V, enabled |
| dvdd | `camera_vdig_tele`, measured high (GPIO 93) |
| reset | GPIO 30 asserted/deasserted by the driver |

and on the receive side:

| item | state |
|---|---|
| CSID base | 0xacb3000, `hw_version=0x10000000`, `rx_cfg0` write sticks |
| CSID lane mapping | now consistent with the PHY (`assign=0x10`, lanes 0/1) |
| CSID capability | proven by its own test generator (`rxirq=0x004000ff`, VFE IRQs) |
| CSIPHY clocks | `csiphy2` 400 MHz, `csiphy2_timer` 300 MHz, `cphy_rx` 400 MHz |
| CSIPHY tables | replayed, readback matches the vendor values |
| CSIPHY activity | **none whatsoever** |

So the fault is between the sensor's MIPI pads and the PHY's inputs, and it is not a
register we can see from either side.

## Two things found along the way

`/home/winter/tmp/re/camera.ko` exists on this host - the vendor camera module, 6.7 MB,
not stripped.  It contains the CSIPHY status-register plumbing
(`cam_csiphy_get_common_status_regs`, `cam_csiphy_util_update_aon_registration`,
the "CSIPHY %d status reg is NULL" message) but **no OV8856 tables**: the vendor keeps
sensor register sequences in the per-sensor `.so` under `/vendor/lib`, which is not in
this tree.

`cam_csiphy_irq` and `cam_csiphy_cphy_data_rate_config` in the vendor kernel both
disassemble to loops that read registers and throw the values away - they are debug
stubs, so the interrupt-status semantics cannot be recovered from this image.

## Where that leaves it

With the PHY proven inert, the remaining explanations are physical or inside the
module: a flex that is not seated, a module whose MIPI section is dead, or a sensor
enable that neither the mainline driver nor the extracted tables contain.

The handset cannot be opened in this session, so none of those can be tested here.
