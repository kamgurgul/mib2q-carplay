#!/usr/bin/env bash
# Build the HW301 dual-screen (Virtual Cockpit) images from cluster_dual.c.
# No dongle shell needed: the step is baked in at compile time, one image each.
#
#   build_dual.sh [step]      step = 1 | 2 | 3 | all   (default: all)
#
#   step 1 -> hw301_v1132.tar  advertise cluster display + log SETUP/keys only
#   step 2 -> hw301_v1133.tar  + open 111 listener, append dataPort, decrypt, count
#   step 3 -> hw301_v1134.tar  + inject 111 to the head unit and relay (full)
#
# Flash ONE at a time on BENCH power, car-test, getlogs, then the next step:
#   ./work/.venv/bin/python work/tools/flash_local.py work/out/hw301_v<code>.tar --version <code>
# Recover any time by flashing stock fw/hw301_v129.tar --version 129.
set -euo pipefail
cd "$(dirname "$0")/../.."

build_step() {
  local step="$1" code="$2" advert="${3:-3}"
  echo "######## building dual-screen STEP $step ADVERT=$advert -> v$code ########"
  WRAP_LIB=libCoreUtils.so \
  CHECK_SYM=AirPlayReceiverSessionSetup \
  CLUSTER_SRC=cluster_dual.c \
  CLUSTER_CFLAGS="-DCLUSTER_STEP=$step -DCLUSTER_ADVERT=$advert" \
    ./work/tools/build.sh 129 "$code"
}

# Diagnostic step-1 variants (2026-10-19): iOS ends the session right after our
# modified /info. CLUSTER_ADVERT isolates the cause. Flash one per car trip.
#   advert0 (v1140) stock /info, logged only  -> MUST keep CarPlay up (control)
#   advert1 (v1141) merge enabledFeatures only
#   advert2 (v1142) append the cluster display only
case "${1:-all}" in
  1) build_step 1 1132 ;;
  2) build_step 2 1133 ;;
  3) build_step 3 1134 ;;
  advert0) build_step 1 1140 0 ;;
  advert1) build_step 1 1141 1 ;;
  advert2) build_step 1 1142 2 ;;
  advert)
    build_step 1 1140 0
    build_step 1 1141 1
    build_step 1 1142 2
    echo "######## advert diagnostics v1140/v1141/v1142 in work/out/ ########"
    ;;
  all)
    build_step 1 1132
    build_step 2 1133
    build_step 3 1134
    echo "######## all three images in work/out/ ########"
    ;;
  *) echo "usage: $0 [1|2|3|all|advert|advert0|advert1|advert2]"; exit 1 ;;
esac
