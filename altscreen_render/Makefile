# altscreen_render — CarPlay cluster video renderer (Audi MHI2Q)
#
# Targets:
#   make hosttest        Build+run the host decode test (needs FFmpeg on the host).
#   make host            Build the headless host renderer (tee sink, no GL).
#   make qnx             Cross-build the on-car renderer (see build_altscreen_render.sh).
#
# Host FFmpeg: set FFMPEG_PREFIX to an FFmpeg install with libavcodec/format/util
# static or shared libs (default: $HOME/alt111-host/prefix, matching the build in
# docs/deploy/altscreen-mhi2q.md). Or use pkg-config by setting USE_PKGCONFIG=1.
#
# SPDX-License-Identifier: GPL-3.0-or-later

FFMPEG_PREFIX ?= $(HOME)/alt111-host/prefix
CC     ?= cc
CFLAGS ?= -std=gnu99 -O2 -Wall -Wextra
COMMON  = ../common

ifeq ($(USE_PKGCONFIG),1)
  FF_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libavutil)
  FF_LIBS   := $(shell pkg-config --libs   libavformat libavcodec libavutil)
else
  FF_CFLAGS := -I$(FFMPEG_PREFIX)/include
  FF_LIBS   := $(FFMPEG_PREFIX)/lib/libavformat.a $(FFMPEG_PREFIX)/lib/libavcodec.a $(FFMPEG_PREFIX)/lib/libavutil.a -lm -lpthread
endif

.PHONY: hosttest host qnx clean
.DEFAULT_GOAL := host

host_decode_test: host_decode_test.c decode.c view.c decode.h
	$(CC) $(CFLAGS) $(FF_CFLAGS) host_decode_test.c decode.c view.c $(FF_LIBS) -o $@

hosttest: host_decode_test
	@echo "Run: ./host_decode_test <clip.mp4|stream.264>"

# Headless host renderer: real tee client + decode, GL stubbed out. Useful to
# validate the on-car tee over an SSH port-forward (ssh -L 19820:127.0.0.1:19820).
altscreen_render_host: main.c decode.c view.c video_gles.c omx_probe.c hw_decode.c decode.h video_gles.h omx_probe.h hw_decode.h omx_il.h
	$(CC) $(CFLAGS) $(FF_CFLAGS) main.c decode.c view.c video_gles.c omx_probe.c hw_decode.c $(FF_LIBS) -o $@

host: altscreen_render_host

qnx:
	@echo "Cross-build with: ../scripts/build_altscreen_render.sh"
	@echo "(needs the QNX FFmpeg static libs — see build_altscreen_render.sh)"

clean:
	rm -f host_decode_test altscreen_render_host *.o
