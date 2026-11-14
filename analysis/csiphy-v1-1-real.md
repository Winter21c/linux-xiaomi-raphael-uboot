# The real raphael CSIPHY: 2-phase v1.1

Everything here comes from the phone's own kernel, not from the Mi 9 module that
earlier rounds used.

## How the vendor kernel was obtained

```
/dev/block/by-name/boot  (128 MiB, boot -> /dev/block/sde49)
  -> header "UNCOMPRESSED_IMG" + u32 size, Image starts at 0x14
  -> 43 958 725-byte arm64 Image (magic "ARMd" at 0x38)
  -> vmlinux-to-elf  -> 135 954 symbols, .kernel at 0x2e46000
```

File offset of a virtual address inside the Image: `va - 0x2e46000`.

Key symbols:

```
cam_csiphy_config_dev             0x390ff88
cam_csiphy_core_cfg               0x391055c
cam_csiphy_cphy_data_rate_config  0x390fd2c
cam_csiphy_reset                  0x390f888
cam_csiphy_enable_hw              0x390ea4c
cam_csiphy_parse_dt_info          0x390ec34
csiphy_reset_reg_1_1              0x551306c
csiphy_irq_reg_1_1                0x55130bc
csiphy_2ph_v1_1_reg               0x551310c
csiphy_2ph_v1_1_combo_mode_reg    0x55131bc
csiphy_common_reg_1_1             0x5513028   (decode still uncertain)
```

## Register map (proven)

| range | role |
|---|---|
| 0x000..0x0ff | **data lane 0** |
| 0x200..0x2ff | **data lane 1** |
| 0x400..0x4ff | **data lane 2** |
| 0x700..0x7ff | **clock lane** (matches bit 0x80 of the enable mask) |
| 0x800 | reset / enable control |
| 0x814, 0x818, 0x81c | clock configuration |
| 0x82c..0x854 | interrupt masks (11 registers) |

There is **no second register page**. Everything is inside the 0x1000 window the
device tree already maps, so the mainline mapping is correct - the earlier
"control block at base+0x1000" was a property of the Mi 9 PHY only.

## Record format (proven from the disassembly)

```c
struct csiphy_reg_1_1 {
        u32 offset;      /* +0  */
        u32 value;       /* +4  */
        u32 delay_ms;    /* +8  : "ldr w8, [x, #8]", *1000, usleep_range() */
        u32 type;        /* +12 : "ldr w10, [x, #12]", range-checked, jump table */
};
```

**Correction (round 61):** an earlier version of this document had `type` and
`delay_ms` swapped.  The disassembly is unambiguous: at 0xaca24c and 0xaca35c the
field at **+12** is the one that is compared and switched on, and at 0xaca2b8 and
0xaca3ec the field at **+8** is the one multiplied by 1000 and passed to
`usleep_range`.  With the wrong order the reset sequence came out as
"0x800 = 1, delay 0" instead of "0x800 = 1, delay 1 ms, 0x800 = 0".

`cam_csiphy_config_dev` maps `type` for the lane tables as:

| type | value written |
|---|---|
| 0 | `record->value` |
| 1 | the computed **lane enable mask** |
| 2 | low byte of the rate field |
| 3 | second byte of the rate field |

and for the reset table, type 0/1 select whether the record is written
unconditionally or only when `csiphy->field_508` is zero/non-zero.

## Two different tables, two different moments (proven, corrected in round 62)

The vendor keeps a *reset* table and an *IRQ* table, and they are used in
different places.  Which one is which was resolved from `cam_csiphy_parse_dt_info`
(file offset 0xac8e98, the v1.1 branch):

```
res + 72 = csiphy_reset_reg_1_1   written by cam_csiphy_config_dev (7-way switch)
res + 80 = csiphy_2ph_v1_1_reg    written by cam_csiphy_cphy_data_rate_config
res + 88 = csiphy_irq_reg_1_1     written by cam_csiphy_reset (power-on, no switch)
```

