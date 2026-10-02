#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: AltScreen advertisement ON (revert).
#
# Removes /mnt/app/mibr-carplay111.noadvertise, so the hook advertises the
# AltScreen display in /info again and accepts stream-111 SETUP: CarPlay video on
# the Virtual Cockpit resumes from the NEXT phone reconnect, no reboot.
# Idempotent: a no-op when the advertisement is already on.
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

M=/mnt/app/mibr-carplay111.noadvertise
echo "custom.sh: enabling the AltScreen /info advertisement"
mount -uw /mnt/app 2>/dev/null || echo "custom.sh: mount -uw /mnt/app failed"
rm -f "$M"
if [ -e "$M" ]; then
    echo "FAILED: $M still present (is /mnt/app read-only?)"
    exit 1
fi
sync
echo "DONE. AltScreen advertisement ON."
echo "Reconnect the phone to apply (no reboot needed)."
