# VFE reset timeout: what is fixed and what is still open

Symptom, on a stable system (mpss disabled), after the pipeline
`ov8856 -> csiphy2 -> csid2 -> vfe2_rdi0` is configured and linked:

```
/dev/video6: 1640x1232 fourcc=UYVY planes=1 size=4040960
STREAMON: Input/output error
qcom-camss: VFE reset timeout
qcom-camss: Failed to power up pipeline: -5
```

The VFE IRQ counter stays at 0 (`/proc/interrupts`), so the reset-ack interrupt
never arrives.

## Fixed on the way there

| problem | evidence | fix |
|---|---|---|
| 32-bit DMA mask forced swiotlb bounce buffering | `swiotlb buffer is full (sz: 2097152 bytes)` on `REQBUFS` | `dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64))`, buffers allocate now |
| CAMCC power domains attached in the wrong order | driver uses `dev_pm_domain_attach_by_id(genpd_num - 1)` and expects TITAN_TOP last; DTB had `"top","ife0","ife1"` | DTB reordered to `ife0, ife1, top`, VFE0/VFE1 got `has_pd = true` + `pd_name` |

## Open: the video node seems bound to a lite VFE

Streaming `/dev/video0` - whose sysfs name is `msm_vfe0_video0` - enables the
clock pair of a **VFE-lite** instance:

```
clk[6/8] ife_lite: prepare_enable
clk[7/8] ife_lite_axi: prepare_enable
qcom-camss: VFE reset timeout
```

while `vfe_res_sm8150[0]` (VFE0) lists `ife0`/`ife0_axi`.  Both names are resolved
through `clock[i].name` (the resource string), so the powered VFE really is a lite
entry.  Either the video node's `vfe->id` does not match the index used for
`camss->res->vfe_res[]`, or the media-entity names and the video-node names are
built from different indices.

Next checks, cheapest first:

1. enable the clock print for `vfe->id` and `res->reg[0]` in `camss_vfe_probe()`
   and stream `/dev/video0` and `/dev/video6` in turn - the printed reg name
   (`vfe0` vs `vfe_lite0`) settles which instance is being powered,
2. if `vfe->id` is wrong, look for the index source in `camss_vfe_probe()` /
   `camss_create_video_device()` (the video node name uses `vfe->id` too, so a
   mismatch means the name and the resource disagree),
3. the second anomaly to explain afterwards: `mfmt` with `pgAA` (the packed
   Bayer form of the sensor's `0x300a` = MEDIA_BUS_FMT_SGRBG10_1X10) comes back
   as `UYVY`, so the video node's format negotiation is falling back to its
   default rather than matching the linked subdev.

Reproduce with `tools/mcam.c` (build on the device) - there is no media-ctl or
v4l2-ctl in the image.
