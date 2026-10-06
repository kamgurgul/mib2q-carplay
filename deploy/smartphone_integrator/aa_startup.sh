#!/bin/sh
# Thin smartphone_integrator child wrapper for Android Auto (children.gal).
# Publish this generation, start the renderer monitor, then immediately become
# the stock gal so SI retains exact PID/watchdog ownership.
#
# It always ends in the stock gal: a missing helper only drops the cluster extras,
# never Android Auto itself. The cluster-display hook is preloaded only when it is
# installed and /mnt/app/root/aa_cluster.off is absent; it then still keeps stock
# behaviour on a receiver it does not know (see aa_hook/aa_hook.c).

GALDIR=${GALDIR:-/mnt/app/eso/bin/apps}
H=${H:-/mnt/app/root/hooks}
WLOG=${WLOG:-/tmp/aa_wrapper.log}
OWNER_FILE=${OWNER_FILE:-/tmp/aa_supervisor.owner}
AA_OFF_MARKER=${AA_OFF_MARKER:-/mnt/app/root/aa_cluster.off}
# The renderers' tested library path (the CarPlay child's). gal's own path also
# carries /mnt/app/armle/graphics, which they were never run with.
RENDER_LD_LIBRARY_PATH=/mnt/app/root/lib-target:/eso/lib:/mnt/app/usr/lib:/mnt/app/armle/lib:/mnt/app/armle/lib/dll:/mnt/app/armle/usr/lib
GAL_PID=$$

[ -x "$GALDIR/gal" ] || {
    echo "[aa-startup] missing $GALDIR/gal" >> "$WLOG"
    exit 127
}
cd "$GALDIR" 2>/dev/null || {
    echo "[aa-startup] cannot enter $GALDIR" >> "$WLOG"
    exit 127
}
echo "===== Android Auto generation pid=$GAL_PID ppid=${PPID:-unknown} =====" >> "$WLOG"

# Renderers (maneuver arrow, cluster video): the same monitor as CarPlay, with this
# generation's owner file. Never exposes a partially-written owner file.
if [ -x "$H/carplay_monitor.sh" ] && [ -r "$H/carplay_processes.sh" ]; then
    OWNER_STAGE=${OWNER_FILE}.${GAL_PID}
    if echo "$GAL_PID" > "$OWNER_STAGE" && mv "$OWNER_STAGE" "$OWNER_FILE"; then
        LD_PRELOAD= LD_LIBRARY_PATH=$RENDER_LD_LIBRARY_PATH H=$H WLOG=$WLOG OWNER_FILE=$OWNER_FILE \
            "$H/carplay_monitor.sh" "$GAL_PID" </dev/null >>"$WLOG" 2>&1 &
        echo "[aa-startup] monitor=$!" >> "$WLOG"
    else
        echo "[aa-startup] owner file not writable; no renderer monitor" >> "$WLOG"
    fi
else
    echo "[aa-startup] renderer monitor not installed" >> "$WLOG"
fi

unset LD_PRELOAD AA_CLUSTER_HOOK
if [ -e "$AA_OFF_MARKER" ]; then
    echo "[aa-startup] cluster display off ($AA_OFF_MARKER)" >> "$WLOG"
elif [ -r "$H/libaa_cluster_hook.so" ]; then
    # Only gal receives the hook; the monitor and renderers run LD_PRELOAD-clear.
    export AA_CLUSTER_HOOK=1
    export LD_PRELOAD="$H/libaa_cluster_hook.so"
else
    echo "[aa-startup] cluster hook not installed; stock gal" >> "$WLOG"
fi

echo "[aa-startup] exec gal pid=$GAL_PID preload=${LD_PRELOAD:-none}" >> "$WLOG"
exec "$GALDIR/gal" "$@"

# Reachable only for an exec/runtime loader failure.
echo "[aa-startup] exec gal failed rc=$?" >> "$WLOG"
exit 127
