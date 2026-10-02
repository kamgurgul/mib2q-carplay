#!/bin/bash
# Sourced by the Java build/test/audit scripts: where the unit's stock HMI classes,
# the public OSGi/ASM libs and the host test JDK live. Everything sits in stock/
# (gitignored; layout in stock/README.md). Requires PROJECT_DIR to be set.
#
# Overrides (env):
#   STOCK_DIR           default <repo>/stock
#   STOCK_JAR           jxe2jar output of the unit's lsd.jxe   (default $STOCK_DIR/base.jar)
#   STOCK_COMBINED_JAR  pre-uninline core+appimg jar           (default $STOCK_JAR)
#   STOCK_VF_DIR        decompiled stock sources (audit only)  (default $STOCK_DIR/vf)
#   STOCK_JCL_JAR       the unit's J9 class library            (default first $STOCK_DIR/libs/jcl/*/jcl.jar)
#   JDK                 host JDK 8 home for the host tests     (default $STOCK_DIR/jdk)

STOCK_DIR="${STOCK_DIR:-$PROJECT_DIR/stock}"
STOCK_JAR="${STOCK_JAR:-$STOCK_DIR/base.jar}"
STOCK_COMBINED_JAR="${STOCK_COMBINED_JAR:-$STOCK_JAR}"
STOCK_VF_DIR="${STOCK_VF_DIR:-$STOCK_DIR/vf}"
STOCK_JCL_JAR="${STOCK_JCL_JAR:-$(ls "$STOCK_DIR"/libs/jcl/*/jcl.jar 2>/dev/null | head -1)}"
OSGI_LIBS="$STOCK_DIR/libs/org.osgi.framework-1.10.0.jar:$STOCK_DIR/libs/org.osgi.util.tracker-1.5.4.jar"
ASM_LIBS="$STOCK_DIR/libs/asm-9.7.jar:$STOCK_DIR/libs/asm-tree-9.7.jar"
JDK="${JDK:-$STOCK_DIR/jdk}"

# stock_require <what...>: fail early with a pointer to stock/README.md.
stock_require() {
    local w
    for w in "$@"; do
        case "$w" in
            jar) [ -f "$STOCK_JAR" ] || { echo "ERROR: stock HMI jar not found: $STOCK_JAR (see stock/README.md)" >&2; exit 1; } ;;
            jdk) [ -x "$JDK/bin/javac" ] || { echo "ERROR: host JDK 8 not found: $JDK (see stock/README.md)" >&2; exit 1; } ;;
        esac
    done
}
