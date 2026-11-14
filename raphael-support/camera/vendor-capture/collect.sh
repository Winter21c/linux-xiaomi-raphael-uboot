#!/system/bin/sh
# Runs ON the vendor device (adb shell su -c 'sh collect.sh'). Idempotent.
# Writes everything into /sdcard/camcap/.
OUT=/sdcard/camcap
mkdir -p $OUT
TRACE=""
for t in /sys/kernel/tracing /sys/kernel/debug/tracing; do
  [ -d "$t" ] && TRACE=$t && break
done
echo "tracer=$TRACE" > $OUT/env.txt
echo "kernel=$(uname -r)" >> $OUT/env.txt
id >> $OUT/env.txt
getenforce >> $OUT/env.txt 2>/dev/null

# live device tree
cat /sys/firmware/fdt > $OUT/vendor.dtb 2>/dev/null && echo "dtb=$(wc -c < $OUT/vendor.dtb)" >> $OUT/env.txt

# symbols we intend to hook
grep -E " (cam_io_w_mb|cam_io_w_mb_offset_val_block|cam_io_r_mb|cam_csiphy_reg_dump)$" \
    /proc/kallsyms > $OUT/kallsyms_hooks.txt 2>/dev/null
grep -E "csiphy|csid|vfe" /proc/kallsyms > $OUT/kallsyms_cam.txt 2>/dev/null

# module parameters that raise the vendor log level
ls /sys/module/camera/parameters/ > $OUT/cam_params.txt 2>/dev/null
for f in /sys/module/camera/parameters/*; do
  [ -f "$f" ] && printf "%s=%s\n" "$(basename $f)" "$(cat $f 2>/dev/null)" >> $OUT/cam_params.txt 2>/dev/null
done

# debugfs camera trees
ls -R /sys/kernel/debug/camera > $OUT/debugfs_camera.txt 2>/dev/null
ls /sys/kernel/debug/ > $OUT/debugfs_root.txt 2>/dev/null

if [ -n "$TRACE" ]; then
  # clear any previous probes
  echo > $TRACE/kprobe_events 2>/dev/null
  echo 'p:camw cam_io_w_mb a0=%x0 a1=%x1' >> $TRACE/kprobe_events 2>>$OUT/kprobe.err
  echo 'p:camwb cam_io_w_mb_offset_val_block a0=%x0 a1=%x1 a2=%x2' >> $TRACE/kprobe_events 2>>$OUT/kprobe.err
  echo 'p:camr cam_io_r_mb a0=%x0 a1=%x1' >> $TRACE/kprobe_events 2>>$OUT/kprobe.err
  echo 1 > $TRACE/events/kprobes/enable 2>>$OUT/kprobe.err
  cat $TRACE/available_filter_functions 2>/dev/null | grep -c csiphy > $OUT/filter_count.txt
  echo "probes:" >> $OUT/env.txt
  cat $TRACE/kprobe_events >> $OUT/env.txt 2>/dev/null
fi
echo READY
