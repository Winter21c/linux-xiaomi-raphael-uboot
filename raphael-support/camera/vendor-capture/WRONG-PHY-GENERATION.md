# The correction that matters: we were using the wrong PHY generation

## How it was found

The stock raphael device tree (dumped from the running phone) says:

```
cam_csiphy2: qcom,csiphy@ac67000 {
        compatible = "qcom,csiphy-v1.1", "qcom,csiphy";
```

but the vendor module that every earlier round of this port was reversed from
(`camera.ko` pulled from a **Mi 9 "dada"** HyperOS ROM) contains only:

```
qcom,csiphy-v2.1.0  qcom,csiphy-v2.1.1  qcom,csiphy-v2.1.3
qcom,csiphy-v2.2.0  qcom,csiphy-v2.2.1  qcom,csiphy-v2.3.0 ...
```

- no `v1.x` at all.  Raphael's camera code is not even a module on this ROM: it is
  built into the kernel (no `camera.ko` under `/vendor/lib/modules`, and
  `cam_csiphy_core` appears in `/proc/kallsyms` without a module file).

So every CSIPHY table used so far - the 103-record `csiphy_2ph_v2_1_1_reg`
sequence, `csiphy_lane_en_reg_2_1_1` (0x1014), the reset/common/IRQ tables at
0x1000..0x1084, the 12 `datarate_211_*` PLL tables, and the whole "the control
block is at base + 0x1000" conclusion - **belongs to a different PHY generation
and does not apply to raphael**.

## The correct data, now extracted

The phone's own kernel was pulled from `/dev/block/by-name/boot`
(`UNCOMPRESSED_IMG` header + 44 MB arm64 `Image`) and symbolised with
`vmlinux-to-elf`.  The real tables are:

| symbol | address | records |
|---|---|---|
| `csiphy_reset_reg_1_1` | 0x551306c | 5 |
| `csiphy_irq_reg_1_1` | 0x55130bc | 5 |
| `csiphy_2ph_v1_1_reg` | 0x551310c | 11 |
| `csiphy_2ph_v1_1_combo_mode_reg` | 0x55131bc | 14 (+padding) |
| `csiphy_common_reg_1_1` | 0x5513028 | 4 (decode uncertain) |

Record layout is 16 bytes `{u32 offset; u32 value; u32 mask; u32 type}` - proven
from the disassembly of `cam_csiphy_config_dev` (`add x9, x9, w8, uxth #4` stride
16, and a `switch` on field +12 with 4 cases).

**The control block of this PHY is at base + 0x800, not base + 0x1000:**

```
reset: 0x814=0xd5, 0x818=0x01, 0x81c=0x02, 0x800=0x01, 0x800=0x00
irq:   0x814=0x00, 0x818=0x00, 0x81c=0x00, 0x800=0x01, 0x800=0x00
```

and the per-lane init (combo table, first 14 records):

```
0x004=0x0c 0x02c=0x01 0x034=0x0f 0x01c=0x0a 0x014=0x60 0x028=0x00
0x03c=0xb8 0x000=0x90 0x008=0x0e 0x00c=0x00 0x010=0x52 0x038=0xfe
0x060=0x00 0x064=0x7f
```

Note 0x000 = **0x90**, not the 0x8e that the Mi 9 table used, and there is no
0x400-block PLL table at all in this generation.

Generated header: `../drivers/csiphy_v1_1_tables.h`.
Symbol table of the real kernel: `data/raphael-syms.txt.xz`.

## Consequence

* the "lane enable bit 0x14 |= 0x10" change is based on the wrong module and
  should be dropped, not tested;
* the mainline CSIPHY driver for raphael has to be rebuilt around the v1.1
  control block (0x800) and the tables above;
* the CSID work is unaffected: raphael uses `qcom,csid175`, which is the same
  generation the Mi 9 module carried (`cam_ife_csid175_200_*`).
