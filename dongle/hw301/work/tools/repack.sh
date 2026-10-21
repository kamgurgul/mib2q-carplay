#!/usr/bin/env bash
# Repack a modified rootfs tree into an HW301 OTA tar (app.img + appmd5sum.txt).
#
#   repack.sh <rootfs_dir> <out_versioncode>
#
# Produces  work/out/hw301_v<code>.tar  containing:
#     app.img        xz squashfs of <rootfs_dir>  (block size 262144, all-root)
#     appmd5sum.txt  md5 of app.img  (what UpdateServer checks before dd)
#
# The dongle's UpdateServer untars this, verifies md5(app.img)==appmd5sum.txt,
# then dd's app.img onto /dev/by-name/app. There is NO signature, so a matching
# md5 is sufficient. The image MUST fit the app partition (>= the stock image).
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"          # dongle/
rootfs="${1:?usage: repack.sh <rootfs_dir> <versioncode>}"
code="${2:?usage: repack.sh <rootfs_dir> <versioncode>}"
out="$here/work/out"; mkdir -p "$out"
img="$out/app.img"; md5f="$out/appmd5sum.txt"

mksquashfs "$rootfs" "$img" -comp xz -b 262144 -noappend -all-root -no-xattrs -no-progress >/dev/null
# md5sum may be gmd5sum (coreutils) or `md5 -q` (BSD/macOS). Match the vendor file:
# 32 hex digits + trailing newline (device does `cat appmd5sum.txt|awk '{printf $1}'`).
if command -v md5sum >/dev/null; then md5sum "$img" | awk '{print $1}' > "$md5f"
else md5 -q "$img" | tr -d '\n' | sed 's/$/\n/' > "$md5f"; fi

# app partition (GPT on the SPI-NOR, 0x7e0000-0xea0000) must hold this image.
# A larger image would run into UDISK, so refuse to pack it.
part=$((0x6c0000))
now=$(stat -f%z "$img")
echo "app.img  = $now bytes  (app partition = $part)"
[ "$now" -le "$part" ] || { echo "ERROR: image larger than the app partition; not packing"; exit 1; }

# Build the OTA tar with a LINUX tar (clean ustar: app.img then appmd5sum.txt,
# no AppleDouble ._* and no PaxHeader/* members). macOS bsdtar injects those and
# the dongle's busybox `tar -xvf` chokes on them, aborting the flash. Run tar in
# the alpine container against the same output dir so the bytes match the vendor.
rm -f "$out/hw301_v${code}.tar"
docker run --rm -v "$out":/o -w /o alpine:3.20 \
  tar -cf "hw301_v${code}.tar" app.img appmd5sum.txt
test -f "$out/hw301_v${code}.tar" || { echo "ERROR: tar step failed"; exit 1; }
members=$(docker run --rm -v "$out":/o -w /o alpine:3.20 tar tf "hw301_v${code}.tar" | tr '\n' ' ')
case "$members" in
  "app.img appmd5sum.txt "* ) : ;;
  * ) echo "ERROR: unexpected tar members: [$members]"; exit 1 ;;
esac
echo "md5      = $(cat "$md5f")"
echo "tar      = $out/hw301_v${code}.tar"
echo "members  = $members"
