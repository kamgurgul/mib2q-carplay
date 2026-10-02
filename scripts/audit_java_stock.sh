#!/bin/bash
set -euo pipefail
PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
. "$PROJECT_DIR/scripts/stock_env.sh"
stock_require jar jdk
ASM="$ASM_LIBS"
bash "$PROJECT_DIR/scripts/build_java.sh"
python3 "$PROJECT_DIR/tests/audit_java_sources.py" "$PROJECT_DIR" \
    "$STOCK_VF_DIR" "$PROJECT_DIR/build/java-stock-audit"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
"$JDK/bin/javac" -cp "$ASM" -d "$TMP" "$PROJECT_DIR/tests/JavaStockLinkageAudit.java"
# The final JAR is for decompilation: AccessInline rewrites synthetic accessors.
# Also check the pre-uninline JAR, whose callers retain the actual accessor ABI.
for STOCK in $(printf "%s\n" "$STOCK_JAR" "$STOCK_COMBINED_JAR" | uniq); do
    echo "Auditing $(basename "$STOCK") with $STOCK_JCL_JAR"
    "$JDK/bin/java" -Xmx1g -cp "$TMP:$ASM" JavaStockLinkageAudit \
        "$PROJECT_DIR/build/carplay_hook.jar" "$STOCK" \
        "$STOCK_JCL_JAR" \
        "$STOCK_DIR/libs/org.osgi.framework-1.10.0.jar" "$STOCK_DIR/libs/org.osgi.util.tracker-1.5.4.jar"
done
