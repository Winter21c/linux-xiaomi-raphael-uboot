# Task queue (P0 highest)

| id | prio | status | task |
|----|------|--------|------|
| A1 | P1 | done | SDSP sensors: build sensor tree, hexagonrpcd -R, libssc + iio-sensor-proxy; accel/light/prox verified |
| A2 | P1 | done | NFC PN553: DT overlay + dtbo-loader, nxp-nci bound, DEV_UP/START_POLL ok |
| A3 | P1 | done | PM8150B haptics: overlay + driver on SPMI USID3 0xc000; vibration verified by user |
| A4 | P1 | partial | Audio: speaker OK, headphone OK, jack detection OK, mic capture (ALSA) OK; PipeWire/WirePlumber does not expose the card |
| A5 | P1 | blocked | Camera: no camss/csiphy/cci DT nodes in boot DTB and no sensor drivers for IMX586/S5K3L6/OV8856/S5K3T2; needs kernel source work (raphael-v7.3.0 available on GitHub) |
| A6 | P2 | open | GNSS: gnss.ko present, no DT node/device |
| A7 | P2 | open | Fingerprint (Goodix) - no mainline driver |
| A8 | P2 | done | fwupd seccomp_attach_filter kernel Oops identified as cause of a system freeze; fwupd masked |
| A9 | P3 | open | Land everything in the u-boot repo (scripts, module sources, overlay, docs) |
