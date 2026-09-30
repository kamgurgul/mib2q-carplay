#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: ENABLE CarPlay route guidance.
#
# Removes the marker(s) written by rgd_disable, so RgdModule resumes the cluster
# maneuver arrow / HUD / BAP takeover from the NEXT phone reconnect — no reboot.
# Idempotent: running it with route guidance already on is a no-op.
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

echo "custom.sh: enabling CarPlay route guidance"
mount -uw /mnt/app 2>/dev/null || true
rm -f /mnt/app/carplay_rgd.disabled /tmp/carplay_rgd.disabled
sync
echo "DONE. Route guidance ON."
echo "Reconnect the phone to apply (no reboot needed). Run rgd_disable for CarPlay video only."
