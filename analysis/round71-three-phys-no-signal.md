# Round 71: three PHYs, the sensor's own test pattern, still no CSI-2

## What was measured

### The vendor `cam_csiphy_enable_hw` is not a register writer

Disassembled from the phone's own copy of the vendor Image with
`aarch64-linux-gnu-objdump` (the host tools are unusable, see below):

```
390ea58  ldr w9, [x0, #656]      ; field_656
390ea5c  ldr x8, [x0, #536]      ; a rate
390ea70  ldrb w9, [x0, #508]     ; field_508
390ea7c  cmp w9, #0x12
390ea8c  mov w9, #0x3 / 390ea80 mov w9, #0x4
390ea90  lsr x8, x8, x9
390eaac  cmp w9, #1
...
390eb0c  ret                     ; returns a clock index
```

It **computes and votes a CSI PHY clock rate**; it writes no registers.  The register
programming really is `cam_csiphy_config_dev`, whose visible body is the table replay
plus a verification read (`cam_io_r_mb`/`cam_io_r`) that compares each register with
the table value and logs a mismatch.  Our replay produces exactly the table values
(`0x000=0x90`, `0x700=0x80`, `0x82c=0xff`), so the PHY programming matches the vendor
byte for byte.

### The v1.1 lane tables contain no type-1 records

Checked every record in `csiphy_v1_1_tables.h`: the lane tables are all type 0 (plus
two type-4 records the vendor skips).  The lane "enable mask" is therefore not what
enables a lane - the table's own `0x000=0x90` / `0x700=0x80` values are.  That is why
the mask sweep could never have changed anything.

### PHY index sweep

| forced PHY | csiphy interrupts | csid0 interrupts | bytes |
|---|---|---|---|
| 2 (DT value) | 1 per stream | 0 | 0 |
| 1 | 1 per stream | 0 | 0 |
| 0 | 1 per stream | 0 | 0 |

Identical for all three, so the PHY index is not the fault.

### The PHY's own analog rails are on

```
ac65000.camss-vdda-pll   1   10mA
ac65000.camss-vdda-phy   1   17mA
```

both fed from `vreg_l3c_1p2` during streaming.

### The sensor reads back exactly what it was given

```
RAPHAEL I2CREAD 0100=01 0103=00 0300=00 0301=00 0302=4b 0303=01 0304=03
                0305=01 0306=01 0307=00 300a=00 300b=88 4800=44 4801=00 4805=00
```

`0x0100=0x01` (streaming), no software reset pending, PLL registers written
(`0x300b=0x88`), MIPI control `0x4800=0x44`.  Notably `0x4801` and `0x4805` are
**zero** - the mainline driver never writes a MIPI lane-count register, which is a
prime suspect: a sensor that believes it drives zero lanes stays silent while every
register we do write reads back correctly.

### Even the sensor's own test pattern produces nothing

`v4l2-ctl -d /dev/v4l-subdev22 --set-ctrl=test_pattern=1` reports
`test_pattern: 1 (Standard Color Bar)`, and a stream with it enabled still delivers
`csid_irq=0`, `bytes=0`.

## Host tooling note

`bash`, `grep`, `glob` and the REA/Ghidra provider all fail on this host with
`Unknown system error -122 ... write`, i.e. the disk-write path is broken.  All the
analysis above was done by uploading files to the phone with `ssh_upload` and running
`objdump`/`grep` there.

## Next

1. read `0x5e00` back to confirm the sensor really took the colour-bar setting;
2. find how the OV8856 is told its lane count (`0x4801`/`0x4805` are zero) and set
   it to 2 - the vendor sensor driver in `camera.ko` is the reference;
3. reconsider the MCLK: the mode tables may assume 24 MHz while the board runs
   19.2 MHz; the PLL written into `0x0300..0x0307`/`0x300b` has to match the
   actual clock.
