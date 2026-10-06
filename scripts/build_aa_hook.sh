#!/bin/bash
#
# Build the Android Auto cluster hook (libaa_cluster_hook.so) for QNX/ARMv7 in Docker.
#
#   ./scripts/build_aa_hook.sh
#
# Source: aa_hook/ (adapted from wasimlhr/mib2q-android-auto-cluster, GPL-3.0-or-later).
# LD_PRELOADed into gal by aa_startup.sh. The interposer set is an exact allowlist
# (aa_hook/aa_hook.exports.map), checked against what lands in .dynsym.
#
# Same image as the other native builds (qnx65-armv7-toolchain, GCC 8.5).
set -e

[ "$#" -eq 0 ] || { echo "usage: ./scripts/build_aa_hook.sh"; exit 2; }

IMG=qnx65-armv7-toolchain:latest
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SONAME=libaa_cluster_hook.so
OUT="$PROJECT_DIR/build/$SONAME"
mkdir -p "$(dirname "$OUT")"

if ! docker image inspect "$IMG" >/dev/null 2>&1; then
    echo "ERROR: docker image '$IMG' not found."
    echo "Build it once from https://github.com/luka-dev/qnx65-armv7-toolchain :  ./host-scripts/qnx-run.sh build"
    exit 1
fi

echo "=== Android Auto Hook Build (Docker $IMG) ==="

docker run --rm --platform=linux/amd64 -v "$PROJECT_DIR":/src "$IMG" bash -c '
  set -e
  export PATH=/opt/qnx650/host/linux/x86/usr/bin:$PATH
  export QNX_HOST=/opt/qnx650/host/linux/x86 QNX_TARGET=/opt/qnx650/target/qnx6
  CC=arm-unknown-nto-qnx6.5.0eabi-gcc
  NM=arm-unknown-nto-qnx6.5.0eabi-nm
  cd /src/aa_hook
  SRCS="aa_hook.c aa_tee.c aa_navxlate.c aa_uiconfig.c"
  # Link in the container, copy only the final ELF to the mount (see README, Windows notes).
  $CC -shared -fPIC -O2 -g -std=gnu99 -Wall -Wextra -Werror -fvisibility=hidden \
      $SRCS -Wl,-soname,'"$SONAME"' -Wl,--version-script=/src/aa_hook/aa_hook.exports.map \
      -lsocket -o /tmp/'"$SONAME"'
  awk "/global:/{g=1;next} /local:/{g=0} g{gsub(/[;[:space:]]/,\"\"); if(length) print}" \
      /src/aa_hook/aa_hook.exports.map | sort > /tmp/exports_expected
  $NM -D --defined-only /tmp/'"$SONAME"' | awk "NF >= 3 { print \$3 }" | sort > /tmp/exports_actual
  diff -u /tmp/exports_expected /tmp/exports_actual || { echo "REJECTED: dynamic exports differ from the allowlist"; exit 1; }
  # emutls trap: no thread-local emutls in a preloaded library (QNX 6.5 crash).
  n=$($NM /tmp/'"$SONAME"' 2>/dev/null | grep -ci emutls || true)
  [ "$n" = "0" ] || { echo "REJECTED: $n emutls symbols present"; exit 1; }
  cp /tmp/'"$SONAME"' /src/build/'"$SONAME"'
'

test -s "$OUT"
echo "built: $OUT"
