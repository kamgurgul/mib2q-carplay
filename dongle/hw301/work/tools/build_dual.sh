#!/usr/bin/env bash
# Build the HW301 dual-screen (Virtual Cockpit) image from cluster_dual.c.
# No dongle shell needed: every option is baked in at compile time.
#
#   build_dual.sh [target]      target = release | release-input | optA | optA-input
#
#   release        -> hw301_v1178.tar  native proxy + route guidance bridge + car view at phone reconnect (#68), diagnostics off
#   release-input  -> hw301_v1174.tar  Input ChaCha key diagnostic
#   optA           -> hw301_v1179.tar  same as release + logging
#   optA-input     -> hw301_v1175.tar  logging + Input key diagnostic
#
# Every target replaces the single-screen proxy boundary. Stream 111 uses
# native /info, SETUP and /command, with its own socket and encryption state.
# Builds produce images only; no flashing or device access.
set -euo pipefail
cd "$(dirname "$0")/../.."

build_step() {
  local code="$1" extra="${2:-}"
  echo "######## building dual-screen ${extra:-} -> v$code ########"
  WRAP_LIB=libCoreUtils.so \
  CHECK_SYM=AirPlayReceiverSessionSetup \
  CLUSTER_SRC=cluster_dual.c \
  CLUSTER_CFLAGS="-DCLUSTER_STEP=3 -DCLUSTER_ADVERT=3 -DSTEP_DECRYPT=1 ${extra}" \
    ./work/tools/build.sh 129 "$code"
}

case "${1:-release}" in
  release)       build_step 1178 "-DCLUSTER_DIAG=0" ;;
  release-input) build_step 1174 "-DCLUSTER_DIAG=0 -DCLUSTER_KEY_INPUT=1" ;;
  optA)          build_step 1179 ;;
  optA-input)    build_step 1175 "-DCLUSTER_KEY_INPUT=1" ;;
  *) echo "usage: $0 [release|release-input|optA|optA-input]"; exit 1 ;;
esac
