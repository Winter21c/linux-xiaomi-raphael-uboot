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
