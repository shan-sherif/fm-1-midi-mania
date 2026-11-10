#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Felucca in the browser: the host (platforms/host/mm_host.c: the firmware against a simulated FM-1) compiled to
# WebAssembly with Emscripten, its API (mm_host.h) as the wasm's exports, plus the page (index.html, the
# AudioWorklet worklet.js). After X0X's web/emu/build.sh (charlesvestal/fm1-x0x, GPL-3.0).
#   platforms/web/build.sh   ->  build/web/{index.html, worklet.js, midi-mania.wasm, the page's modules, fonts/}
# After ./build.sh gen (build/gen). WEB_OPT replaces -O2, WEB_CFLAGS adds flags (WEB_CFLAGS=-msimd128: let the
# compiler vectorise; every browser with AudioWorklet has wasm SIMD). The page shows the version (MM_VERSION)
# marked "(preview)" before its release; WEB_PREVIEW=0 drops the mark.
set -e
cd "$(dirname "$0")/../.."
. tools/hostflags.sh
host_gen_ok web
command -v emcc >/dev/null 2>&1 || { echo "web: no emcc (Emscripten)"; exit 1; }
mkdir -p build/web/fonts
# Studio's songs: 1 MiB and 64 songs of their own past the FM-1's 1 MiB of flash (the FM-1: 240 KB, 16 songs), so a
# song loads here whatever its size; Studio says when it is too big to send (platforms/web/index.html)
# shellcheck disable=SC2086
emcc ${WEB_OPT:--O2} $HOST_CFLAGS ${WEB_CFLAGS:-} "-DMM_VERSION=\"$VERSION\"" $HOST_INC \
    '-DFH_API=__attribute__((used, visibility("default")))' -DMM_PLATFORM=FE_PLATFORM_WEB \
    -DMM_HOST_NOR=0x200000u -DMM_SONG_BASE=0x100000u -DMM_SONG_SIZE=0x100000u -DMM_SONG_MAX=64u \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/web/midi-mania.wasm platforms/host/mm_host.c
[ "${WEB_PREVIEW:-1}" = 0 ] && PREVIEW='' || PREVIEW=' (preview)'
# the build's time on the page (its footer: which Studio this is), and on the scripts it loads (?b=..: a new build's
# page never runs with an old build's scripts a browser kept)
BUILT=$(date '+%Y-%m-%d %H:%M %Z')
STAMP=$(date -u '+%Y%m%d%H%M%S')
sed -e "s/@VERSION@/$VERSION/g" -e "s/@PREVIEW@/$PREVIEW/g" -e "s/@BUILT@/$BUILT/g" \
    -e "s#from \"\./\([a-z0-9_]*\)\.js\"#from \"./\1.js?b=$STAMP\"#g" \
    -e "s#addModule(\"worklet\.js\")#addModule(\"worklet.js?b=$STAMP\")#" \
    platforms/web/index.html > build/web/index.html
cp platforms/web/worklet.js build/web/
cp web/mm_songs.js build/web/                 # (the page's MIDI converter: Your songs)
cp web/fm1ota.js web/fm1backup.js web/fm1songs.js build/web/   # (Your songs onto a real FM-1 over USB)
cp platforms/web/fonts/DotGothic16-subset.woff platforms/web/fonts/OFL.txt build/web/fonts/
echo "web: build/web ($(wc -c < build/web/midi-mania.wasm | tr -d ' ') B wasm, $VERSION)"
