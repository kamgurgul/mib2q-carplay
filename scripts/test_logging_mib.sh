#!/bin/sh
# mods/logging_MoreIncredibleBash/mod/custom.sh on a fake card: numbered saves 001/002,
# /tmp logs and captures copied, shared-memory objects skipped, verbose marker made
# by run 1 and reported by run 2.  Every POSIX/ksh shell here (QNX 6.5 /bin/sh is pdksh).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL ($1): $2"; exit 1; }
shells=
for s in /bin/ksh /bin/mksh "$(command -v mksh 2>/dev/null)" /bin/dash /bin/sh; do [ -x "$s" ] && shells="$shells $s"; done
for sh in $shells; do
    rm -rf "$T/card" "$T/src" "$T/core" "$T/errdump"; mkdir -p "$T/card" "$T/src" "$T/core" "$T/errdump"
    cp -R "$ROOT/mods/logging_MoreIncredibleBash/mod" "$T/card/"
    echo h > "$T/src/carplay_hook.log"; echo a > "$T/src/maneuver_render.log"; echo s > "$T/src/shmem_obj"
    # HMI crash evidence (#62): j9 cores and the HMI's ErrorDumpDir, capped at 1 MB per file.
    echo j > "$T/core/j9.core.gz"; echo o > "$T/core/btstack.core.gz"
    echo e > "$T/errdump/hmi_error_1.txt"
    dd if=/dev/zero of="$T/errdump/big.dump" bs=4096 count=300 2>/dev/null
    export CP_CORE_DIR=$T/core CP_ERRDUMP_DIR=$T/errdump
    CP_LOG_SRC=$T/src "$sh" "$T/card/mod/custom.sh" > /dev/null || fail "$sh" "run 1"
    CP_LOG_SRC=$T/src "$sh" "$T/card/mod/custom.sh" > /dev/null || fail "$sh" "run 2"
    L=$T/card/carplay_logs
    [ -f "$L/001/tmp/carplay_hook.log" ] && [ -f "$L/002/tmp/maneuver_render.log" ] || fail "$sh" "logs not copied"
    [ ! -e "$L/001/tmp/shmem_obj" ] || fail "$sh" "shared-memory object copied"
    [ -f "$L/001/core/j9.core.gz" ] && [ ! -e "$L/001/core/btstack.core.gz" ] || fail "$sh" "j9 core not copied (or a foreign core was)"
    [ -f "$L/001/errdump/hmi_error_1.txt" ] && grep -q hmi_error_1 "$L/001/errdump_ls.txt" || fail "$sh" "HMI error dump not copied"
    [ "$(wc -c < "$L/001/errdump/big.dump" | tr -d ' ')" = 1048576 ] || fail "$sh" "error dump not capped at 1 MB"
    [ -e "$T/src/carplay_verbose" ] || fail "$sh" "no verbose marker"
    grep -q 'verbose     off' "$L/001/info.txt" && grep -q 'verbose     on' "$L/002/info.txt" || fail "$sh" "verbose state not recorded"
    # numbering continues after the highest save, even with gaps and past 008/009
    rm -rf "$L/001"; mkdir "$L/009"
    CP_LOG_SRC=$T/src "$sh" "$T/card/mod/custom.sh" > /dev/null || fail "$sh" "run 3"
    [ -d "$L/010" ] && [ ! -e "$L/001" ] || fail "$sh" "next save is not highest+1: $(ls "$L" | tr '\n' ' ')"
done
echo "logging M.I.B.: highest+1 numbering, /tmp logs, verbose marker:$shells PASS"
