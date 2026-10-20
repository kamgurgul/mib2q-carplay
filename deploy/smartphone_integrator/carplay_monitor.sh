#!/bin/sh
# maneuver_render monitor for one smartphone_integrator/dio generation.
# It never controls USB, OTG, dio_manager or the SI retry state machine.

H=${H:-/mnt/app/root/hooks}
WLOG=${WLOG:-/tmp/carplay_wrapper.log}
OWNER_FILE=${OWNER_FILE:-/tmp/carplay_supervisor.owner}
# Power-on gate for maneuver_render only. Native navigation misses its ~60 s service-manager
# start deadline when a phone session starts in the same window (carplay_logs 024/027). The
# maneuver overlay needs Navigation, so the first start after power-on waits for Java's
# NavReadyMarker, at most NAV_WAIT_MAX_S across generations. /tmp is RAM. With a dongle at
# power-on that wait was 2-4 min (carplay_logs_fixed/_last). altscreen_render is not held:
# it paints the phone map, and a consumer that attaches after that wait has missed the first
# cluster IDR, so the cockpit stays on the native map until navigation is up.
NAV_READY=${NAV_READY:-/tmp/carplay_nav_ready}
NAV_GATE=${NAV_GATE:-/tmp/carplay_renderer_gate}
NAV_WAIT_MAX_S=${NAV_WAIT_MAX_S:-300}
# Diagnostics (boot snapshots, boost trace) only with verbose logging; the persistent marker is
# GEM CarPlay-RGI -> "Verbose logging ON / OFF", the /tmp one is made by "Save logs".
VERBOSE_MARKERS=${VERBOSE_MARKERS:-/mnt/app/carplay_verbose /tmp/carplay_verbose}
# Power-on navigation boost: navigation's start-up threads (priority 10) wait on the eMMC driver
# and io-pkt behind the projection session's priority-10 work; NavSDK start took 54-56 s instead
# of 2.8 s.  nav_boost moves its priority-10 threads to 12 (servers inherit the client's
# priority) until navigation is up, then puts back exactly the threads it raised (recorded in
# NAV_BOOST_STATE).  Off: /mnt/app/root/carplay_nav_boost.off.
NAV_BOOST_MARK=${NAV_BOOST_MARK:-/tmp/carplay_nav_boost.started}
NAV_BOOST_STATE=${NAV_BOOST_STATE:-/tmp/carplay_nav_boost.state}
NAV_BOOST_OFF=${NAV_BOOST_OFF:-/mnt/app/root/carplay_nav_boost.off}
NAV_BOOST_FROM=${NAV_BOOST_FROM:-10}
NAV_BOOST_TO=${NAV_BOOST_TO:-12}
NAV_BOOST_MAX_S=${NAV_BOOST_MAX_S:-300}
NAV_BOOST_NAMES=${NAV_BOOST_NAMES:-AppStartATF navStartup PNavApp DRController IconProviderApp}
# Boot-time slog snapshots (verbose only): the slog ring holds only ~2 min, so a "Save logs" made
# after a failed navigation start has already lost it (carplay_logs_hang).  Taken once per boot,
# only when the first session starts before navigation is up; the logging mod copies
# /tmp/carplay_* to the card.
BOOT_SNAP_MARK=${BOOT_SNAP_MARK:-/tmp/carplay_boot_snap.started}
BOOT_SNAP_PREFIX=${BOOT_SNAP_PREFIX:-/tmp/carplay_boot_slog_}
BOOT_SNAP_AT=${BOOT_SNAP_AT:-60 240}
DIO_PID=${1:-}

case "$DIO_PID" in
    ''|*[!0-9]*|0|1)
        echo "[monitor] invalid dio pid: $DIO_PID" >> "$WLOG"
        exit 2
        ;;
esac

if [ ! -r "$H/carplay_processes.sh" ]; then
    echo "[monitor] missing $H/carplay_processes.sh" >> "$WLOG"
    exit 127
fi
. "$H/carplay_processes.sh"

monitor_owns_generation()
{
    MON_OWNER=
    [ -r "$OWNER_FILE" ] && read MON_OWNER < "$OWNER_FILE"
    [ "$MON_OWNER" = "$DIO_PID" ]
}

monitor_dio_alive()
{
    [ -d "/proc/$DIO_PID" ]
}

monitor_current()
{
    monitor_owns_generation && monitor_dio_alive
}

