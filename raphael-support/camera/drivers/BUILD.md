# Building the camera drivers against the running kernel

These sources are the **exact files that are running on the handset** as of round 79.
They are kept here verbatim so the build can be reproduced from the repository alone,
since the out-of-tree trees live only in `/root` on the phone.

| file | what it is |
|---|---|
| `camss.c` | camss core: SM8150 resources, GDSC keep-alive, `raphael_force_phy` knob |
| `camss-csid.c` | CSID core: the base-address fix, test-generator forcing, probes |
| `camss-csid-gen2.c` | CSID 175 register map (vendor offsets), `cgc_ovd`, `rx_cfg0` readback |
| `camss-csiphy.c` | CSIPHY core |
| `camss-csiphy-2ph-1-1.c` | the raphael D-PHY v1.1 driver (see `../VERIFICATION.md`) |
| `csiphy_v1_1_tables.h` | the 77 vendor records (reset, irq, 2ph, four lane tables) |
| `camss-vfe.c` | VFE: lite resources without `ife_lite_axi` |
| `ov8856-raphael.c` | the OV8856 sensor driver with the bring-up probes |

## Toolchain

The kernel is built with clang and LTO (`CONFIG_LTO_CLANG_THIN=y`), and a plain gcc
invocation fails on this tree's `ns_common_types.h`.  Two workarounds are used:

1. `clang-nofilter` - a wrapper that drops the flags the in-tree headers cannot take:

```sh
#!/bin/sh
# /usr/local/bin/clang-nofilter
args=""
skip=0
for a in "$@"; do
    if [ $skip -eq 1 ]; then skip=0; continue; fi
    case "$a" in
        -fms-anonymous-structs|-fexperimental-late-parse-attributes|-fsplit-lto-unit)
            continue ;;
        -mllvm) skip=1; continue ;;
    esac
    args="$args \"$a\""
done
eval exec /usr/lib/llvm-19/bin/clang $args
```

2. the in-tree headers needed three small edits (kept as `.bak` next to them on the
   phone): the `sizeof(struct filename) % 64` assert commented out, `struct ns_common`
   given `ns_id`/`__ns_ref_active`, and `struct ns_tree;` turned into `void *ns_tree;`.

## Build commands

camss (the CSIPHY/CSID/VFE stack):

```sh
mkdir -p /root/camss-v11 && cd /root/camss-v11
make -C /lib/modules/$(uname -r)/build M=$PWD \
     CC=/usr/local/bin/clang-nofilter LD=ld.lld LLVM=1 modules
```

OV8856 sensor:

```sh
mkdir -p /root/ovmod && cd /root/ovmod   # ov8856-raphael.c as ov8856.c + Makefile
make -C /lib/modules/$(uname -r)/build M=$PWD \
     CC=/usr/local/bin/clang-nofilter LD=ld.lld LLVM=1 modules
```

with `Makefile` containing `obj-m += qcom-camss.o` (or `ov8856.o`).

## Loading, and why a reboot is needed between builds

```sh
for m in videodev v4l2-async v4l2-fwnode videobuf2-common videobuf2-memops \
         videobuf2-v4l2 videobuf2-dma-sg v4l2-cci; do modprobe $m; done
modprobe i2c-qcom-cci
insmod /root/camss-v11/qcom-camss.ko
insmod /root/ovmod/ov8856.ko
```

`rmmod qcom-camss` **segfaults** once a stream has been attempted, and the module then
stays busy, so every rebuilt module requires a reboot before it can be tested.  Batch
experiments per boot.

## Pipeline

```sh
M="media-ctl -d /dev/media0"
$M -l '"msm_csiphy2":1->"msm_csid0":0[1]'
$M -l '"msm_csid0":1->"msm_vfe0_rdi0":0[1]'
$M --set-v4l2 '"ov8856 3-0036":0[fmt:SGRBG10_1X10/1640x1232]'
$M --set-v4l2 '"msm_csiphy2":0[fmt:SGRBG10_1X10/1640x1232]'
$M --set-v4l2 '"msm_csiphy2":1[fmt:SGRBG10_1X10/1640x1232]'
$M --set-v4l2 '"msm_csid0":0[fmt:SGRBG10_1X10/1640x1232]'
$M --set-v4l2 '"msm_csid0":1[fmt:SGRBG10_1X10/1640x1232]'
$M --set-v4l2 '"msm_vfe0_rdi0":0[fmt:SGRBG10_1X10/1640x1232]'
v4l2-ctl -d /dev/video2 --set-fmt-video=width=1640,height=1232,pixelformat=pgAA
timeout -s INT 8 v4l2-ctl -d /dev/video2 --stream-mmap --stream-count=3 --stream-to=/tmp/f.raw
```

A `-EPIPE` from the pipeline start means a media-bus format mismatch; a missing link
gives `-ENOLINK`.

## Bring-up knobs added for debugging

| knob | effect |
|---|---|
| `raphael_force_phy=N` | attach the sensor to CSIPHY N at probe time |
| `raphael_lane_mask_11=N` | override the CSIPHY v1.1 lane enable mask |
| `raphael_mipi4800` / `raphael_mipi4801` | write those sensor registers before stream on |
| `camss_lane_override` (1-0 driver only) | not applicable to this PHY |

## Tools

`../tools/cam-mclk-hold.c` holds a CAMCC clock by id (`clk_id=86` is the front camera's
MCLK2) and `../tools/cam-power-hold.c` holds regulators by name
(`supplies=camera_viodd,camera_vdig_front`), so a camera module can be powered and
clocked before any sensor driver exists.
