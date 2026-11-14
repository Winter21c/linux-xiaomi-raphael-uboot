# Vendor capture runbook (MIUI + root, one shot)

Goal: capture the **exact register writes** the stock camera driver performs while a
real camera session is running, so the mainline driver can be matched to it.

## Why this works

The vendor module keeps its symbols and funnels every MMIO write through exported
helpers:

```
cam_io_w_mb
cam_io_w_mb_offset_val_block
cam_io_w_mb_same_offset_block
cam_io_r_mb
```

`nm camera.ko` shows them as `T` (global), so `kprobe_events` can hook them and fetch
their arguments.  That yields (address, value) pairs for CSIPHY (lane *and* global
page), CSID, VFE, CDM and CAMCC - exactly what we could not extract statically.

## Preparation on the phone (user)

1. flash MIUI, enable developer options, USB debugging
2. give the `shell` user root (Magisk / KernelSU / userdebug `su`)
3. keep the screen unlocked and the phone awake (`svc power stayon true`)
4. `adb shell su -c 'id'` must print `uid=0`

## Collection steps (agent)

```sh
adb shell su -c 'cat /proc/kallsyms | grep -E "cam_io_w_mb|cam_io_r_mb"'   # confirm
adb shell su -c 'cat /sys/firmware/fdt > /sdcard/vendor.dtb'               # live DT
adb shell su -c 'ls /sys/kernel/tracing /sys/kernel/debug/tracing'
```

Then, with the tracer path known (`TRACE`):

```sh
# 1. list available events
ls $TRACE/events/kprobes 2>/dev/null

# 2. add probes: dump the two argument registers of every helper
echo 'p:camw cam_io_w_mb a0=%x0 a1=%x1'          >> $TRACE/kprobe_events
echo 'p:camwb cam_io_w_mb_offset_val_block a0=%x0 a1=%x1 a2=%x2' >> $TRACE/kprobe_events
echo 'p:camr cam_io_r_mb a0=%x0 a1=%x1'          >> $TRACE/kprobe_events

# 3. enable and start a camera session (open the stock camera app)
echo 1 > $TRACE/events/kprobes/enable
echo 1 > $TRACE/tracing_on
# ... open camera, let the preview run 10 s, take a photo ...

# 4. stop and save
echo 0 > $TRACE/tracing_on
cat $TRACE/trace > /sdcard/cam_trace.txt
```

`/sys/firmware/fdt` gives the live vendor device tree (the real camera node
addresses, lanes and rates - something we previously had to guess).

## What to capture

| scenario | why |
|---|---|
| camera app closed, then opened (preview) | the full power-up + PHY + CSID + VFE sequence |
| take a photo | confirms the full capture path |
| switch to 2x (tele) | the OV8856 path, the one mainline is trying to drive |
| switch to front camera | the pop-up motor + S5K3T2 path |

## Safety

* only kprobes - no `devmem` reads of unpowered camera blocks (that is what crashed
  the mainline kernel twice);
* do not unload vendor modules;
* keep a copy of every capture on the host as soon as it is pulled.

## Fallbacks if kprobe_events is unavailable

1. `ls -R /sys/kernel/debug/camera` - the vendor driver may expose register dumps
   through debugfs; if so, dumping them while streaming is enough on its own.
2. `ls /sys/module/camera/parameters/` - raising the vendor log level often makes the
   driver print every register write into `dmesg`/`logcat` (`cam_io` debug logs).
3. If neither works, the remaining option is a read-only snapshot through
   `/dev/mem` **only while the camera is streaming** (all camera blocks powered), never
   when idle.
