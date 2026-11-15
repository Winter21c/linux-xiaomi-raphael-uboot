# Vendor-side artifacts found on disk (round 82)

`/home/winter/tmp/miui/` holds material pulled earlier that had not been examined:

| file | what it is |
|---|---|
| `boot.img` | 128 MiB boot partition dump |
| `Image` | 43 958 725-byte vendor arm64 kernel (the source of every table in this repo) |
| `raphael-syms.txt.xz` | 135 954 symbols |
| `com.qti.sensor.ov8856_raphael.so` | 11 KiB aarch64 vendor sensor HAL library, stripped |
| `com.qti.sensormodule.raphael_ofilm_ov8856.bin` | 112 KiB Qualcomm "Chromatix" module/tuning file |
| `d_tele.txt` | 1.8 MB vendor camera runtime trace |
| `dmesg_preview.txt` | 13 KiB dmesg excerpt |

## The sensor HAL library is not useful

`com.qti.sensor.ov8856_raphael.so` contains no register tables - its only strings are
`GetSensorLibraryAPIs`, libc/liblog/libcutils names and section names.  It is a thin
entry point, not the init-sequence data.

## The Chromatix file does carry per-module configuration

It parses as a nested table of 72-byte entries (`name[32]`, then five u64:
a zero field, `-1`, a data offset, a length and an id):

```
sensorDriverData
  sensorName, sensorSlaveAddress, powerSetting x2, resolutionInfo, resolutionData
  streamConfiguration, downScaleFactor
  regSetting        (x several, 3840 bytes each, one per resolution)
```

`laneAssign` (id 23) is present but its value is **0** - i.e. the vendor does not
override it here, so it does not settle the data-lane question for this module.

The `regSetting` blocks are the per-resolution register lists, but their payload
offsets resolve into the tree region itself rather than into a flat register array, so
a straightforward walk does not recover {addr, value} pairs.  Decoding them needs the
proper Chromatix node semantics, which this project does not have.  This is recorded as
an open lead rather than something that was used.

## The runtime trace confirms two CSID paths were live

`d_tele.txt` is an IRQ trace from a working vendor session:

```
7943  cam_ife_csid_irq            (CSID 1)
     3426 CSID 0 interrupts / 4517 CSID 1 interrupts
 389  cam_vfe_bus_handle_vfe_out_done_bottom_half
 109  cam_vfe_camif_handle_irq_bottom_half
  34  IFE0 / 29 IFE1 references
 100  cam_cdm_write_hw_reg
```

So the vendor stack really does run both IFE0/CSID0 and IFE1/CSID1 in one session, which
is consistent with the topology already assumed in the device tree.  The trace does not
contain CSIPHY programming (the CDM writes it shows are VFE bus writes at offsets
0x50/0x54/0x58), and it names neither sensor nor resolution, so it does not identify
which CSID the tele module used.

## Nothing here changes the conclusion

The artifacts add context but no new register-level facts.  The camera situation is
unchanged from `reports/WHERE-IT-STANDS.md`: every software layer is verified, and the
remaining fault needs physical access.
