#!/bin/bash
# Build the AltScreen GEN2 hook (libaltscreen111_mhi2q.so) for QNX/ARMv7 in Docker.
#
#   ./scripts/build_altscreen_hook.sh
#
# Source: altscreen_hook/ (imported from mhi2_altscreen_carplay, GPL-3.0-or-later,
# see LICENSE). Compiled with -DALT111_TARGET_MHI2Q: constructor-
# free, PLT interposition, 1440x540 defaults, stream 111 on port 7100 (the PF
# allowlist, see docs/deploy/altscreen-mhi2q.md section 15). It is LD_PRELOADed
# into dio_manager ALONGSIDE libcarplay_hook.so (carplay_startup.sh).
#
# Same image as the other native builds (qnx65-armv7-toolchain, GCC 8.5).
# The result is intentionally unstripped (-O2 -g).
set -e

[ "$#" -eq 0 ] || { echo "usage: ./scripts/build_altscreen_hook.sh"; exit 2; }

IMG=qnx65-armv7-toolchain:latest
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SONAME=libaltscreen111_mhi2q.so
OUT="$PROJECT_DIR/build/$SONAME"
mkdir -p "$(dirname "$OUT")"

if ! docker image inspect "$IMG" >/dev/null 2>&1; then
    echo "ERROR: docker image '$IMG' not found."
    echo "Build it once from https://github.com/luka-dev/qnx65-armv7-toolchain :  ./host-scripts/qnx-run.sh build"
    exit 1
fi

echo "=== AltScreen Hook Build (Docker $IMG) ==="

docker run --rm --platform=linux/amd64 -v "$PROJECT_DIR":/src "$IMG" bash -c '
  set -e
  export PATH=/opt/qnx650/host/linux/x86/usr/bin:$PATH
  export QNX_HOST=/opt/qnx650/host/linux/x86 QNX_TARGET=/opt/qnx650/target/qnx6
  CC=arm-unknown-nto-qnx6.5.0eabi-gcc
  cd /src/altscreen_hook
  SRCS="libaltscreen111_gen2.c src/alt111_profile.c src/alt111_control.c src/alt111_video.c src/alt111_resync.c"
  # Link in the container, copy only the final ELF to the mount (see README, Windows notes).
  $CC -shared -fPIC -O2 -g -std=gnu99 -Wall -Wextra -DALT111_TARGET_MHI2Q -Iinclude \
      $SRCS -Wl,-soname,'"$SONAME"' -lsocket -o /tmp/'"$SONAME"'
  cp /tmp/'"$SONAME"' /src/build/'"$SONAME"'
'

test -s "$OUT"
echo "built: $OUT (target=mhi2q, soname=$SONAME)"
# Deliberately no strip.
