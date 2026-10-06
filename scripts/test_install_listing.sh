#!/bin/sh
# list_payload() from the M.I.B. installer.  Flat release assets: checked by name, find
# never runs, a partly copied release fails naming what is missing.  root/ tree, against
# a stub find: the QNX fs-dos "./dir/..: Filename too long" quirk (exit 1, full list)
# passes; any other error fails.  Every shell found here (QNX 6.5 /bin/sh is pdksh).
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=$ROOT/mods/install_MoreIncredibleBash/mod/custom.sh
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
{ echo 'set -u'; echo 'RES=$2; ROOT=$RES/root; HOOKS=/h; JARS=/j'
  sed -n '/^flat_dest()/,/^}/p' "$SRC"; sed -n '/^FLAT_ASSETS=/,/"$/p' "$SRC"
  sed -n '/^OPTIONAL_ASSETS=/p' "$SRC"
  sed -n '/^list_payload()/,/^}/p' "$SRC"; echo 'list_payload "$1"'; } > "$T/h.sh"
ASSETS=$(sed -n '/^FLAT_ASSETS=/,/"$/p' "$SRC" | tr -d '"' | sed 's/^FLAT_ASSETS=//')
mkdir -p "$T/bin" "$T/tree/root/a" "$T/flat"
: > "$T/tree/root/a/one"; : > "$T/tree/root/a/two"
stub() {   # stub <stderr line or empty> <print files 0|1>
    { echo '#!/bin/sh'
      [ "$2" = 1 ] && echo 'echo ./a/one; echo ./a/two'
      [ -n "$1" ] && echo "echo '$1' >&2; exit 1"
      echo 'exit 0'; } > "$T/bin/find"
    chmod +x "$T/bin/find"
}
fail() { echo "FAIL ($1): $2"; exit 1; }
run() { PATH=$T/bin:$PATH "$sh" "$T/h.sh" "$T/list" "$T/$1"; }
shells=
for s in /bin/ksh /bin/mksh "$(command -v mksh 2>/dev/null)" /bin/dash /bin/sh; do [ -n "$s" ] && [ -x "$s" ] && shells="$shells $s"; done
for sh in $shells; do
    stub "find: ./a/..: Filename too long" 1
    run tree > /dev/null || fail "$sh" "fs-dos quirk rejected"
    [ "$(wc -l < "$T/list")" -eq 2 ] && [ ! -e "$T/list.err" ] || fail "$sh" "list incomplete or .err left"
    stub "find: ./a: Permission denied" 1
    ! run tree > /dev/null || fail "$sh" "real error accepted"
    [ ! -e "$T/list" ] || fail "$sh" "list left after failure"
    # flat: complete release, no find at all (stub find would fail)
    stub "find: must not run" 0
    for a in $ASSETS; do : > "$T/flat/$a"; done
    run flat > /dev/null || fail "$sh" "complete release rejected"
    [ "$(wc -l < "$T/list")" -eq "$(echo $ASSETS | wc -w)" ] || fail "$sh" "flat list wrong"
    grep -q '/flat/carplay_monitor.sh|/h/carplay_monitor.sh$' "$T/list" || fail "$sh" "monitor not listed"
    rm "$T/flat/carplay_monitor.sh"
    out=$(run flat) && fail "$sh" "incomplete release accepted"
    case $out in *'missing in '*carplay_monitor.sh*) ;; *) fail "$sh" "missing file not named: $out" ;; esac
    rm -f "$T/flat/"*
    ! run flat > /dev/null || fail "$sh" "empty card accepted"
done
echo "install payload listing: flat release checked by name, fs-dos '..' quirk tolerated, real errors fail:$shells PASS"
