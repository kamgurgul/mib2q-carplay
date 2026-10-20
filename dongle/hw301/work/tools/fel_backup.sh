#!/usr/bin/env bash
# fel_backup.sh - wait for the HW301 in Allwinner FEL mode, then make two
# READ-ONLY backups of the whole SPI-NOR flash and check they are identical.
# This script never erases or writes the flash.
#
#   fel_backup.sh [out-dir]                 (default: work/rollback, git-ignored)
#   XFEL=/path/to/xfel fel_backup.sh        (default: work/third_party/xfel/xfel)
#
# Enter FEL (board RSY03 V1.0, V851S): dongle unplugged; short the flash chip's
# pin 2 (DO, second leg from the top on the left of the 8-pin chip above the
# SoC) to GND (USB plug metal shell); plug into the Mac; release after ~2 s.
# xfel: https://github.com/xboot/xfel (v1.4.0 supports V851/V853 spinor).
set -euo pipefail
cd "$(dirname "$0")/../.."
XFEL="${XFEL:-work/third_party/xfel/xfel}"
OUT="${1:-work/rollback}"
test -x "$XFEL" || { echo "!! xfel not found at $XFEL (build: make -C <xfel-src>)"; exit 1; }
mkdir -p "$OUT"

echo "[fel] waiting for the dongle in FEL mode (short pin 2 to GND, plug in, release) ..."
until "$XFEL" version >/dev/null 2>&1; do sleep 0.5; done
echo "[fel] FOUND: $("$XFEL" version | tr -d '\r')"
echo "[fel] >>> RELEASE THE PIN-2 SHORT NOW - reading starts in 10 s <<<"
sleep 10

det=$("$XFEL" spinor 2>&1 | tr -d '\r')
echo "[fel] $det"
size=$(printf '%s\n' "$det" | sed -n 's/.* with \([0-9][0-9]*\) bytes.*/\1/p')
[ -n "$size" ] || { echo "!! no SPI-NOR detected (short still held, or bad contact) - unplug and retry"; exit 2; }
hex=$(printf '0x%x' "$size")

ts=$(date +%Y%m%d-%H%M%S)
for n in 1 2; do
  f="$OUT/flash-$ts-$n.bin"
  echo "[fel] read $n of 2: $hex bytes -> $f"
  "$XFEL" spinor read 0x0 "$hex" "$f"
done

a="$OUT/flash-$ts-1.bin"; b="$OUT/flash-$ts-2.bin"
cmp -s "$a" "$b" || { echo "!! the two reads differ - unstable read; write NOTHING, replug and retry"; exit 3; }
# identical but meaningless (e.g. DO still shorted -> all zeros): a real dump holds squashfs images
LC_ALL=C grep -q -a 'hsqs' "$a" || { echo "!! reads match but contain no squashfs - DO line still shorted? retry"; exit 4; }

ln -sf "$(basename "$a")" "$OUT/flash-latest.bin"
echo "[fel] OK: both reads identical, $size bytes"
shasum -a 256 "$a"
echo "[fel] backup: $a   (link: $OUT/flash-latest.bin)"
echo "[fel] restore: see README.md \"Rollback\" (fw/rollback/restore-app-v129-at-0x7e0000.bin)"
