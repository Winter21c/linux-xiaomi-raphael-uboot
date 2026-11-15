# Round 87: the sensor is verified end to end and the PHY still sees nothing

## The scan is now trustworthy

Round 86's probe ran inside `csiphy_1_1_lanes_enable`, which happens **before** the
sensor's stream-on (the pipeline brings subdevices up upstream: vfe -> csid -> csiphy
-> sensor), so "no register changed" was expected and proved nothing.  There is now a
`raphael_scan_now` parameter that samples the whole PHY window twice, 150 ms apart, at
whatever moment it is written.  Triggering it four seconds into an active stream:

```
RAPHAEL SCANNOW csiphy2 changed:
```

Still nothing, in both the 1640x1232 and the 3280x2464 mode.  Every register in
0x000..0x83c is bit-identical across the two samples while the sensor is streaming.

## The sensor side is now fully verified

The probe reads the registers the mode table programmed, after stream-on:

```
0100=01  mode select = streaming
0300=00 0302=4b 0304=03 0306=01        PLL
300a=00 300b=88                        chip id 0x00885a (24-bit, matches the driver)
300e=20                                MIPI enable, identical to the mode table
3015=84 3018=32 3021=23                2-lane MIPI configuration, all match
4800=44 4837=16                        MIPI lane control, match
```

Every value equals what `lane_2_mode_1640x1232` asked for.  The sensor accepted the
whole configuration, and the chip identity is confirmed: mainline defines the ID as
`0x00885a` read as a 24-bit value from 0x300a, and that is exactly what the part
returns.

So on the transmit side: identity confirmed, powered (bob 3.03 V, viodd 1.8 V, vdig
1.35 V), clocked (MCLK1 19.2 MHz on a muxed `cam_mclk` pad), reset sequenced, mode and
MIPI registers written and read back correct, streaming mode entered.

On the receive side: CSID at 0xacb3000 answering and proven able to receive (its own
test generator yields `rxirq=0x004000ff` and VFE interrupts), CSIPHY programmed to the
vendor tables with readback matching, `titan_top_gdsc` on, clocks at 400/300/400 MHz -
and not a single register transition.

## What that means

The receiver is correct and receives nothing.  The transmitter is correct and (as far
as any register can show) transmits.  Between them there is only the flex cable, the
connector and the module's MIPI section - none of which software can inspect, and the
handset cannot be opened in this session.

This is as far as evidence can be pushed without physical access.
