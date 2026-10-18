#!/usr/bin/env bash
# Cross-compile for the HW301 dongle (ARM musl hard-float, Linux 4.9).
# Runs the muslcc gcc 11 toolchain in Docker, with dongle/ mounted at /src.
#
#   xcc.sh gcc  -shared -fPIC work/src/foo.c -o work/out/foo.so
#   xcc.sh objdump -p work/out/foo.so
#   xcc.sh sh -c '...'                      # arbitrary toolchain command
#
# Paths are relative to dongle/ (the mount root). Toolchain is unprefixed
# (gcc/g++/ld/objdump/strip/readelf all target arm-linux-musleabihf).
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"   # dongle/
IMG=muslcc/x86_64:arm-linux-musleabihf
exec docker run --rm --platform linux/amd64 \
  -v "$here":/src -w /src \
  -e PATH="/bin:/sbin:/usr/bin:/usr/sbin:/arm-linux-musleabihf/bin" \
  "$IMG" "$@"
