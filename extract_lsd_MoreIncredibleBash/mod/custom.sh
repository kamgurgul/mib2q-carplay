#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: copy the stock lsd.jxe to the SD card.
# QNX 6.5 /bin/sh is ksh (pdksh) - this script stays inside its portable subset.
# Writes <card>/lsd_extract/lsd.jxe and info.txt. Nothing is changed on the unit.
set -u
PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin:/mnt/app/armle/sbin:/mnt/app/armle/usr/sbin
export PATH
unset LD_PRELOAD

case $0 in */*) D=${0%/*} ;; *) D=. ;; esac
D=$(cd "$D" && pwd) || exit 1

# The HMI files live on the MMX; forward a manual RCC launch.
if [ ! -d /mnt/app/eso/hmi/lsd ] && [ ! -r /ifs/lsd.jxe ] && [ -d /net/mmx/mnt/app/eso/hmi/lsd ]; then
    exec on -f mmx /bin/sh "$D/custom.sh" "$@"
fi

CARD=${D%/mod}                           # this script sits in <card>/mod/

# The card may be mounted read-only: remount the mount point that holds it.
can_write() { : > "$1/.lsd_write_test" 2>/dev/null && rm -f "$1/.lsd_write_test"; }
if ! can_write "$CARD"; then
    for m in "$CARD" /fs/sda0 /fs/sdb0 /fs/usb0_0 /net/mmx/fs/sda0 /net/mmx/fs/sdb0 /net/mmx/fs/usb0_0; do
        [ -d "$m/mod" ] || continue
        mount -uw "$m" 2>/dev/null
        can_write "$m" && { CARD=$m; break; }
    done
fi
can_write "$CARD" || { echo "FAILED: card at $CARD is not writable"; exit 1; }

SRC=
for f in /mnt/app/eso/hmi/lsd/lsd.jxe /ifs/lsd.jxe; do
    [ -r "$f" ] && { SRC=$f; break; }
done
[ -n "$SRC" ] || { echo "FAILED: lsd.jxe not found (try: find / -name lsd.jxe)"; exit 1; }

OUT=$CARD/lsd_extract
mkdir -p "$OUT" || { echo "FAILED mkdir $OUT"; exit 1; }
echo "custom.sh: copying $SRC to $OUT"
cp "$SRC" "$OUT/lsd.jxe" || { echo "FAILED cp"; exit 1; }
{
    echo "source $SRC"
    echo "date   $(date 2>&1)"
    ls -l "$SRC" 2>&1
    uname -a 2>&1
} > "$OUT/info.txt"
sync
ls -l "$SRC" "$OUT/lsd.jxe"
echo "custom.sh: done"
