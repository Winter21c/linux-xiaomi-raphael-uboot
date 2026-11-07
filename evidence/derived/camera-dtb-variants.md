# Camera DTB variants used during the bring-up

| md5 | file | content | boot | sensors |
|---|---|---|---|---|
| f5c79245 | cam-full9 / `sm8150-xiaomi-raphael-camera.dtb` (older) | camera nodes + CCI/MCLK pinctrl, no PMIC rail flags | ok | 0x54 only |
| 747d08fd | cam-clean / `sm8150-xiaomi-raphael-camera.dtb` (current) | + `vs5-supply`/`vl17-supply` on both sensor nodes | ok | 0x1a, 0x54, 0x36, 0x21 |
| e3880d0f | cam-B / `sm8150-xiaomi-raphael-camera-ov8856-0x36.dtb` | single sensor node, tele rails, address 0x36 | ok | driver binds, chip id accepted |
| 88eb7469 | newcam | cam-B + CAMSS-side `remote-endpoint` + sensor-endpoint phandle (2-property diff) | ok | load wedges userspace |
| (built) | variantC | CAMSS-side link only (sensor-side link removed) | untested | - |

`regulator-always-on` audit of every variant: only `vreg_s4a_1p8`, `ts_avdd_3p3`
and `ts_vddio_1p8` (base board).  Any camera rail with those flags = unbootable
phone (confirmed twice).
