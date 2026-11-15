# Round 67: why the CSID was silent, and the fix

## Root cause

`camss-csid.c` chooses where to map the CSID registers from the CAMSS *version*:

```c
	if (camss->res->version == CAMSS_8250) {
		/* for titan 480, CSID registers are inside the VFE region ... */
		csid->base = get_base_address(camss, id) + VFE_480_CSID_OFFSET;
	} else {
		csid->base = devm_platform_ioremap_resource_byname(pdev, res->reg[0]);
	}
```

The SM8150 resource table carries **`.version = CAMSS_8250`** (it genuinely shares the
titan 480 VFE), so the driver took the alias path with
`VFE_480_CSID_OFFSET = 0x1200`:

```
CSID0 computed = vfe0 base (0xacaf000) + 0x1200 = 0xacb0200
CSID0 actual   = 0xacb3000                      (vendor DT, confirmed in /proc/device-tree)
```

The mapping landed 0x2e00 short, inside the VFE's own window, so every CSID
register read back zero and every write was lost.  Nothing was wrong with the
power domain, the clocks or the DT.

## Fix

Only take the alias path when the device tree does **not** describe a region for
that CSID:

```c
	if (camss->res->version == CAMSS_8250 &&
	    !platform_get_resource_byname(pdev, IORESOURCE_MEM, res->reg[0])) {
```

## Result on hardware

```
RAPHAEL RES csid0 csid0 start=0xacb3000 size=0x1000
CSID0 dump: 0x0=10000000 0x4=00000000 0x10=00000000 0x14=00000000
            0x18=00000000 0x80=00000000 0x100=00032103 0x104=00000001
csid0 hw_version reg(0x0)=0x10000000
csid0 rx_cfg0: wrote=0x00200211 readback=0x00200211  (lane_cnt=2 assign=0x21 phy=2)
csiphy2 v1.1 readback: 0x000=00000090 0x700=00000080 0x82c=000000ff 0x800=00000000
```

The CSID now reports a real hardware version and the RX configuration sticks.

## What is still missing: no CSI-2 packet reaches the CSID

During a stream (all measured concurrently):

| observation | value |
|---|---|
| OV8856 runtime status | **active** (the sensor is streaming) |
| `msm_csiphy2` interrupts | fires (1-2 per stream start) |
| `msm_csid0` interrupts | **0** |
| `csid0 rx_cfg0` | 0x00200211 (configured) |
| captured bytes | 0 |

So the sensor emits, the PHY runs, but the CSID receives nothing.  Next things to
check, in order:

1. the PHY interrupt status registers `0x82c..0x854` - log *why* the PHY interrupts,
   which separates "clock detected" from a lane error;
2. the lane order/polarity: the DT says `data-lanes = <1 2>`, the CSID programs
   `lane_assign = 0x21`, and the vendor's table wants the clock on bit 7 with the
   data lanes in bits 0..3 - a mismatch here drops every packet;
3. the CSID RDI configuration (`CSID_RDI_CFG0`) for rdi0.

## Operational note

`rmmod qcom-camss` segfaults after any stream attempt, so every rebuilt module needs
a reboot before it can be tested.  Batch experiments per boot.