start_renderer()
{
    MON_NAME=$1
    MON_REASON=$2
    monitor_current || return 0

    if cp_renderer_running "$MON_NAME"; then
        if [ "$MON_REASON" = initial ] && ! cp_renderer_healthy "$MON_NAME"; then
            echo "[monitor] $MON_NAME not ready; confirming in 2s" >> "$WLOG"
            sleep 2
            monitor_current || return 0
            if cp_renderer_running "$MON_NAME" && cp_renderer_healthy "$MON_NAME"; then
                echo "[monitor] $MON_NAME became ready" >> "$WLOG"
                return 0
            fi
            echo "[monitor] $MON_NAME still not ready; replacing" >> "$WLOG"
            cp_kill_renderer "$MON_NAME" 1
        else
            [ "$MON_REASON" = initial ] &&
                echo "[monitor] adopted $MON_NAME" >> "$WLOG"
            return 0
        fi
    fi

    if [ "$MON_REASON" = restart ]; then
        sleep 5
        monitor_current || return 0
    fi

    [ -x "$H/$MON_NAME" ] || {
        echo "[monitor] missing executable $H/$MON_NAME" >> "$WLOG"
        return 0
    }

    # Ownership may change during health probing or backoff. Re-scan before
    # spawning so a live renderer hidden by a stale PID file is adopted.
    monitor_current || return 0
    cp_renderer_running "$MON_NAME" && return 0
    echo "[monitor] starting $MON_NAME reason=$MON_REASON" >> "$WLOG"

    (
        cd "$H" || exit 1
        monitor_current || exit 0
        LD_PRELOAD= GRAPHICS_ROOT=/proc/boot exec "$H/$MON_NAME" \
            </dev/null >>"/tmp/$MON_NAME.log" 2>&1
    ) &
    MON_NEW_PID=$!

    # A replacement generation can take ownership between fork and publish.
    # The child performs the same check before exec; never overwrite the new
    # generation's registry after ownership has changed.
    if monitor_current; then
        cp_renderer_record_pid "$MON_NAME" "$MON_NEW_PID" || :
    fi
}

# 0 = start the renderers now, 1 = this generation ended while waiting.  NAV_GATE holds the
# seconds already waited this boot ("done" once released), so a replaced generation resumes the
# wait instead of restarting it.  A running renderer or a later connect never waits.
monitor_nav_gate()
{
    MON_WAITED=0
    [ -r "$NAV_GATE" ] && read MON_WAITED < "$NAV_GATE"
    [ "$MON_WAITED" = done ] && return 0
    case "$MON_WAITED" in ''|*[!0-9]*) MON_WAITED=0 ;; esac
    if [ -e "$NAV_READY" ] || cp_renderer_running maneuver_render; then
        echo done > "$NAV_GATE" 2>/dev/null
        return 0
    fi
    echo "[monitor] holding renderers until native navigation is up (waited ${MON_WAITED}s, max ${NAV_WAIT_MAX_S}s)" >> "$WLOG"
    while [ ! -e "$NAV_READY" ] && [ "$MON_WAITED" -lt "$NAV_WAIT_MAX_S" ]; do
        monitor_current || return 1
        sleep 2
        MON_WAITED=`expr "$MON_WAITED" + 2`
        echo "$MON_WAITED" > "$NAV_GATE" 2>/dev/null
    done
    if [ -e "$NAV_READY" ]; then
        echo "[monitor] native navigation up after ${MON_WAITED}s; starting renderers" >> "$WLOG"
    else
        echo "[monitor] native navigation not up after ${MON_WAITED}s; starting renderers anyway" >> "$WLOG"
    fi
    echo done > "$NAV_GATE" 2>/dev/null
    return 0
}

monitor_verbose()
{
    for MON_V in $VERBOSE_MARKERS; do
        [ -e "$MON_V" ] && return 0
    done
    return 1
}

