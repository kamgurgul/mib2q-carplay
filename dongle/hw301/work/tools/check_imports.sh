#!/bin/sh
# Fail if a shim imports a symbol the dongle cannot resolve.
#
#   check_imports.sh <build-tree> <shim-file-name-in-lib>     (runs in alpine)
#
# musl does not load a library with an unresolved non-weak symbol, and the shims
# are loaded by CPAAProxyEx, which is also what starts the Wi-Fi AP (hostapd) and
# the USB gadget. One bad import = no AP, no USB, no Wi-Fi recovery (2026-10-19:
# __dlsym_time64 from newer musl headers did exactly this).
# "Resolvable" = defined by a library in the image, or already imported by stock
# code (proof that the dongle's base-firmware libc provides it).
set -eu
apk add --no-cache binutils >/dev/null 2>&1
root="$1"
shim="$root/lib/$2"
test -f "$shim" || { echo "!! no shim $shim"; exit 1; }

syms() { readelf -W --dyn-syms "$1" 2>/dev/null || true; }

# non-weak undefined imports of the shim
syms "$shim" | awk '$7=="UND" && $5!="WEAK" && $8!="" {print $8}' | sort -u > /tmp/need

: > /tmp/have
for f in "$root"/bin/* "$root"/lib/*.so*; do
  [ -f "$f" ] || continue
  [ "$f" = "$shim" ] && continue
  # defined symbols, plus non-weak imports of stock code
  syms "$f" | awk '$8!="" && ($7!="UND" || $5!="WEAK") {print $8}' >> /tmp/have
done
# Present in every musl release since 1.0, so safe even though no stock binary
# imports them: dynamic TLS for -fPIC __thread, pthread_detach (receiver
# thread), raise (libgcc's divide-by-zero helper). Keep this list minimal;
# never add a *time64 name here.
printf '%s\n' __tls_get_addr pthread_detach raise >> /tmp/have
sort -u /tmp/have -o /tmp/have

missing=$(comm -23 /tmp/need /tmp/have | tr '\n' ' ')
if [ -n "$missing" ]; then
  echo "!! $2 imports symbols the dongle cannot resolve: $missing"
  echo "!! (CPAAProxyEx would fail to start: no Wi-Fi AP, no USB). Not packing this image."
  exit 1
fi
echo "   all $(wc -l < /tmp/need | tr -d ' ') non-weak imports of $2 resolve on the dongle"
