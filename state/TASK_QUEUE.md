# Task queue (P0 highest)

| id | prio | status | task |
|----|------|--------|------|
| A1 | P1 | done | SDSP sensors: build sensor tree, hexagonrpcd -R, libssc + iio-sensor-proxy; accel/light/prox verified |
| A2 | P1 | done | NFC PN553: DT overlay + dtbo-loader, nxp-nci bound, DEV_UP/START_POLL ok |
| A3 | P1 | done | PM8150B haptics: overlay + driver on SPMI USID3 0xc000; vibration verified by user |
| A4 | P1 | partial | Audio: speaker OK, headphone OK, jack detection OK, mic capture (ALSA) OK; PipeWire/WirePlumber does not expose the card |
| A5 | P1 | partial | **Camera.** DONE: camss on SM8150 (media0/14 video/22 subdevs), CCI both masters + upstream HALT_ACK fix, CCI/MCLK pinctrl, rails (viodd/BOB/S8/L17, SMPS5 for the wide module), OV8856 binds at 0x36 with chip-id verified. OPEN: sensor not in the media graph — the CAMSS-side `remote-endpoint` DTB boots but loading camss+ov8856 freezes userspace (round 19); phone needs a power cycle. NEXT: variant C (CAMSS-side link only), reversed load order, then instrument the notifier |
| A5.1 | P1 | blocked | Camera media graph: chip id verified but the async link wedges userspace; needs the power cycle then variant C. Blocker: device frozen, only a physical power cycle clears it |
| A5.2 | P2 | open | IMX586 (wide, CCI0 m0 @0x1a): no mainline driver; port one and wire `vs5-supply`+`vl17-supply` |
| A5.3 | P2 | open | Identify 0x21 on CCI0 master1 (returns the same data as 0x36) and S5K3L6/S5K3T2 addresses with the stock-DT rail map |
| A5.4 | P2 | open | Streaming: once the sensor joins the graph, set formats with a small V4L2 ioctl tool (media-ctl/v4l2-ctl are not installed) and capture from msm_vfe2_rdi* |
| A6 | P2 | open | GNSS: gnss.ko present, no DT node/device |
| A7 | P2 | open | Fingerprint (Goodix) - no mainline driver |
| A8 | P2 | done | fwupd seccomp_attach_filter kernel Oops identified as cause of a system freeze; fwupd masked |
| A9 | P3 | open | Land everything in the u-boot repo (scripts, module sources, overlay, docs) |
