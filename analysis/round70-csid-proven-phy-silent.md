# Round 70: the CSID is proven functional, nothing arrives from the PHY

## The isolation experiment

The CSID refuses to enable its test generator while a CSIPHY is linked, so the
`csiphy2 -> csid0` link was disabled and `test_pattern` set to 1 (`Incrementing`).
Nothing filled in `csid->phy.en_vc` in that state, so `csid_set_stream()` was given a
forced `en_vc = 1` to make `configure_stream()` walk the port and programme the
generator.

Result while streaming the generator:

```
RAPHAEL TRACE csid0 configure_stream done
csid0 state: en_vc=0x1 lane_cnt=0 lane_assign=0x0 csiphy_id=0 phy_ok=0
csid0 rx: cfg0=0x00000003 rxirq=0x004000ff top=0x00000001
RAPHAEL CSIDPOST core=0x00000000 ctrl=0x00000000 rxirq=0x004000ff rdi0=0x802bf007 stat=0x00000001
msm_vfe0 interrupts: 2
```

Compare with the real sensor attached:

```
csid0 rx: cfg0=0x00200211 rxirq=0x00000000
msm_csid0 interrupts: 0
```

So the CSID can produce traffic and the VFE reacts to it; what never happens is any
CSI-2 activity from the PHY side.  That is the whole remaining problem, and it is
**not** in the CSID any more.

No frame reached the capture buffers even from the generator, which is expected
here: the generator path needs the RDI/DMA plumbing that only a real stream
exercises, and this experiment was only meant to separate the two halves.

## Consequence: the open question is now physical

The OV8856 is powered (three rails), clocked (19.2 MHz MCLK), programmed
(`mode 1640x1232`, `0x4800 = 0x44`, `0x0100 = 0x01`) - and the PHY two blocks away
sees no clock and reports no error at all.

The most likely explanation left is that the tele module is **not wired to
`csiphy2`**.  The "tele = csiphy[2] -> csid[0] -> ife[0]" mapping that this project
has used since the early rounds was never derived from a source that describes the
raphael board wiring - the vendor FDT we extracted does not contain the sensor
nodes at all (they are configured by the CAMX userspace stack, not the device tree).

## Next

1. establish the real mapping: try the sensor on `csiphy0`, `csiphy1` and `csiphy3`
   as well (needs either a DTB with several candidate links, or a driver knob that
   registers the sensor against a chosen PHY);
2. in parallel, look inside the vendor ROM for the CAMX camera configuration that
   names the CSI PHY per physical camera (`camxoverridesettings`, the
   `com.qti.chi.override` XMLs or the sensor XML that carries the CSI id).