`cam_csiphy_reset` (0x390f888) is a plain loop:

```c
count = res[32];
for (i = 0; i < count; i++)
        cam_io_w(res[88][i].value, base + res[88][i].offset);
        usleep_range(res[88][i].delay_ms * 1000, ...);
```

so **at power-on the IRQ table is replayed**:

```
0x814 = 0x00, 0x818 = 0x00, 0x81c = 0x00, 0x800 = 0x01, 1 ms, 0x800 = 0x00
```

and **at configuration time the reset table is replayed**:

```
0x814 = 0xd5, 0x818 = 0x01, 0x81c = 0x02, 0x800 = 0x01, 1 ms, 0x800 = 0x00
```

An earlier version of this document called the 0xd5 sequence "the reset sequence"
and the driver used it in both places; that is corrected now.

## Interrupt masks (proven, 11 records)

```
0x82c=ff 0x830=ff 0x834=fb 0x838=ff 0x83c=7f 0x840=ff
0x844=ff 0x848=ef 0x84c=ff 0x850=ff 0x854=ff
```

`csiphy->field_135` is set to **11** just before these are written - exactly their
count.  `cam_csiphy_cphy_data_rate_config` then loops `i < csiphy->field_135` and
does `cam_io_w(base + rec->offset, rec->value)` with a 16-byte stride.

## Lane blocks (proven)

Four blocks of 14 records each, 0x2b0 apart in the table, with these register bases:

| table block | base | role | marker |
|---|---|---|---|
| `csiphy_1_1_lane0` | 0x000 | data lane 0 | `0x000 = 0x90` |
| `csiphy_1_1_lane1` | 0x700 | clock lane | `0x700 = 0x80` |
| `csiphy_1_1_lane2` | 0x200 | data lane 1 | `0x200 = 0x90` |
| `csiphy_1_1_lane3` | 0x400 | data lane 2 | `0x400 = 0x90` |

Data lane 0 (offsets relative to its base): `0x004=0x0c, 0x02c=0x01, 0x034=0x0f,
0x01c=0x0a, 0x014=0x60, 0x028=0x00 (4 ms), 0x03c=0xb8, 0x000=0x90, 0x008=0x0e
(type 2, 2 ms), 0x00c=0x00 (4 ms), 0x010=0x52, 0x038=0xfe, 0x060=0x00, 0x064=0x7f`.

The clock block differs in three places: `0x700=0x80`, `0x70c=0xa5` (data blocks
have `0x00c=0x00`) and `0x738=0x1f` (data blocks have `0x038=0xfe`).

## Lane enable mask algorithm (proven)

```
in  = lane_mask_field & 0x1f          /* bit 1 is the clock lane */
out = (in & 0x2) ? 0x80 : 0
bit = 0
for i in 0..3:
    if i == 1: continue               /* clock */
    if in & (1 << i): out |= 1 << bit
    bit = min(bit + 1, 3)
```

For a 2-lane sensor (data lanes 0 and 1 plus clock) the input is `0b0111 = 0x7`
and the value written is `0x80 | 0x1 | 0x2 = 0x83`.

The mask is delivered by the **type-1 records**, i.e. it is written into the base
register of each enabled block (`0x000`, `0x200`, `0x700` for a 2-lane sensor).

## Status

| item | status |
|---|---|
| kernel image + symbols | obtained, reproducible |
| register map | proven |
| reset / IRQ / lane tables | proven, extracted verbatim |
| lane mask algorithm | proven |
| lane block roles | proven (clock at 0x700) |
| `csiphy_common_reg_1_1` decode | **open** - values do not look like write pairs |
| mainline driver | written, **syntax-checked** against stub headers (round 61) |
| frames on mainline | not yet attempted (needs the device) |

Two compile-breaking defects were found and fixed by the round-61 syntax check:
the register table `csiphy_1_1_reset[]` collided with a function of the same name,
and the struct field order was swapped.  Neither would have compiled.
