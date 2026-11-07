# Why the async link freezes userspace (hypotheses, not conclusions)

Observation: with the CAMSS-side `remote-endpoint` present, `insmod qcom-camss`
is fine, and `insmod ov8856` (which triggers `v4l2_async_register_subdev_sensor()`
and therefore the notifier) freezes userspace.  The kernel stays alive (ping and
TCP/22 answer), so this is not a panic.

Code read (`camss.c` in /root/camss-final on the device):

* `camss_subdev_notifier_bound()` only sets `csiphy->cfg.csi2` and
  `subdev->host_priv` - no locks, so it cannot be the deadlock itself.
* `camss_subdev_notifier_complete()` walks `v4l2_dev->subdevs`, creates one pad
  link per CSIPHY host and ends with `v4l2_device_register_subdev_nodes()`.
  Both take media-device/v4l2-device mutexes.

Hypotheses, cheapest to test first:

1. **Module order** - the freeze may depend on which side registers first.  Test
   `ov8856` before `qcom-camss`.
2. **Sensor-side link not needed** - the async core only needs the consumer's
   endpoint to point at the provider.  Variant C drops the sensor-side
   `remote-endpoint` and keeps only the CAMSS-side one.
3. **`v4l2_device_register_subdev_nodes()` creating device nodes while the
   sensor's probe still holds its lock** - if so, the fix is to defer node
   creation (`V4L2_SUBDEV_FL_HAS_DEVNODE` timing) or to bind the sensor from a
   work queue.
4. **Not the notifier at all** - check `journalctl -k` from the wedged boot for
   the modem WCSS crash (the old wedge looked identical).  This must be ruled out
   before blaming the camera path.

All three experiments are prepared in `tools/` of this session's workspace:
`wdt` (arm the hardware watchdog so a freeze self-reboots) and `camtest.sh`
(order A/B + watchdog), `v4ltool` (media topology/formats/stream, because the
image has neither media-ctl nor v4l2-ctl).

## Addendum (round 22): rule out the modem before blaming the camera

The wedge signature is **identical** to the modem WCSS crash loop of rounds 4-5:
ping answers, TCP/22 accepts, every SSH session hangs in authentication, and the
kernel keeps running (no panic).  So an experiment that freezes the phone is not
proof that the async link is at fault.

Decisive check, available after the next boot from the journal of the *previous*
boot:

```sh
journalctl -k -b -1 | grep -iE 'wcss|ccpm|srpll|remoteproc|crash detected|rcu|soft lockup|hung task|qcom_wdt'
```

* WCSS/CCPM/SRPLL lines immediately before the end -> the modem crash loop came
  back (the `raphael-stop-modem.service` guard did not hold that boot),
* only `camss`/`ov8856`/`media` lines -> the async link is the culprit,
* `qcom_wdt` or `soft lockup` -> the watchdog fired and rebooted by itself.

`tools/next-session.sh` now has two stages for exactly this:
`post-mortem` (prints the previous boot's modem and camera lines) and
`preflight` (refuses to run camera tests unless `remoteproc1` is `offline`).

Note on the second possibility: an earlier round saw the same freeze *without*
any camera change, purely from the modem loop, and the guard service had been
verified active in the boots where it worked.  Whether it was also active in this
wedged boot is **unknown** - that is precisely what `post-mortem` answers.
