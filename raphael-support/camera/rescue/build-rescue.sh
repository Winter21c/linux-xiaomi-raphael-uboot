#!/bin/bash
# Build a fastboot-bootable rescue image that repairs a wedged raphael install.
#
#   ./build-rescue.sh <rootfs.img> [out.img]
#
# The result is an Android boot image (raw arm64 Image + the phone's own
# initramfs with an extra local-top hook) that, when booted with
#     fastboot boot rescue-boot.img
# restores /boot/dtbs/qcom/sm8150-xiaomi-raphael.dtb from its .bak and deletes
# every /lib/modules/*/extra/raphael/qcom-camss.ko, then reboots.
#
# Why this is needed: if a camera module is installed under /lib/modules, udev
# autoloads it at boot (modalias qcom,sm8150-camss).  A probe that wedges on a
# clock or PHY leaves the CLK framework lock held, which freezes userspace while
# the kernel keeps answering ICMP - SSH accepts TCP but no command completes,
# so the module can not be removed from userspace any more.
set -e

IMG=${1:?usage: $0 <rootfs.img> [out.img]}
OUT=${2:-$PWD/rescue-boot.img}
HERE=$(cd "$(dirname "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

echo "==> reading the nested GPT (4096-byte sectors)"
read ROOT_START BOOT_START < <(python3 - "$IMG" <<'PY'
import struct, sys
f = open(sys.argv[1], 'rb')
f.seek(4096)
hdr = f.read(4096)
entries_lba, num, esize = struct.unpack("<QII", hdr[72:84])
f.seek(entries_lba * 4096)
parts = []
for i in range(num):
    e = f.read(esize)
    if e[:16] == b'\0' * 16:
        continue
    first, last = struct.unpack("<QQ", e[32:48])
    parts.append((first, last))
# p1 = Boot, p2 = Root
print(parts[1][0], parts[0][0])
PY
)
echo "    boot LBA $BOOT_START, root LBA $ROOT_START"

echo "==> extracting the Boot partition (kernel + initramfs)"
dd if="$IMG" of="$WORK/p1.img" bs=4096 skip="$BOOT_START" count=60416 status=none
7z x -y "$WORK/p1.img" -o"$WORK/boot" >/dev/null
python3 - "$WORK/boot/vmlinuz" "$WORK/Image" <<'PY'
import sys, zlib
d = open(sys.argv[1], 'rb').read()
i = d.find(b'\x1f\x8b\x08')
o = zlib.decompressobj(16 + zlib.MAX_WBITS)
img = o.decompress(d[i:])
assert img[56:60] == b'ARMd', "not an arm64 Image"
open(sys.argv[2], 'wb').write(img)
print("    arm64 Image", len(img), "bytes")
PY

echo "==> unpacking the phone initramfs"
mkdir -p "$WORK/ird"
case "$(file -b "$WORK/boot/initrd.img")" in
*Zstandard*) zstd -d -c "$WORK/boot/initrd.img" > "$WORK/initrd.cpio" ;;
*gzip*) zcat "$WORK/boot/initrd.img" > "$WORK/initrd.cpio" ;;
*) cp "$WORK/boot/initrd.img" "$WORK/initrd.cpio" ;;
esac
(cd "$WORK/ird" && cpio -idm --no-absolute-filenames < "$WORK/initrd.cpio" >/dev/null 2>&1)

echo "==> injecting the rescue hook"
install -m 0755 "$HERE/zz-raphael-rescue" "$WORK/ird/scripts/local-top/zz-raphael-rescue"
grep -qx "zz-raphael-rescue" "$WORK/ird/scripts/local-top/ORDER" || \
	echo "zz-raphael-rescue" >> "$WORK/ird/scripts/local-top/ORDER"
(cd "$WORK/ird" && find . | cpio -o -H newc 2>/dev/null | zstd -19 -T0 -q -o "$WORK/rescue-initrd.img")

echo "==> building the boot image"
python3 - "$WORK/Image" "$WORK/rescue-initrd.img" "$OUT" <<'PY'
import sys, struct, hashlib
kernel = open(sys.argv[1], 'rb').read()
ramdisk = open(sys.argv[2], 'rb').read()
PAGE = 4096
cmdline = b"console=ttyMSM0,115200n8 earlycon loglevel=8"
hdr = bytearray(1632)
hdr[0:8] = b"ANDROID!"
for off, val in ((8, len(kernel)), (12, 0x80008000), (16, len(ramdisk)),
                 (20, 0x81000000), (24, 0), (28, 0x80f00000),
                 (32, 0x80000100), (36, PAGE), (40, 0), (44, 0)):
    struct.pack_into("<I", hdr, off, val)
hdr[48:64] = b"raphael-rescue".ljust(16, b"\0")
hdr[64:64 + len(cmdline)] = cmdline
def sha(d, n):
    return hashlib.sha1(struct.pack("<I", n) + d).digest()
hdr[608:640] = sha(kernel, len(kernel)) + sha(ramdisk, len(ramdisk)) + sha(b"", 0) + sha(b"", 0)
out = bytearray(bytes(hdr).ljust(PAGE, b"\0"))
for blob in (kernel, ramdisk):
    out += blob + b"\0" * ((-len(blob)) % PAGE)
open(sys.argv[3], 'wb').write(bytes(out))
print("    wrote", sys.argv[3], len(out), "bytes")
PY

echo "==> done"
echo "Put the phone into fastboot (Volume Down + Power) and run:"
echo "    fastboot boot $OUT"
