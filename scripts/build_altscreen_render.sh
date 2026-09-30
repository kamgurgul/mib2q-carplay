#!/bin/bash
# Cross-build altscreen_render (CarPlay cluster video renderer) for QNX 6.5 ARMv7
# in the qnx65-armv7-toolchain Docker image (same image as build_renderers.sh).
#
#   ./scripts/build_altscreen_render.sh
#
# Output: build/altscreen_render
#
# It first builds a minimal static FFmpeg 6.1.5 for QNX (H.264 decoder + parser
# only — the on-car renderer is fed Annex-B directly by the AltScreen hook tee,
# so no demuxer/bitstream-filter is needed) into build/ffmpeg-qnx, then compiles
# the renderer + the shared cluster_surface and links EGL/GLES/screen the same
# way the maneuver renderer does (synthesised import stubs; the ELF binds the
# unit's real libraries at runtime).
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -e

IMG=qnx65-armv7-toolchain:latest
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
FFMPEG_VERSION="${FFMPEG_VERSION:-6.1.5}"
FFMPEG_SHA256="${FFMPEG_SHA256:-b8c8e926b948c14df1264cd0beac1c773df9170ac9cac97bdf1275cd3d385902}"

if ! docker image inspect "$IMG" >/dev/null 2>&1; then
    echo "ERROR: docker image '$IMG' not found."
    echo "Build it once from https://github.com/luka-dev/qnx65-armv7-toolchain : ./host-scripts/qnx-run.sh build"
    exit 1
fi

echo "=== altscreen_render QNX build (Docker $IMG) ==="

docker run --rm --platform=linux/amd64 \
  -e FFMPEG_VERSION="$FFMPEG_VERSION" -e FFMPEG_SHA256="$FFMPEG_SHA256" \
  -v "$PROJECT_DIR":/src "$IMG" bash -c '
  set -e
  export PATH=/opt/qnx650/host/linux/x86/usr/bin:$PATH
  export QNX_HOST=/opt/qnx650/host/linux/x86 QNX_TARGET=/opt/qnx650/target/qnx6
  CC=arm-unknown-nto-qnx6.5.0eabi-gcc
  AR=arm-unknown-nto-qnx6.5.0eabi-ar
  CFLAGS_TARGET="-D_QNX_SOURCE -march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3-d16 -O2"

  # ---- minimal static FFmpeg for QNX (H.264 decode + parse only) ----
  # Build under a CONTAINER-LOCAL path, never the bind mount: the QNX SDP ar/ld
  # are 32-bit binaries without large-file support and hit EOVERFLOW ("Value too
  # large for defined data type") stat-ing intermediate .o files on the Docker
  # Desktop /mnt/c 9p mount. Only the final linked binary is written to /src.
  FF=/opt/ffmpeg-qnx
  mkdir -p "$FF"
  TAR="$FF/ffmpeg-$FFMPEG_VERSION.tar.gz"
  DIR="$FF/ffmpeg-$FFMPEG_VERSION"
  if [ ! -f "$TAR" ]; then
    curl -fL --retry 3 "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.gz" -o "$TAR"
  fi
  echo "$FFMPEG_SHA256  $TAR" | sha256sum -c -
  if [ ! -f "$DIR/configure" ]; then
    rm -rf "$DIR"; mkdir -p "$DIR"; tar xf "$TAR" -C "$DIR" --strip-components=1
  fi
  if [ ! -f "$DIR/libavcodec/libavcodec.a" ]; then
    ( cd "$DIR" && ./configure \
        --cc="$CC" --ar="$AR" --ld="$CC" --arch=arm --target-os=qnx \
        --enable-cross-compile --disable-asm --disable-debug --disable-doc \
        --disable-programs --disable-avdevice --disable-avformat --disable-swresample \
        --disable-swscale --disable-postproc --disable-avfilter --disable-network \
        --disable-everything \
        --enable-decoder=h264 --enable-parser=h264 \
        --extra-cflags="-include stddef.h $CFLAGS_TARGET" \
      && make -j"$(nproc)" )
  fi

  cd /src/altscreen_render
  ABI_INCLUDE=/src/toolchain/qnx65-abi/include
  SRCS="main.c decode.c video_gles.c ../common/cluster_surface.c"

  gen_stub(){ local so="$1" rx="$2"; shift 2
    grep -rhoE "$rx" "$@" 2>/dev/null | sort -u | sed "s/.*/int &(){return 0;}/" > /tmp/st_$so.c
    $CC -shared -fPIC -Wl,-soname,"$so" /tmp/st_$so.c -o /tmp/"$so"
  }
  gen_stub libscreen.so.1 "\bscreen_[a-z_]+"       $SRCS
  gen_stub libEGL.so.1    "\begl[A-Z][A-Za-z0-9]+"  $SRCS
  gen_stub libGLESv2.so.1 "\bgl[A-Z][A-Za-z0-9]+"   $SRCS

  $CC $CFLAGS_TARGET -std=gnu99 -Wall -D__QNX__ -DPLATFORM_QNX \
      -fdata-sections -ffunction-sections \
      -I. -I../common -I"$ABI_INCLUDE" -I"$DIR" \
      $SRCS \
      "$DIR/libavcodec/libavcodec.a" "$DIR/libavutil/libavutil.a" \
      -o /src/build/altscreen_render \
      -Wl,--gc-sections -Wl,--allow-shlib-undefined \
      -L/tmp -l:libscreen.so.1 -l:libEGL.so.1 -l:libGLESv2.so.1 -lsocket -lm

  # Verify on a container-local copy: the 32-bit SDP readelf hits EOVERFLOW
  # stat-ing the output on the Docker Desktop /mnt/c 9p mount.
  cp /src/build/altscreen_render /tmp/_ar_check
  m=$(arm-unknown-nto-qnx6.5.0eabi-readelf -h /tmp/_ar_check | awk -F: "/Machine/{print \$2}" | tr -d " ")
  rm -f /tmp/_ar_check
  echo "altscreen_render: machine=$m"
  [ "$m" = ARM ] || exit 1
'

echo ""
echo "Done:"
ls -lh "$PROJECT_DIR/build/altscreen_render"
