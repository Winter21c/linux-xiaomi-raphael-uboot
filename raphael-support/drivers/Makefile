# Build every Raphael out-of-tree module against the running kernel.
#
#   make                 # build with the kernel's default compiler
#   make CC=gcc-noclang  # needed on clang+ThinLTO kernels (see README)
#   sudo make install    # copy into /lib/modules/$(uname -r)/extra/raphael
KDIR ?= /lib/modules/$(shell uname -r)/build
MODS := input/pm8150b-haptics of/dtbo-loader

all:
	for m in $(MODS); do $(MAKE) -C $(KDIR) M=$(PWD)/$$m modules || exit 1; done

clean:
	for m in $(MODS); do $(MAKE) -C $(KDIR) M=$(PWD)/$$m clean; done

install: all
	mkdir -p /lib/modules/$(shell uname -r)/extra/raphael
	for m in $(MODS); do cp $$m/*.ko /lib/modules/$(shell uname -r)/extra/raphael/; done
	depmod -a

.PHONY: all clean install
