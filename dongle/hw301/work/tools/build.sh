#!/usr/bin/env bash
# Build a modified HW301 OTA tar (stock v1.29) that adds the Virtual Cockpit
# shim, WITHOUT touching CPAAProxyEx. Injection = "wrapper rename":
#
#   libCoreUtils.so (stock) --rename-->  libCoreUtilsReal.so (soname set distinct)
#   cluster_shim.so         --install--> libCoreUtils.so     (soname libCoreUtils.so,
#                                                             NEEDED libCoreUtilsReal.so)
#
# CPAAProxyEx still NEEDS "libCoreUtils.so" -> loads our shim -> the shim
# overrides what it hooks and reaches the real libs via dlsym(RTLD_NEXT).
# Normally called by build_dual.sh, which sets the env below.
#
#   build.sh [stock=129] [code]   (default stock 129, code 1<stock>)
#   env: WRAP_LIB, CHECK_SYM, CLUSTER_SRC, CLUSTER_CFLAGS
#
# Output: work/out/hw301_v<code>.tar  (+ app.img, appmd5sum.txt)
# Flash with: work/tools/flash_local.py work/out/hw301_v<code>.tar --version <code>
#
# This script only PRODUCES the image. It never contacts the device.
set -euo pipefail
# name the failing command instead of exiting silently under set -e
trap 'rc=$?; echo "!! build.sh failed (exit $rc) at line $LINENO: $BASH_COMMAND" >&2' ERR
here="$(cd "$(dirname "$0")/../.." && pwd)"            # dongle/
cd "$here"
STOCK="${1:-129}"                                       # stock firmware to modify
CODE="${2:-1${STOCK}}"                                  # OTA version code to advertise
STOCK_IMG="fw/v${STOCK}/app.img"

# ---- preflight: every host dependency, with the fix in the message ----------
need() { command -v "$1" >/dev/null 2>&1 || { echo "!! missing '$1' - $2" >&2; exit 1; }; }
need unsquashfs "install with: brew install squashfs"
need mksquashfs "install with: brew install squashfs"
need docker     "install Docker Desktop"
need python3    "needed by flash_local.py / getlogs.py"
docker info >/dev/null 2>&1 || { echo "!! Docker is not running - start Docker Desktop and retry" >&2; exit 1; }
test -f "$STOCK_IMG" || { echo "!! no stock image $STOCK_IMG (git checkout restores it; it is tracked)" >&2; exit 1; }
BUILD="work/build-v${CODE}"
OUT="work/out"
ALPINE="alpine:3.20"
WRAP="${WRAP_LIB:-libCoreUtils.so}"                 # which stock lib to wrap
REAL="${WRAP%.so}Real.so"                           # e.g. libCoreUtilsReal.so
MAINSYM="${CHECK_SYM:-AirPlayReceiverSessionSetup}"  # a symbol to sanity-check is exported

echo ">> 1. fresh-extract stock v${STOCK} rootfs -> $BUILD  (wrapping $WRAP)"
rm -rf "$BUILD"; unsquashfs -d "$BUILD" "$STOCK_IMG" >/dev/null
test -f "$BUILD/lib/$WRAP" || { echo "no $WRAP in stock image"; exit 1; }

# build marker served by the dongle's web server (web files live in the app
# partition). Lets us verify the flash actually landed, independent of the shim:
#   curl http://192.168.5.1/clbuild.txt
BUILDID="v${CODE}-$(date +%Y%m%d%H%M%S)"
test -d "$BUILD/web" && echo "$BUILDID" > "$BUILD/web/clbuild.txt"
echo "   build marker: web/clbuild.txt = $BUILDID"

echo ">> 2. build shim as $WRAP from src/${CLUSTER_SRC:-cluster_dual.c} (NEEDED $REAL)"
mkdir -p "$OUT/stub"
SRC="work/src/${CLUSTER_SRC:-cluster_dual.c}"       # resolved HERE (outer shell)
test -f "$SRC" || { echo "no shim source $SRC"; exit 1; }
./work/tools/xcc.sh sh -c "
  set -e
  echo '' > /tmp/empty.c
  cc -shared -fPIC -o 'work/out/stub/$REAL' /tmp/empty.c
  cc -shared -fPIC -Os -marm ${CLUSTER_CFLAGS:-} -o work/out/cluster_shim.so '$SRC' \
     -Wl,-soname,'$WRAP' -Lwork/out/stub -Wl,--no-as-needed -l:'$REAL'
  strip work/out/cluster_shim.so"

echo ">> 3. rename stock real lib $WRAP -> $REAL + set a DISTINCT soname"
cp "$BUILD/lib/$WRAP" "$BUILD/lib/$REAL"
docker run --rm -v "$here":/src -w /src "$ALPINE" sh -c "
  apk add --no-cache patchelf >/dev/null 2>&1
  patchelf --set-soname '$REAL' '$BUILD/lib/$REAL'"

echo ">> 4. install shim as $WRAP"
cp "$OUT/cluster_shim.so" "$BUILD/lib/$WRAP"

echo ">> 5. sanity-check the modified libs in the build tree"
docker run --rm -v "$here":/src -w /src "$ALPINE" sh -c "
  apk add --no-cache binutils >/dev/null 2>&1
  echo '   $WRAP (shim):';   readelf -d '$BUILD/lib/$WRAP' | awk '/SONAME|NEEDED/{print \"      \"\$0}'
  echo '   $REAL (real):';   readelf -d '$BUILD/lib/$REAL' | awk '/SONAME/{print \"      \"\$0}'
  echo '   shim exports $MAINSYM:'; readelf -W --dyn-syms '$BUILD/lib/$WRAP' | awk '/$MAINSYM/ && /FUNC/ && !/UND/{print \"      \"\$0}'"

echo ">> 5b. every symbol the shim imports must resolve on the dongle (else CPAAProxyEx won't start)"
docker run --rm -v "$here":/src -w /src "$ALPINE" sh work/tools/check_imports.sh "$BUILD" "$WRAP"

echo ">> 6. repack -> OTA tar"
./work/tools/repack.sh "$BUILD" "$CODE"

echo ">> 7. round-trip verify the packed image unpacks and the wrapped libs survive"
rm -rf work/_verify && unsquashfs -d work/_verify "$OUT/app.img" >/dev/null
test -f "work/_verify/lib/$WRAP" || { echo "verify: missing shim $WRAP"; exit 1; }
test -f "work/_verify/lib/$REAL" || { echo "verify: missing renamed real $REAL"; exit 1; }
rm -rf work/_verify
echo ">> DONE. Flash with:"
echo "     python3 work/tools/flash_local.py $OUT/hw301_v${CODE}.tar --version $CODE --dry-run"
echo "   (drop --dry-run to actually flash; bench power only, see README.md)"
