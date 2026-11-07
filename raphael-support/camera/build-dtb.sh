#!/bin/bash
# Rebuild /boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb with the camera nodes.
#
#   ./build-dtb.sh <linux source dir> [out.dtb]
#
# The script compiles the DTS directly with cpp + dtc (no kernel build needed)
# and verifies that the result contains the camera nodes.
#
# Installing it is a *boot-critical* change: keep the original DTB as a backup
# (U-Boot falls back to its built-in "legacy" DTB when the file is missing, but
# not when the file is corrupt).
set -e

SRC=${1:?usage: $0 <linux source dir> [out.dtb]}
OUT=${2:-$PWD/sm8150-xiaomi-raphael.dtb}
HERE=$(cd "$(dirname "$0")" && pwd)
DTS="$SRC/arch/arm64/boot/dts/qcom/sm8150-xiaomi-raphael.dts"

[ -f "$DTS" ] || { echo "missing $DTS" >&2; exit 1; }

echo "==> applying camera DTS patch"
if ! grep -q "camss@ac6a000" "$DTS"; then
	patch -d "$SRC" -p1 --forward < "$HERE/patches/0002-dts-raphael-camera-nodes.patch"
fi

echo "==> preprocessing"
cpp -nostdinc \
    -I"$SRC/include" \
    -I"$SRC/arch/arm64/boot/dts/qcom" \
    -I"$SRC/scripts/dtc/include-prefixes" \
    -undef -D__DTS__ -x assembler-with-cpp "$DTS" -o /tmp/raphael-cam-pre.dts

echo "==> compiling"
dtc -I dts -O dtb -o "$OUT" /tmp/raphael-cam-pre.dts

echo "==> verifying"
dtc -I dtb -O dts "$OUT" 2>/dev/null | grep -q 'compatible = "qcom,sm8150-camss"' \
	|| { echo "camera nodes missing" >&2; exit 1; }
ls -l "$OUT"
echo "done - copy it to /boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb (keep a backup)"
