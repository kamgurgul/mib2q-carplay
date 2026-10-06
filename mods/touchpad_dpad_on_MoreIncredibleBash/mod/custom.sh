#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: MMI touchpad as D-pad ON (default).
#
# Removes /mnt/app/carplay_touchpad_dpad.off: finger drags on the MMI touchpad move the
# CarPlay selection again. Applies from the next CarPlay connection.
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
echo "custom.sh: touchpad as D-pad ON"
mount -uw /mnt/app 2>/dev/null || echo "custom.sh: mount -uw /mnt/app failed"
rm -f "$M"
if [ -e "$M" ]; then
    echo "FAILED: $M still present (is /mnt/app read-only?)"
    exit 1
fi
sync
echo "DONE. Touchpad as D-pad ON."
echo "Reconnect the iPhone to apply (no reboot needed)."
