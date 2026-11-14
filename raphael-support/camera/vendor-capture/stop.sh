#!/system/bin/sh
# Stop capture, save the trace, drop the probes.
OUT=/sdcard/camcap
TRACE=""
for t in /sys/kernel/tracing /sys/kernel/debug/tracing; do
  [ -d "$t" ] && TRACE=$t && break
done
[ -z "$TRACE" ] && { echo "no tracer"; exit 1; }
echo 0 > $TRACE/tracing_on
cp $TRACE/trace $OUT/cam_trace.txt 2>/dev/null
echo 0 > $TRACE/events/kprobes/enable 2>/dev/null
echo > $TRACE/kprobe_events 2>/dev/null
dmesg > $OUT/dmesg.txt 2>/dev/null
logcat -d -v time > $OUT/logcat.txt 2>/dev/null
echo "trace_lines=$(wc -l < $OUT/cam_trace.txt 2>/dev/null)"
echo SAVED
