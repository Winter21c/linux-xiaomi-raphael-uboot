# Round 69: lane sweep and test-pattern isolation

## A real lane override for the v1.1 driver

`camss_lane_override` lives only in the **1-0** driver, so the round-67 sweep never
touched this PHY.  The 1-1 driver now has its own knob,
`/sys/module/qcom_camss/parameters/raphael_lane_mask_11`, and the log confirms it
takes effect (`RAPHAEL LANE csiphy2 override mask 0x..`).

Swept at runtime, one boot, no rebuild:

| mask | csid0 interrupts | captured bytes |
|---|---|---|
| 0x83 (2 data lanes + clock) | 0 | 0 |
| 0x81 | 0 | 0 |
| 0x87 | 0 | 0 |
| 0x8f | 0 | 0 |
| 0x80 (clock only) | 0 | 0 |
| 0x0f (all data lanes) | 0 | 0 |

So the lane mask is not what is missing.

## The sensor really does enable its MIPI output

Every OV8856 mode table in the driver writes `0x4800 = 0x44`, which sets the MIPI
output enable bits, and the driver's own log confirms the mode it programs:

```
ov8856 3-0036: RAPHAEL SENSOR mode 1640x1232 vts=1514 link_freq_idx=1
```

Together with MCLK at 19.2 MHz, all three rails enabled and `0x0100 = 0x01`, the
sensor side looks complete.

## Test-pattern isolation does not work as-is

`csid_set_test_pattern()` refuses to enable the generator while the CSID is linked
to a CSIPHY:

```c
	if (value && media_pad_remote_pad_first(&csid->pads[MSM_CSID_PAD_SINK]))
		return -EBUSY;
```

Disabling the `csiphy2 -> csid0` link lets `test_pattern=1` stick (verified:
`test_pattern: 1 (Incrementing)`), but the stream then delivers nothing either,
because `csid_configure_stream()` only walks the ports enabled in
`csid->phy.en_vc`, and with no PHY attached `en_vc` stays zero, so the test
generator is never configured.

To use the generator as an isolation test the driver has to force
`en_vc = 1` (or configure the testgen unconditionally) when `tg->enabled`.

## Where this leaves the investigation

Everything up to the CSID is verified working, and the CSID is correctly
addressed and configured; what is missing is any CSI-2 activity arriving from the
PHY.  The next decisive experiment is to make the CSID prove it can produce and
deliver frames on its own (force `en_vc` for the test generator) - that separates
"CSID/VFE broken" from "no data from the sensor/PHY".
