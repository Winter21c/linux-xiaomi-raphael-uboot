# Round 70b: a runtime knob for the PHY wiring, and the first two PHYs

## `raphael_force_phy`

The device tree says the tele module sits on `csiphy2`, but that mapping was never
proven against the board.  `camss-csid.c`/`camss.c` now accept

```
insmod qcom-camss.ko raphael_force_phy=N
```

which moves the sensor's `host_priv` (and therefore the media link) to any CSIPHY
index at probe time.  Verified working:

```
qcom-camss ac65000.camss: raphael: sensor moved from csiphy2 to csiphy1
```

The `msm_csiphy1 -> msm_csid0 -> msm_vfe0_rdi0` path then comes up with
`rx_cfg0: wrote=0x00100211 readback=0x00100211 (lane_cnt=2 assign=0x21 phy=1)`.

## Results so far

| forced PHY | csiphy interrupts | csid0 interrupts | bytes |
|---|---|---|---|
| 2 (as in the DT) | 1 per stream | 0 | 0 |
| 1 | 1 per stream | 0 | 0 |

Both behave identically: the PHY raises a single interrupt when the stream starts
and then nothing, and the CSID never sees a packet.  That is the signature of a PHY
that is programmed correctly but receives no CSI-2 signal at all, not of a wrong
lane mapping or a wrong PHY index.

## Weight of evidence now

For the sensor:

* three rails enabled (avdd, dovdd from camera_viodd_ldo 1.8 V, dvdd from
  camera_vdig_tele 1352 mV);
* MCLK `cam_cc_mclk1_clk` at 19.2 MHz, consumer `3-0036 xvclk`;
* chip id readable over CCI, so it is out of reset and the I/O rail is good;
* driver programmes `mode 1640x1232 vts=1514 link_freq_idx=1`, `0x4800 = 0x44`
  (MIPI output enable) and `0x0100 = 0x01` (stream on).

For the receive side: CSID proven able to produce traffic (test generator gives
`rxirq=0x004000ff` and VFE interrupts), PHY proven programmable and error-free.

So the gap is between the sensor's MIPI pads and the SoC's CSI input.

## Next

1. finish the sweep (`raphael_force_phy=0` and `=3`);
2. read the sensor's own PLL and lane registers back over I2C after stream-on
   (`0x0300`-`0x030f` PLL, `0x4800`/`0x4801`/`0x4805` lane setup) - if the PLL did
   not lock the sensor stays silent while looking perfectly healthy over I2C;
3. check the `link_freq_idx=1` choice (360 MHz) against what the 1640x1232 mode
   actually needs; a wrong PLL configuration is exactly this failure mode.
