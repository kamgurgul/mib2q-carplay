#!/usr/bin/env bash
# Old mirror experiment. Not the working Virtual Cockpit image.
# Injecting stream 111 into CPAAProxyEx drops CarPlay in about 0.6s.
# The working image is ./work/tools/build_dual.sh optA (cluster_dual.c,
# -DHU_DIRECT=1), which leaves CPAAProxyEx single-screen.
#
# This still builds the experiment:
#   - cluster_inject.c mirrors accepted video bytes onto the partner socket
#   - NOP the type-gate in CPAAProxyEx
# Output: work/out/hw301_v1129.tar. Do not flash it as the cockpit image.
set -euo pipefail
cd "$(dirname "$0")/../.."
WRAP_LIB=libCoreUtils.so CHECK_SYM=CFArrayEnsureCreatedAndAppend CLUSTER_SRC=cluster_inject.c \
  PATCH_CPAA="0x4c9a8:00 0x4c9a9:f0 0x4c9aa:20 0x4c9ab:e3" ./work/tools/build.sh 129
