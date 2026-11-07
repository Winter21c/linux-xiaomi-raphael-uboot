#!/bin/sh
# Build a qcom-camss module with SM8150 support against the *running* kernel.
#
# Works on any distribution: only the kernel headers for $(uname -r) are needed.
#
#   ./build-camss.sh                    # inside the device
#   KDIR=/path/to/build ./build-camss.sh
#
# The kernel of the Redmi K20 Pro image is built with clang + ThinLTO, so
# out-of-tree modules need a wrapper that strips the clang-only flags; see
# ../../tools/gcc-noclang and CC= below.
set -e

SRC=${1:-/tmp/camss}
KREL=$(uname -r)
KDIR=${KDIR:-/lib/modules/$KREL/build}
CC=${CC:-}
HERE=$(cd "$(dirname "$0")" && pwd)

if [ ! -d "$SRC" ]; then
	echo "usage: $0 [<camss source dir>]" >&2
	echo "fetch it with:" >&2
	echo "  git clone --depth 1 -b raphael-v7.3.0 https://github.com/GengWei1997/linux" >&2
	echo "  cp -r linux/drivers/media/platform/qcom/camss $SRC" >&2
	exit 1
fi

echo "==> applying SM8150 patch"
# the patch paths are kernel-relative (b/drivers/media/platform/qcom/camss/x.c),
# so strip six components to apply it inside the copied camss directory
if [ ! -e "$SRC/.patched" ]; then
	patch -d "$SRC" -p6 --forward < "$HERE/patches/0001-camss-add-sm8150-support.patch"
	touch "$SRC/.patched"
fi

echo "==> building against $KDIR"
if [ -n "$CC" ]; then
	make -C "$KDIR" M="$SRC" CC="$CC" modules
else
	make -C "$KDIR" M="$SRC" modules
fi
ls -l "$SRC"/qcom-camss.ko
