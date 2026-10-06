#!/bin/sh
# M.I.B. -> Advanced Settings -> Run Custom Script: Android Auto cluster map ON.
#
# Removes /mnt/app/root/aa_cluster.off, so aa_startup.sh preloads the cluster-display
# hook into gal again. The hook still runs only on a receiver it has a profile for
# (see docs/android-auto/firmware-porting.md); on any other it keeps stock Android Auto.
# Applies from the next phone connection.
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

M=/mnt/app/root/aa_cluster.off
echo "custom.sh: Android Auto cluster map ON"
mount -uw /mnt/app 2>/dev/null || echo "custom.sh: mount -uw /mnt/app failed"
rm -f "$M"
if [ -e "$M" ]; then
    echo "FAILED: $M still present (is /mnt/app read-only?)"
    exit 1
fi
sync
echo "DONE. Android Auto cluster map ON."
echo "Reconnect the phone to apply (no reboot needed)."
