#!/bin/bash
#
# Build the canonical CarPlay Java patch — in Docker (no host JDK required).
#
#   ./scripts/build_java.sh
#
# Input:  java_patch/   +   ../../Tools/jxe2jar   (stock jar + OSGi libs)
# Output: build/carplay_hook.jar
#
# Compiles against MU1316-final.jar + OSGi, target 1.4 (jclfoun11 = Foundation 1.1),
# inside a pinned JDK 8 container so the build does not depend on a host JVM.
#
# NOTE: MU1316-final.jar is the author's own decompiled stock HMI jar
# (../../Tools/jxe2jar/out/). Yours may be named or located differently - point
# STOCK_JAR below and the CP line inside the container at your own stock jar.
set -e

[ "$#" -eq 0 ] || { echo "usage: ./scripts/build_java.sh"; exit 2; }

IMG=eclipse-temurin:8-jdk-jammy
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TOOLS_DIR="$(cd "$PROJECT_DIR/../../Tools/jxe2jar" && pwd)"

# Stock HMI jar: the author's is MU1316-final.jar, but any unit's decompiled jxe
# works. Override with STOCK_JAR=<name-or-path>, else prefer MU1316-final.jar,
# else auto-pick the newest *.jar in out/. The jar must live in TOOLS_DIR/out/
# (that is what gets mounted into the container).
if [ -n "${STOCK_JAR:-}" ] && [ -f "$TOOLS_DIR/out/$(basename "$STOCK_JAR")" ]; then
    STOCK_JAR="$TOOLS_DIR/out/$(basename "$STOCK_JAR")"
elif [ -f "$TOOLS_DIR/out/MU1316-final.jar" ]; then
    STOCK_JAR="$TOOLS_DIR/out/MU1316-final.jar"
else
    STOCK_JAR="$(ls -t "$TOOLS_DIR"/out/*.jar 2>/dev/null | head -1)"
fi
[ -n "$STOCK_JAR" ] && [ -f "$STOCK_JAR" ] || {
    echo "ERROR: no stock HMI jar in $TOOLS_DIR/out/"
    echo "  Put your unit's decompiled lsd.jxe jar there (see docs/deploy/altscreen-mhi2q.md, section 'Building the Java patch')."
    exit 1
}
STOCK_JAR_NAME="$(basename "$STOCK_JAR")"
echo "Stock jar: $STOCK_JAR_NAME"
[ -d "$PROJECT_DIR/java_patch" ] || { echo "ERROR: java_patch/ not found"; exit 1; }

# Reproducible build identity is computed on the host (git lives here), passed into the container.
BUILD_ID_RAW=${CARPLAY_BUILD_ID:-$(git -C "$PROJECT_DIR" describe --always --dirty 2>/dev/null || echo unknown)}
BUILD_ID=$(printf '%s' "$BUILD_ID_RAW" | tr -cd 'A-Za-z0-9._-')

echo "=== CarPlay Java Patch Build (Docker $IMG) ==="

docker run --rm \
  -v "$PROJECT_DIR":/src \
  -v "$TOOLS_DIR":/tools:ro \
  -e BUILD_ID="$BUILD_ID" \
  -e STOCK_JAR_NAME="$STOCK_JAR_NAME" \
  "$IMG" bash -c '
  set -e
  SRC=/src/java_patch
  OUT=/src/build/java/classes
  OUTJAR=/src/build/carplay_hook.jar
  CP="/tools/out/$STOCK_JAR_NAME:/tools/libs/org.osgi.framework-1.10.0.jar:/tools/libs/org.osgi.util.tracker-1.5.4.jar"

  rm -rf /src/build/java; mkdir -p "$OUT" /src/build
  SRCLIST=$(mktemp)
  find "$SRC" -name "*.java" -type f > "$SRCLIST"
  echo "Compiling $(wc -l < "$SRCLIST" | tr -d " ") files (target 1.4)..."

  # Generate a CarPlayApp copy with the real BUILD_ID; never edit the source tree.
  GEN=$(mktemp -d)
  mkdir -p "$GEN/com/luka/carplay/core"
  sed "s/@BUILD_ID@/$BUILD_ID/g" "$SRC/com/luka/carplay/core/CarPlayApp.java" > "$GEN/com/luka/carplay/core/CarPlayApp.java"
  grep -v "/com/luka/carplay/core/CarPlayApp.java$" "$SRCLIST" > "$SRCLIST.tmp"; mv "$SRCLIST.tmp" "$SRCLIST"
  printf "%s\n" "$GEN/com/luka/carplay/core/CarPlayApp.java" >> "$SRCLIST"

  javac -source 1.4 -target 1.4 -cp "$CP" -sourcepath "$GEN:$SRC" -d "$OUT" -Xlint:-options @"$SRCLIST"
  # Compact generated metrics/Unicode tables (VC route text) live inside the jar.
  cp -R /src/java_resources/. "$OUT/"
  (cd "$OUT" && jar cf "$OUTJAR" .)
  rm -rf /src/build/java
'

echo "Output: $PROJECT_DIR/build/carplay_hook.jar"
echo "Build ID: $BUILD_ID"
