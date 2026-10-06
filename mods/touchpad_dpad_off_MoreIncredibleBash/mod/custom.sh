#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: MMI touchpad as D-pad OFF.
#
# Creates /mnt/app/carplay_touchpad_dpad.off. CarPlay then gets the touchpad the way the
# stock unit sends it (raw touchpad input) instead of D-pad steps from finger drags.
# Applies from the next CarPlay connection; run touchpad_dpad_on to revert.
#
# QNX 6.5 /bin/sh is ksh; stays inside its portable subset.
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

M=/mnt/app/carplay_touchpad_dpad.off
echo "custom.sh: touchpad as D-pad OFF"
mount -uw /mnt/app 2>/dev/null || echo "custom.sh: mount -uw /mnt/app failed"
: > "$M" || { echo "FAILED: cannot create $M (is /mnt/app read-only?)"; exit 1; }
[ -e "$M" ] || { echo "FAILED: $M was not created"; exit 1; }
sync
echo "DONE. Touchpad as D-pad OFF ($M)."
echo "Reconnect the iPhone to apply (no reboot needed)."
