#!/bin/bash
# Run ON the device as root, after copying the three v1.1 files next to this script.
#   /root/v11/  camss-csiphy-2ph-1-1.c  camss-csiphy-2ph-1-1.h  csiphy_v1_1_tables.h
# It builds qcom-camss as an external module from a copy of the in-tree sources,
# loads it and runs the tele pipeline, then dumps the diagnostic registers.
set -u
SRC=/lib/modules/$(uname -r)/build
WORK=/root/camss-v11
V11=/root/v11
LOG=/root/v11-test.log
exec > >(tee -a "$LOG") 2>&1

echo "=== $(date) kernel $(uname -r)"

# 1. fresh copy of the in-tree camss sources
rm -rf $WORK; mkdir -p $WORK
cp $SRC/drivers/media/platform/qcom/camss/*.c $WORK/ 2>/dev/null
cp $SRC/drivers/media/platform/qcom/camss/*.h $WORK/ 2>/dev/null
cp $SRC/drivers/media/platform/qcom/camss/Makefile $WORK/ 2>/dev/null
echo "sources: $(ls $WORK/*.c | wc -l) .c files"

# 2. drop in the v1.1 driver and register it
cp $V11/camss-csiphy-2ph-1-1.c $V11/camss-csiphy-2ph-1-1.h $V11/csiphy_v1_1_tables.h $WORK/
sed -i 's|^qcom-camss-objs += camss-csiphy-2ph-1-0.o|qcom-camss-objs += camss-csiphy-2ph-1-0.o\nqcom-camss-objs += camss-csiphy-2ph-1-1.o|' $WORK/Makefile
grep -c "2ph-1-1" $WORK/Makefile

# 3. point the SM8150 csiphy resources at the new ops
sed -i 's|extern const struct csiphy_hw_ops csiphy_ops_2ph_1_0;|extern const struct csiphy_hw_ops csiphy_ops_2ph_1_0;\nextern const struct csiphy_hw_ops csiphy_ops_2ph_1_1;|' $WORK/camss-csiphy.h
python3 - "$WORK/camss.c" <<'PY'
import re,sys
p=sys.argv[1]; s=open(p).read()
i=s.find('csiphy_res_sm8150')
if i<0: print("WARN: csiphy_res_sm8150 not found"); sys.exit(0)
j=s.find('};', i)
seg=s[i:j]
n=seg.count('csiphy_ops_2ph_1_0')
s=s[:i]+seg.replace('csiphy_ops_2ph_1_0','csiphy_ops_2ph_1_1')+s[j:]
open(p,'w').write(s)
print(f"csiphy_res_sm8150: {n} entries switched to 2ph_1_1")
PY

# 4. build
make -C $SRC M=$WORK CC=/usr/local/bin/gcc-noclang LD=/usr/local/bin/ld-nollvm modules 2>&1 | tail -15
[ -f $WORK/qcom-camss.ko ] || { echo "BUILD FAILED"; exit 1; }
echo "built: $(ls -la $WORK/qcom-camss.ko | awk '{print $5}') bytes"

# 5. load and run the tele pipeline
pkill -9 -f mcam 2>/dev/null
modprobe -r qcom-camss 2>/dev/null
modprobe i2c-qcom-cci
insmod $WORK/qcom-camss.ko || exit 1
sleep 2
insmod /root/ovcomb/ov8856.ko 2>/dev/null
sleep 1
echo "subdevs: $(ls /dev/v4l-subdev* 2>/dev/null | wc -l)"

for p in 0 1; do for sd in 22 2 4 8; do
  /tmp/mcam /dev/v4l-subdev$sd fmt $p 1640 1232 0x300a >/dev/null 2>&1
done; done
/tmp/mcam /dev/media0 enable 7 1 13 0 1 >/dev/null 2>&1
/tmp/mcam /dev/media0 enable 13 1 37 0 1 >/dev/null 2>&1
/tmp/mcam /dev/video2 mfmt 1640 1232 pgAA >/dev/null 2>&1

dmesg -C
timeout -k 3 8 /tmp/mcam /dev/video2 mstream 1 /root/v11.raw >/dev/null 2>&1
echo "bytes=$(stat -c %s /root/v11.raw 2>/dev/null || echo 0)"
echo "csid0=$(grep msm_csid0 /proc/interrupts | awk '{print $2}')"
dmesg | grep -iE "csiphy|v1.1|SOF|error" | tail -8
echo "=== DONE $(date)"
