# What the stock MIUI system tells us (captured on the device)

All of this was captured on the phone running MIUI with root (Magisk, SELinux
permissive), while the stock camera was streaming.

## 1. Sensor map, confirmed from the vendor's own log

```
sensor_id:0x586  slave_addr:0x34  -> IMX586  wide   (i2c 0x1a)
sensor_id:0x885a slave_addr:0x6c  -> OV8856  tele   (i2c 0x36)
sensor_id:0x30c6 slave_addr:0x20  -> ultrawide      (i2c 0x10)
```

The vendor device tree (`data/vendor-fdt.dts`) maps them to:

| node | csiphy-sd-index | sensor |
|---|---|---|
| cam-sensor@0 | 0 | IMX586 wide |
| cam-sensor@1 | **2** | OV8856 tele |
| cam-sensor@2 | 1 | front |
| cam-sensor@3 | 3 | ultrawide |

## 2. The tele path is exactly what mainline already drives

`data/s_tele.txt` shows the hardware acquired for the 2x session:

```
AXI client=[2][csiphy][2]     tele PHY
AXI client=[6][csid][0]       -> csid0
AXI client=[10][ife][0]       -> ife0 (VFE0)
CAM_INFO: cam_ife_csid_irq: CSID:1 IPP SOF received      (wide)
```

and the wide camera in parallel: `csiphy[0] -> csid[1] -> ife[1]`.

So `csiphy2 -> csid0 -> vfe0` **is** correct - the routing hypothesis is dead.

## 3. Every clock mainline uses is already correct

From `/sys/kernel/debug/clk/clk_summary` during the tele session:

```
cam_cc_mclk1_clk          19200000     (tele MCLK 19.2 MHz)
cam_cc_mclk0_clk          19200000     (wide MCLK)
cam_cc_csiphy2_clk        400000000
cam_cc_csi2phytimer_clk   300000000
cam_cc_cphy_rx_clk_src    400000000
cam_cc_ife_0_csid_clk_src 400000000
cam_cc_ife_0_clk_src      400000000
```

which matches the mainline resource table (`{400 MHz}, {300 MHz}` for
csiphy/timer).

## 4. The missing step: per-lane enable bit

Disassembling `cam_csiphy_update_lane_selection` (0x17a5bc) in the vendor
`camera.ko` shows that after the static tables are replayed the driver does, for
every enabled lane block:

```c
val = readl(lane_base + 0x04);  val |= lane_mask;   writel(val, lane_base + 0x04);
val = readl(lane_base + 0x14);  val |= 0x10;        writel(val, lane_base + 0x14);
```

The static table we replay writes `0x14 = 0x60`, i.e. **bit 4 is clear**, and
mainline never sets it.  That bit is the per-lane output enable.

`camss-csiphy-2ph-1-0.c.vendor-global` now performs this step
(`csiphy_lane_selection()`) right after the vendor sequence and before the global
page programming.

## 5. What this means for the mainline test

When the phone is flashed back to Linux the only untested change is this lane
enable bit (plus the 0x04 mask OR).  If the PHY was silent because its lane
outputs were disabled, this is the fix; if it still fails, the remaining
difference is inside the global page writes (which we already replay verbatim).

## Capture tooling that did NOT work (so nobody repeats it)

* `kprobe_events` - not exported by this kernel (`CONFIG_KPROBE_EVENTS` off), so
  the `cam_io_w_mb` argument trace was impossible;
* `perf` kprobe PMU - absent (only `tracepoint` is listed);
* `camera` tracepoints exist but cover IRQ/CDM/request-mgr, not register writes;
* `enable_reg_dump`, `ife_csid_debug`, `debug_mdl`, `csiphy_dump` - all accepted,
  and `debug_mdl=65535` floods dmesg (ring buffer drops the useful lines) but no
  register dump is emitted;
* `/dev/mem` - the kernel refuses camera MMIO ranges (ENXIO), even after creating
  the node by hand.

## 6. Complete camera wiring, from the live vendor device tree

Decoded from `data/vendor-fdt.dts` (each `qcom,cam-sensor@N` node):

| sensor | csiphy | CCI dev / master | MCLK gpio | reset gpio | vdig-en gpio | MCLK |
|---|---|---|---|---|---|---|
| @0 IMX586 wide | 0 | 0 / 0 | 13 | 28 | **90** | 19.2 MHz |
| @1 OV8856 tele | 2 | 0 / 1 | 14 | 30 | **93** | 19.2 MHz |
| @2 front | 1 | 0 / 0 | 15 | 12 | **95** | 19.2 MHz |
| @3 ultrawide | 3 | **1** / 0 | 16 | 23 | **34** | 19.2 MHz |

Supplies (all four cameras):

```
cam_vio    = camera_viodd_ldo   1800000 uV
cam_vana   = pm8150l_bob        3008000 uV
cam_vdig   = pm8150l_s8         1352000 uV
cam_v_custom1 = pm8150_l17      3096000 uV
cam_v_custom2 = pm8150_s5       1904000 uV   (wide only)
```

Two things mainline does not have today:

1. **CCI controller 1 (`cci@ac4b000`)** - the ultrawide is on CCI device 1; our DT
   only instantiates CCI0, so the ultrawide can never be reached.
2. the per-camera `vdig` enable GPIOs (90 / 93 / 95 / 34) - mainline only has the
   tele one (93) as a fixed regulator.

Everything about the tele (csiphy2, CCI0 master 1, gpio 14/30/93, 19.2 MHz,
bob/s8/viodd/l17) already matches what mainline drives.

## 7. What the stocks system does NOT expose (so nobody repeats the effort)

* no CSIPHY/CSID register values: `kprobe_events` missing, only `tracepoint` PMU,
  `camera` tracepoints cover IRQ/CDM/req-mgr only, `/dev/mem` refuses camera MMIO,
  and no debugfs knob (`enable_reg_dump`, `ife_csid_debug`, `debug_mdl`,
  `csiphy_dump`) emits a register dump;
* `debug_mdl=65535` does flood dmesg (~2000 lines/s) and the ring buffer then drops
  the interesting lines, so keep it at 0.

## 8. Consequence for the mainline test

The single untested change is the per-lane enable bit (`0x14 |= 0x10`) plus the
`0x04` lane-mask OR, both implemented in
`../../drivers/camss-csiphy-2ph-1-0.c.vendor-global`.  Everything else - routing,
clocks, MCLK, supplies, lane count - now has vendor evidence saying it is right.
