#!/usr/bin/env bash
# Build the HW301 dual-screen (Virtual Cockpit) image from cluster_dual.c.
# No dongle shell needed: every option is baked in at compile time.
#
#   build_dual.sh [target]      target = release | release-input | optA | optA-input
#
#   release        -> hw301_v1162.tar  shipped image: HU_DIRECT, diagnostics off
#   release-input  -> hw301_v1163.tar  same, Input ChaCha key (fallback only)
#   optA           -> hw301_v1160.tar  same version as release (v1162) + enabled logging
#   optA-input     -> hw301_v1155.tar  optA with the Input key (fallback only)
#
# Flash on BENCH power only:
#   python3 work/tools/flash_local.py work/out/hw301_v<code>.tar --version <code>
# Roll back any time with fw/hw301_v129.tar --version 129 (see README.md).
#
# Every target keeps HU_DIRECT=1. A step-3 image without it injects stream 111
# into CPAAProxyEx and kills the session in ~0.6 s (v1150). Do not build one.
set -euo pipefail
cd "$(dirname "$0")/../.."

build_step() {
  local code="$1" extra="${2:-}"
  echo "######## building dual-screen ${extra:-} -> v$code ########"
  WRAP_LIB=libCoreUtils.so \
  CHECK_SYM=AirPlayReceiverSessionSetup \
  CLUSTER_SRC=cluster_dual.c \
  CLUSTER_CFLAGS="-DCLUSTER_STEP=3 -DCLUSTER_ADVERT=3 -DSTEP_DECRYPT=1 -DHU_DIRECT=1 ${extra}" \
    ./work/tools/build.sh 129 "$code"
}

case "${1:-release}" in
  release)       build_step 1162 "-DCLUSTER_DIAG=0" ;;
  release-input) build_step 1163 "-DCLUSTER_DIAG=0 -DCLUSTER_KEY_INPUT=1" ;;
  optA)          build_step 1160 ;;
  optA-input)    build_step 1155 "-DCLUSTER_KEY_INPUT=1" ;;
  *) echo "usage: $0 [release|release-input|optA|optA-input]"; exit 1 ;;
esac
