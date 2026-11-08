# camera_peek

A throwaway module that `ioremap`s camera register blocks and prints every
non-zero word, then refuses to stay loaded (`-ENODEV`) so it can be run again.

Use it **only on blocks that are currently powered**, i.e. while `qcom-camss` holds
the CAMCC GDSCs during a stream.  Reading a clock-gated Qualcomm block raises an
external abort and panics the phone - accessing `cpas_top` (0xac40000) or `camnoc`
(0xac42000) while idle did exactly that during bring-up.

Measured with it:

* every camera block reads back 0 while idle (the blocks are power collapsed),
* during a stream VFE0 holds real content (`hw_version = 0x10020000`) while
  CSID0 still reads 0 and drops writes (`CORE_CTRL_0`: wrote 0x211, read 0).
