#!/bin/bash
# Try every plausible CSIPHY lane permutation and report whether CSI-2 traffic
# appears (csid0 / csiphy2 interrupt counters) — no reboot required.
M=/dev/media0; W=1640; H=1232; C=0x300a

setup() {
  modprobe -r qcom-camss 2>/dev/null
  modprobe qcom-camss "camss_lane_override=$1" 2>/dev/null || insmod /root/camss-final/qcom-camss.ko "camss_lane_override=$1" 2>/dev/null
  sleep 3
  insmod /root/ovcomb/ov8856.ko 2>/dev/null
  sleep 3
}

run_variant() {
  local lanes=$1 label=$2
  setup "$lanes"
  local csid0=""; for s in /dev/v4l-subdev*; do n=$(cat /sys/class/video4linux/$(basename $s)/name 2>/dev/null); [ "$n" = msm_csid0 ] && csid0=$s; done
  local vfe0=""; for s in /dev/v4l-subdev*; do n=$(cat /sys/class/video4linux/$(basename $s)/name 2>/dev/null); [ "$n" = msm_vfe0_rdi0 ] && vfe0=$s; done
  for p in 0 1; do
    /tmp/mcam /dev/v4l-subdev22 fmt $p $W $H $C >/dev/null 2>&1
    /tmp/mcam /dev/v4l-subdev2 fmt $p $W $H $C >/dev/null 2>&1
    /tmp/mcam "$csid0" fmt $p $W $H $C >/dev/null 2>&1
    /tmp/mcam "$vfe0" fmt $p $W $H $C >/dev/null 2>&1
  done
  /tmp/mcam $M enable 7 1 13 0 1 >/dev/null 2>&1
  /tmp/mcam $M enable 13 1 37 0 1 >/dev/null 2>&1
  /tmp/mcam /dev/video2 mfmt $W $H pgAA >/dev/null 2>&1
  local before=$(grep msm_csid0 /proc/interrupts | awk '{print $2}')
  timeout 12 /tmp/mcam /dev/video2 mstream 1 /root/lane.raw >/dev/null 2>&1
  local csid=$(grep msm_csid0 /proc/interrupts | awk '{print $2}')
  local phy=$(grep msm_csiphy2 /proc/interrupts | awk '{print $2}')
  local bytes=$(stat -c %s /root/lane.raw 2>/dev/null || echo 0)
  echo "RESULT $label (0x$(printf %03x $lanes)): csid0_irq=$before->$csid csiphy2_irq=$phy frame_bytes=$bytes"
}

for v in "0x210" "0x120" "0x201" "0x102" "0x021" "0x012" "0x010" "0x001" "0x100"; do
  run_variant $v "$v"
done
