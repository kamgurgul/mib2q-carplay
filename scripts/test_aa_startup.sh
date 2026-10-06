#!/bin/sh
# aa_startup.sh (children.gal wrapper) in a sandbox with a fake gal and monitor:
# gal is exec'ed as the same PID with the SI arguments, from its own directory; the
# hook is preloaded (AA_CLUSTER_HOOK=1) only when installed and not switched off; the
# monitor gets this generation's owner file, no LD_PRELOAD and the renderers' library
# path; missing helpers still end in stock gal; a missing gal fails.
# Every POSIX/ksh shell here (QNX 6.5 /bin/sh is pdksh).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$ROOT/deploy/smartphone_integrator/aa_startup.sh
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL ($1): $2"; exit 1; }

setup() {
    rm -rf "$T/s"; mkdir -p "$T/s/apps" "$T/s/hooks"
    cat > "$T/s/apps/gal" <<'EOF'
#!/bin/sh
{ echo "pid=$$"; echo "pwd=$(pwd)"; echo "args=$*"
  echo "preload=${LD_PRELOAD:-}"; echo "hook=${AA_CLUSTER_HOOK:-}"; } > "$OUT"
EOF
    cat > "$T/s/hooks/carplay_monitor.sh" <<'EOF'
#!/bin/sh
{ echo "gen=$1"; echo "owner=$(cat "$OWNER_FILE")"; echo "preload=${LD_PRELOAD:-}"
  echo "ldpath=$LD_LIBRARY_PATH"; } > "$MON"
EOF
    : > "$T/s/hooks/carplay_processes.sh"
    echo so > "$T/s/hooks/libaa_cluster_hook.so"
    chmod +x "$T/s/apps/gal" "$T/s/hooks/carplay_monitor.sh"
}

run() {   # $1 shell; output in $T/out, monitor record in $T/mon
    rm -f "$T/out" "$T/mon"
    ( cd /
      OUT=$T/out MON=$T/mon GALDIR=$T/s/apps H=$T/s/hooks WLOG=$T/s/wlog \
      OWNER_FILE=$T/s/owner AA_OFF_MARKER=$T/s/aa_cluster.off LD_PRELOAD= \
      "$1" "$SRC" -x arg2 )
}

wait_mon() { i=0; while [ ! -s "$T/mon" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i+1)); done; }

shells=
for s in /bin/ksh /bin/mksh "$(command -v mksh 2>/dev/null)" /bin/dash /bin/sh; do [ -n "$s" ] && [ -x "$s" ] && shells="$shells $s"; done
for sh in $shells; do
    setup
    run "$sh" || fail "$sh" "wrapper failed"
    wait_mon
    grep -q "^args=-x arg2$" "$T/out" || fail "$sh" "arguments not passed"
    grep -q "^pwd=$T/s/apps$" "$T/out" || fail "$sh" "gal not started from its directory"
    grep -q "^preload=$T/s/hooks/libaa_cluster_hook.so$" "$T/out" || fail "$sh" "hook not preloaded"
    grep -q "^hook=1$" "$T/out" || fail "$sh" "AA_CLUSTER_HOOK not set"
    pid=$(sed -n 's/^pid=//p' "$T/out")
    [ "$(cat "$T/s/owner")" = "$pid" ] || fail "$sh" "owner file does not name the exec'ed gal pid"
    grep -q "^gen=$pid$" "$T/mon" && grep -q "^owner=$pid$" "$T/mon" || fail "$sh" "monitor generation"
    grep -q "^preload=$" "$T/mon" || fail "$sh" "monitor inherited LD_PRELOAD"
    grep -q '^ldpath=/mnt/app/root/lib-target:' "$T/mon" || fail "$sh" "monitor library path"
    grep -q 'graphics' "$T/mon" && fail "$sh" "monitor got gal's graphics path"

    : > "$T/s/aa_cluster.off"
    run "$sh" || fail "$sh" "wrapper failed (off)"
    grep -q "^preload=$" "$T/out" && grep -q "^hook=$" "$T/out" || fail "$sh" "hook preloaded although switched off"

    setup; rm "$T/s/hooks/libaa_cluster_hook.so"
    run "$sh" || fail "$sh" "wrapper failed (no hook)"
    grep -q "^preload=$" "$T/out" || fail "$sh" "preload without the hook installed"

    setup; rm "$T/s/hooks/carplay_monitor.sh"
    run "$sh" || fail "$sh" "wrapper failed (no monitor)"
    grep -q "^args=-x arg2$" "$T/out" || fail "$sh" "no gal without the monitor"

    setup; rm "$T/s/apps/gal"
    set +e; run "$sh" > /dev/null 2>&1; rc=$?; set -e
    [ $rc -eq 127 ] || fail "$sh" "missing gal rc=$rc"
done
echo "aa_startup.sh: same-PID exec, args, hook preload/off/missing, monitor generation+env, stock fallback:$shells PASS"
