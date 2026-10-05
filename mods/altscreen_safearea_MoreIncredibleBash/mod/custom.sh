#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: set the AltScreen SafeArea.
#
# Copies x/y/w/h from safearea.conf (next to this script on the SD card) to
# /mnt/app/root/mibr-carplay111-safearea.conf, which the AltScreen hook reads at
# every CarPlay connect and advertises to iOS. iOS centres the map puck and keeps
# its UI inside that rectangle, so it must be the part of the 1440x540 canvas the
# VC actually shows (measure it with the altscreen_grid mod).
# A line "reset" in safearea.conf removes the override (back to the hook's built-in
# SafeArea x=360 y=87 w=720 h=297, the commercial MHI2Q table).
# Out-of-range values are ignored by the hook, which then logs it and keeps the built-in SafeArea.
# Reconnect the phone/dongle to apply; no reboot.
#
# QNX 6.5 /bin/sh is ksh; stays inside its portable subset.
# Copyright (c) 2026
# SPDX-License-Identifier: GPL-3.0-or-later
set -u
PATH=/proc/boot:/bin:/usr/bin:/usr/sbin:/sbin:/mnt/app/armle/bin:/mnt/app/armle/usr/bin
export PATH
unset LD_PRELOAD

case $0 in */*) D=${0%/*} ;; *) D=. ;; esac
D=$(cd "$D" && pwd) || exit 1

if [ ! -d /mnt/app/eso/hmi/lsd ] && [ -d /net/mmx/mnt/app/eso/hmi/lsd ]; then
    exec on -f mmx /bin/sh "$D/custom.sh" "$@"
fi

SRC=${CP_SAFEAREA_SRC:-$D/safearea.conf}   # GEM menu: <card>/carplay_safearea.conf
DST=${CP_SAFEAREA_DST:-/mnt/app/root/mibr-carplay111-safearea.conf}   # GEM menu: the classic-view file too
[ -r "$SRC" ] || { echo "FAILED: $SRC not found"; exit 1; }
mount -uw /mnt/app 2>/dev/null || echo "custom.sh: mount -uw /mnt/app failed"

if grep -q '^reset' "$SRC"; then
    rm -f "$DST"
    sync
    echo "DONE. SafeArea override removed (built-in SafeArea). Reconnect to apply."
    exit 0
fi

X= Y= W= H=
while IFS='=' read -r k v; do
    v=$(echo "$v" | tr -d ' \r')
    case $k in
        x) X=$v ;; y) Y=$v ;; w) W=$v ;; h) H=$v ;;
    esac
done < "$SRC"
for n in "$X" "$Y" "$W" "$H"; do
    case $n in ''|*[!0-9]*) echo "FAILED: safearea.conf needs numeric x, y, w, h"; exit 1 ;; esac
done

# The hook parses exactly this layout: "x=%d\ny=%d\nw=%d\nh=%d".
printf 'x=%s\ny=%s\nw=%s\nh=%s\n' "$X" "$Y" "$W" "$H" > "$DST" \
    || { echo "FAILED: cannot write $DST (is /mnt/app read-only?)"; exit 1; }
sync
echo "DONE. SafeArea x=$X y=$Y w=$W h=$H. Reconnect the phone/dongle to apply."
