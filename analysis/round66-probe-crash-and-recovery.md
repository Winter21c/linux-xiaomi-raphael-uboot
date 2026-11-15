# Round 66: what broke and what it taught

## A debugging probe of mine oopsed the kernel

I added a register read to `vfe_set_stream()` to find out whether the VFE (and
therefore the whole IFE power domain) answers while streaming.  The probe ran
before the function's own locals were set up in the way I assumed, dereferenced a
NULL and produced:

```
pc : vfe_set_stream+0xa4/0x130 [qcom_camss]
Call trace:
 vfe_set_stream+0xa4/0x130 [qcom_camss] (P)
 call_s_stream+0x40/0xcc [videodev]
 video_start_streaming+0x364/0x44c [qcom_camss]
```

Two lessons:

1. the crash site proves the pipeline **does** reach the VFE's `s_stream`, i.e. the
   media bus format fix from round 65 is real and the graph is now accepted;
2. instrumentation in this driver has to be additive and self-contained; reading
   through a struct pointer that the function has not resolved yet is not safe.

## Recovering a file I corrupted

The script that removed the probe used a wrong index and turned `camss-vfe.c`
into `[A] + [A[2:]] + [probe + B]` - two SPDX headers, two `vfe_set_stream`
definitions.  The original is exactly reconstructible from that shape:

```
recovered = lines[0..1245]        # up to and including the function's opening brace
          + lines[2489..]         # the same brace again, then probe + B
          - the probe block
          - one of the two duplicate braces
```

The rebuilt `camss-vfe.c` compiles cleanly against the installed headers, so the
reconstruction is exact as far as the compiler is concerned.

## State

* `camss-csiphy-2ph-1-1` is verified on hardware (round 65 readback).
* the full pipeline is accepted by the kernel and reaches VFE `s_stream`.
* the CSID still reads back zero, which is what the next round has to attack.
* `linux-source` and the camss working tree live in `/tmp` on the phone, so they do
  not survive a reboot; `camss-v11/` is in `/root` and does.