# Once per boot (noclobber create is exclusive: CarPlay and Android Auto monitors may race; not
# mkdir: /tmp is /dev/shmem on the unit, which has no directories), and only while
# navigation is not up yet: a detached job saves sloginfo + the navigation processes
# BOOT_SNAP_AT seconds after this first session start.  Never tied to this generation.
monitor_boot_snapshot()
{
    monitor_verbose || return 0
    [ -e "$NAV_READY" ] && return 0
    ( set -C; : > "$BOOT_SNAP_MARK" ) 2>/dev/null || return 0
    echo "[monitor] navigation not up at first session start; boot slog snapshots in ${BOOT_SNAP_AT}s" >> "$WLOG"
    (
        MON_SNAP_DONE=0
        for MON_SNAP_AT in $BOOT_SNAP_AT; do
            sleep `expr "$MON_SNAP_AT" - "$MON_SNAP_DONE"`
            MON_SNAP_DONE=$MON_SNAP_AT
            MON_SNAP_FILE=$BOOT_SNAP_PREFIX${MON_SNAP_AT}s.txt
            {
                echo "== $MON_SNAP_AT s after the first session start; nav_ready=`[ -e "$NAV_READY" ] && echo yes || echo no`"
                pidin ar 2>&1 | grep -i 'navigation\|AppStartATF\|navStartup'
                echo "== sloginfo"
                sloginfo 2>&1
            } > "$MON_SNAP_FILE" 2>&1
        done
    ) </dev/null >/dev/null 2>&1 &
}

# Once per boot, only when the first session starts before navigation is up (the power-on
# case): a detached job re-applies the boost every 2 s, so a relaunched navigation is caught too,
# until NavReadyMarker or NAV_BOOST_MAX_S, then restores the threads it raised.  Never tied to this
# generation; logs only with verbose.
monitor_nav_boost()
{
    [ -e "$NAV_READY" ] && return 0
    [ -x "$H/nav_boost" ] || return 0
    [ -e "$NAV_BOOST_OFF" ] && return 0
    ( set -C; : > "$NAV_BOOST_MARK" ) 2>/dev/null || return 0
    monitor_verbose &&
        echo "[monitor] navigation not up at first session start: priority $NAV_BOOST_FROM -> $NAV_BOOST_TO until it is up (max ${NAV_BOOST_MAX_S}s)" >> "$WLOG"
    (
        MON_BOOST_T=0
        rm -f "$NAV_BOOST_STATE"
        while [ ! -e "$NAV_READY" ] && [ "$MON_BOOST_T" -lt "$NAV_BOOST_MAX_S" ]; do
            MON_BOOST_OUT=`"$H/nav_boost" "$NAV_BOOST_FROM" "$NAV_BOOST_TO" "$NAV_BOOST_STATE" $NAV_BOOST_NAMES 2>&1`
            case "$MON_BOOST_OUT" in
                '0 '*) ;;
                *) monitor_verbose &&
                       echo "[monitor] nav boost +${MON_BOOST_T}s: threads/processes $MON_BOOST_OUT" >> "$WLOG" ;;
            esac
            sleep 2
            MON_BOOST_T=`expr "$MON_BOOST_T" + 2`
        done
        MON_BOOST_OUT=`"$H/nav_boost" -r "$NAV_BOOST_STATE" 2>&1`
        monitor_verbose &&
            echo "[monitor] nav boost ended after ${MON_BOOST_T}s (nav_ready=`[ -e "$NAV_READY" ] && echo yes || echo no`): restored $MON_BOOST_OUT" >> "$WLOG"
    ) </dev/null >/dev/null 2>&1 &
}

monitor_main()
{
    trap 'exit 0' 1 2 15

    # The renderers never gate dio (SI's 10 s budget ends at the phone's control SETUP).
    # altscreen_render starts immediately so it is on the tee before the first cluster
    # IDR. It runs below the boosted navigation threads. maneuver_render still waits
    # for Navigation (monitor_nav_gate): it composites over the native map.
    monitor_current || exit 0
    echo "[monitor] generation=$DIO_PID active" >> "$WLOG"

    cp_seed_renderer_pid_files
    monitor_nav_boost
    monitor_boot_snapshot
    [ -x "$H/altscreen_render" ] && start_renderer altscreen_render initial
    monitor_nav_gate || exit 0
    start_renderer maneuver_render initial

    MON_TICKS=0
    while monitor_current; do
        start_renderer maneuver_render restart
        [ -x "$H/altscreen_render" ] && start_renderer altscreen_render restart
        sleep 2
        MON_TICKS=`expr "$MON_TICKS" + 1`
        if [ `expr "$MON_TICKS" % 150` -eq 0 ]; then
            cp_cap_all_logs "$WLOG"
        fi
    done

    if monitor_owns_generation; then
        echo "[monitor] dio_manager pid=$DIO_PID exited; renderer remains persistent" >> "$WLOG"
    else
        echo "[monitor] generation=$DIO_PID superseded" >> "$WLOG"
    fi
}

monitor_main
