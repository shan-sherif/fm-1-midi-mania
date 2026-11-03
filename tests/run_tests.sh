#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; 2026 Shan-Sherif (Midi-Mania)
# Host tests of the Midi-Mania sources (no hardware). Run from the repo root after ./build.sh (or ./build.sh gen):
#   tests/run_tests.sh
#
# Midi-Mania's own: the timing judge, the song player (the songs in midi/, built in), the game's UI (SONGS, the
#                   play page, GAME, METRONOME, the parts and their sounds).
# From Felucca, for the code Midi-Mania keeps:
#   regression      tests/regress.c: every built engine x preset, the GM map on DRUM, voice modes, the REV send, a
#                   4-part mix: one hash each in tests/golden.txt; health (clipping, DC, voices freed); CPU per preset
#                   (tests/cpu_baseline.txt, +25 %); the same on one core and with the second core taking parts.
#                   A change of the sound fails with the list of renders: GOLDEN_UPDATE=1 to take the new ones after a
#                   review. target cost: the render loops in the FM-1's disassembly (tests/target_budget.txt, +10 %).
#   engines         DRUM, FM6, PHYS (and its fixed-point models), SAMPLE's PIANO, the DIGITAL -> FM6 conversion of old
#                   sounds, the modulation matrix, the speaker EQ.
#   UI drawing      the palettes, the text renderer against its reference, the glyph spacing.
#   device          keys and buttons (debounce, encoders, LEDs), TRS MIDI (parser, timestamps, UART DMA), USB audio
#                   and descriptors, the metronome and count-in, audio overload, storage (A/B, torn writes), settings,
#                   persistence, backups, the FM6 voice bank, the editor protocol, crafted stored data; the update path
#                   (with the FM-1 package: ./build.sh).
#   sanitizers      ASan + UBSan over the stored-data and protocol paths and three short fuzz runs (SANITIZE=0 skips).
#   web             the installer's package builder and update protocol (node); the browser build against the native
#                   one (emcc); the desktop build (SDL2).
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
[ -d "$HOME/emsdk/node" ] && ! command -v node >/dev/null 2>&1 && PATH="$(ls -d "$HOME"/emsdk/node/*/bin | tail -n 1):$PATH"   # (WSL: the SDK's node)
OUT=build/host
mkdir -p "$OUT"
# the hardware layer: firmware/hal, then felucca-bsp (firmware/bsp: chip/, boards/fm1/), as tools/build.py
HALI="-Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 -Ifirmware/src"   # (src: uac_fir.h, which bsp/chip/usb.c includes)
CC="${CC:-cc} -O1 -Wall -Wno-unused-function $HALI"
fail=0
run() { echo "== $1"; n="$1"; shift; "$@" || { fail=1; echo "!! FAILED: $n"; }; }

FW=0
[ -f build/midi-mania.fwsc ] && FW=1
[ $FW = 1 ] || [ -f build/gen/mm_tables.h ] || { echo "run ./build.sh (or ./build.sh gen) first"; exit 1; }

# ---------------------------------------------------------------------------------------------- Midi-Mania ---
${CC%% *} -std=gnu99 -Wall -Wextra -o "$OUT/judge_test" tests/judge_test.c
run "judge: windows, misses, combo, calibration" "$OUT/judge_test"
rm -rf "$OUT/charts"
python3 tools/gen_songs.py build/gen/midimania_songs.h --charts "$OUT/charts" > /dev/null   # (the songs as charts too)
$CC -w -std=gnu99 -Ibuild/gen -o "$OUT/player_test" tests/player_test.c -lm   # (-w: the bsp headers' casts, as the others)
run "player: a built-in song through the audio path, timing, voices, tempo, drum keys, metronome, count-in, PLAY YOU, RANGE; stored songs (charts in flash, gen_songs.py's byte for byte)" \
    sh -c "'$OUT/player_test' '$OUT'/charts/*.mmc"
$CC -w -DUI_TEST_MM_ONLY -Ibuild/gen -o "$OUT/ui_test_mm" tests/ui_test.c -lm
run "UI: the parts' sounds, the panel, SONGS, the play page, GAME, METRONOME" "$OUT/ui_test_mm"

# -------------------------------------------------------------------------------------------------- device ---
$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
$CC -Ibuild/gen -o "$OUT/signed_audio_scaling_test" tests/signed_audio_scaling_test.c -lm
run "DSP signed arithmetic: fixed DC/lowcut/FM6 vectors and phase wrap" "$OUT/signed_audio_scaling_test"
$CC -Ibuild/gen -o "$OUT/phys_signed_test" tests/phys_signed_test.c -lm
run "PHYS: signed output and frozen baseline render" "$OUT/phys_signed_test"
$CC -Ibuild/gen -o "$OUT/uart_dma_test" tests/uart_dma_test.c -lm
run "TRS UART: pending events, stale counts, exact packets and continuous traffic" "$OUT/uart_dma_test"
$CC -o "$OUT/upreset_test" tests/upreset_test.c
run "user presets: parser, bank round trip, versions" "$OUT/upreset_test"
$CC -o "$OUT/input_test" tests/input_test.c
run "keys and buttons: fast press, long release, bouncy contacts (one note each), glitches, encoders" "$OUT/input_test"
$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"
$CC -O2 -o "$OUT/timer5_midi_test" tests/timer5_midi_test.c
run "TRS timestamping during audio with saturated CC input" "$OUT/timer5_midi_test"
$CC -O2 -DMM_UART=0 -o "$OUT/timer5_midi_nouart_test" tests/timer5_midi_test.c
run "TIMER5 with TRS input disabled" "$OUT/timer5_midi_nouart_test"

HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/core.h)
$CC -O2 -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/uac_test.c -lm
run "USB audio input: descriptors (with CDC), ring and packets, 44.1 -> 48 kHz resampler, 48 kHz stream" "$OUT/uac_test"
$CC -O2 -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/uac_test.c -lm
run "USB audio input: descriptors (without CDC), ring and packets, resampler, 48 kHz stream" "$OUT/uac_test_nocdc"
# USB descriptor layouts (#67): CDC UAC LAYOUT CDC-presented 48K, and the AC collection's audio streaming interface
# first (MM_UAC_AS_FIRST, the default) or MIDI first (T_ASF=0)
for v in 1.1.0.1.1 1.1.1.1.1 1.1.2.1.1 1.1.3.1.1 1.1.0.0.1 1.1.2.0.1 0.1.0.1.1 1.1.0.1.0 1.1.0.0.0 1.1.2.1.0 0.1.0.1.0 \
         1.0.0.1.1 1.0.1.1.1 1.0.0.0.1 0.0.0.1.1; do
    IFS=. read -r t_cdc t_uac t_lay t_on t_48 <<EOF
$v
EOF
    $CC -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -DT_48K="$t_48" -o "$OUT/usb_desc_test" \
        tests/usb_desc_test.c
    run "USB descriptors: CDC $t_cdc (presented $t_on), UAC $t_uac (48 kHz $t_48), layout $t_lay" "$OUT/usb_desc_test"
done
for v in 1.1.0.1.1 1.1.0.0.1 1.1.0.1.0 1.1.0.0.0 0.1.0.1.1; do
    IFS=. read -r t_cdc t_uac t_lay t_on t_48 <<EOF
$v
EOF
    $CC -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -DT_48K="$t_48" -DT_ASF=0 \
        -o "$OUT/usb_desc_test" tests/usb_desc_test.c
    run "USB descriptors (AC collection MIDI first): CDC $t_cdc (presented $t_on), UAC $t_uac (48 kHz $t_48)" \
        "$OUT/usb_desc_test"
done

# the FM-1's package (./build.sh): the update path; without it (./build.sh gen only, as CI's host job) skipped
if [ $FW = 1 ]; then
    $CC -DOWN_PKG=1 -o "$OUT/ota_test" tests/ota_test.c
    run "M-UPGRADE entry (own loader)" "$OUT/ota_test" build/midi-mania.fwsc
    head -c 200000 build/midi-mania.bin > "$OUT/old_app.bin"
    python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
    $CC -o "$OUT/ldr_test" tests/ldr_test.c
    run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/midi-mania.fwsc
else
    echo "== skip the update path (no build/midi-mania.fwsc: ./build.sh makes it)"
fi

# ---------------------------------------------------------------------------------------- sound and the UI ---
T="$CC -w -Ibuild/gen"
$T -O2 -o "$OUT/hostsim" tests/hostsim.c -lm
run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
$T -O2 -o "$OUT/speaker_test" tests/speaker_test.c -lm
run "SPEAKER EQ: FLAT / LOWCUT / BASS+ responses, the sub cut, no offset after, headroom on a full-scale kick" \
    "$OUT/speaker_test"
$T -O1 -o "$OUT/midi_drift_test" tests/midi_drift_test.c -lm
run "External-clock long-run phase: five 20-minute USB/TRS cases" "$OUT/midi_drift_test"
$T -O2 -fsanitize=integer-divide-by-zero -fno-sanitize-recover=all -o "$OUT/fm4_div0_test" tests/fm4_div0_test.c -lm
run "old DIGITAL sounds -> FM6 without a divide by zero (#61), a 0.9 ORGAN project (UBSan)" "$OUT/fm4_div0_test"
$T -o "$OUT/theme_test" tests/theme_test.c -lm
run "themes: contrast, text blending and font metrics" "$OUT/theme_test"
$T -o "$OUT/text_ref_test" tests/text_ref_test.c -lm
run "text: pens, kerning and pixels equal the reference renderer" "$OUT/text_ref_test"
if python3 -c "import PIL" 2>/dev/null; then
    run "text spacing: glyph gaps against the font's own (16x supersampled), S M <= 0.25 px, L <= 0.5 px" \
        python3 tests/text_spacing_test.py build/gen/ui_fonts.h
fi
$T -o "$OUT/settings_test" tests/settings_test.c
run "settings: PER1..PER4 migration, palette ids and preference preservation" "$OUT/settings_test"
$T -o "$OUT/audio_test" tests/audio_test.c -lm
run "audio: overload protection, bounded fades and DMA diagnostics" "$OUT/audio_test"
$T -o "$OUT/persistence_test" tests/persistence_test.c -lm
run "persistence: deferred settings, retry and failed-save rollback" "$OUT/persistence_test"
$T -o "$OUT/backup_test" tests/backup_test.c -lm
run "full backup: CRC before writes, stale runtime, USB reset / timeout, malformed objects" "$OUT/backup_test"
$T -o "$OUT/fm6bank_test" tests/fm6bank_test.c -lm
run "FM6 voice bank: write / read, power cuts, CRC, SLOT B1..B32" "$OUT/fm6bank_test"
$T -DMM_NO_USER_SAMPLES=0 -o "$OUT/editor_test" tests/editor_test.c -lm   # (with Felucca's sample slots: their code)
run "editor protocol: real C protocol, malformed transfers, queue recovery, MENU settings" \
    env MENU_JSON="$OUT/menu.json" "$OUT/editor_test"
$T -DMM_NO_USER_SAMPLES=0 -o "$OUT/robust_test" tests/robust_test.c -lm   # (Felucca's sample slots, off in Midi-Mania: their code)
run "robustness: crafted sample slots, engine numbers, retained old projects, preset patterns, malformed requests" \
    "$OUT/robust_test"
$T -O2 -o "$OUT/mod_test" tests/mod_test.c -lm
mkdir -p build/mod_demo
run "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, cost, demos" "$OUT/mod_test" build/mod_demo
$T -O2 -o "$OUT/regress" tests/regress.c -lm
run "regression: golden renders, health, voices, CPU budget (one core)" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
$T -O2 -DMM_CPU2=0 -o "$OUT/regress_cpu2_0" tests/regress.c -lm
run "regression, built without the second core: the same golden renders and CPU budget" \
    "$OUT/regress_cpu2_0" tests/golden.txt tests/cpu_baseline.txt
$T -O2 -DC1_HOST -pthread -o "$OUT/regress_c1" tests/regress.c -lm
run "second core: the golden renders with a thread taking every split of the parts in turn" \
    "$OUT/regress_c1" tests/golden.txt tests/cpu_baseline.txt
$T -O2 -pthread -o "$OUT/dualcore_test" tests/dualcore_test.c -lm
run "second core: 4 parts of every preset, mixes: bit for bit on one core and on two, every split" "$OUT/dualcore_test"
$T -o "$OUT/descdump" tests/descdump.c -lm
echo "== parameter and engine tables as JSON (for the web tests)"
"$OUT/descdump" > "$OUT/desc.json" || fail=1
$T -O2 -o "$OUT/phys_test" tests/phys_test.c -lm
mkdir -p build/phys_demo
run "PHYS: stability C-1..G9 over the parameter corners, worst-case cost, demos" "$OUT/phys_test" build/phys_demo
D=${DAISYSP:-vendor/DaisySP}/Source
if [ -d "$D/PhysicalModeling" ] && command -v c++ >/dev/null 2>&1; then
    $T -O2 -Itests -c -o "$OUT/phys_fixed.o" tests/phys_fixed.c
    c++ -O2 -std=c++14 -w -I"$D" -I"$D/Utility" -o "$OUT/phys_ref" tests/phys_ref.cpp \
        "$D/PhysicalModeling/modalvoice.cpp" "$D/PhysicalModeling/resonator.cpp" "$D/PhysicalModeling/stringvoice.cpp" \
        "$D/PhysicalModeling/KarplusString.cpp" "$D/Filters/svf.cpp" "$D/Utility/dcblock.cpp" \
        "$D/Dynamics/crossfade.cpp" "$OUT/phys_fixed.o"
    run "PHYS: the fixed-point models against DaisySP's float originals (mode frequencies, decays, Svf)" "$OUT/phys_ref"
else
    echo "== skip PHYS reference test (no DaisySP: set DAISYSP to a checkout)"
fi
$T -O2 -o "$OUT/drum_test" tests/drum_test.c -lm
mkdir -p build/drum_demo
run "DRUM: voice targets, controls, no clipping, retrigger, hat choke, the kick on a small speaker, keys, 8 lanes, cost, demos" \
    "$OUT/drum_test" build/drum_demo
$T -O2 -o "$OUT/fm6_test" tests/fm6_test.c -lm
mkdir -p build/fm6_demo
run "FM6: algorithms, envelopes, retrigger, DC, clipping, macros, patch formats, voices, cost, demos" "$OUT/fm6_test" build/fm6_demo
if grep -q '^    {"PIANO", 0, 5},' build/gen/mm_samples.h; then   # SAMPLE's PIANO, and 1.2's PIANO HD in a user slot
    mkdir -p build/sample_demo build/piano_hd
    python3 tools/gen_samples.py --user-slot PIANO "PIANO HD" build/piano_hd/PIANO_HD >/dev/null
    $T -O2 -DMM_NO_USER_SAMPLES=0 -o "$OUT/sample_test" tests/sample_test.c -lm   # (a user slot: Felucca's, off in Midi-Mania)
    run "SAMPLE: PIANO (5 zones: splits, pitch, length), a PIANO HD slot" "$OUT/sample_test" \
        build/piano_hd/PIANO_HD build/sample_demo
else
    echo "== skip SAMPLE's PIANO (no CC0 library: assets/samples-cc0, tools/fetch_cc0.py)"
fi

# ---------------------------------------------------------------------------------------------- sanitizers ---
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize=signed-integer-overflow,shift,bounds,object-size,pointer-overflow"
SAN="$SAN -fno-sanitize-recover=undefined -w"
san_ok() {
    printf 'int main(void){return 0;}\n' > "$OUT/san_probe.c" &&
        ${CC%% *} $SAN -o "$OUT/san_probe" "$OUT/san_probe.c" 2>/dev/null && "$OUT/san_probe"
}
if [ "${SANITIZE:-1}" = 0 ]; then
    echo "== skip the sanitizer runs (SANITIZE=0)"
elif ! san_ok; then
    echo "== skip the sanitizer runs (the compiler has no ASan / UBSan)"
else
    mkdir -p "$OUT/asan"
    A="$OUT/asan"
    SCC="${CC%% *} $SAN $HALI -Ibuild/gen"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    STRICT_CC="${CC%% *} -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -w $HALI -Ibuild/gen"
    for t in signed_audio_scaling_test phys_signed_test uart_dma_test; do
        $STRICT_CC -o "$A/${t}_strict" tests/$t.c -lm
        run "Strict ASan/UBSan (no exclusions): $t" "$A/${t}_strict"
    done
    if [ $FW = 1 ]; then
        $SCC -o "$A/ldr_test" tests/ldr_test.c
        run "ASan/UBSan: update loader (other app -> this build)" "$A/ldr_test" "$OUT/old.fwsc" build/midi-mania.fwsc
        $SCC -DOWN_PKG=1 -o "$A/ota_test" tests/ota_test.c
        run "ASan/UBSan: M-UPGRADE entry (own loader)" "$A/ota_test" build/midi-mania.fwsc
    fi
    $SCC -DMM_NO_USER_SAMPLES=0 -o "$A/robust_test" tests/robust_test.c -lm
    run "ASan/UBSan: robust_test" "$A/robust_test"
    $SCC -DMM_NO_USER_SAMPLES=0 -o "$A/editor_test" tests/editor_test.c -lm
    run "ASan/UBSan: editor_test" "$A/editor_test"
    for t in backup_test fm6bank_test midi_drift_test player_test; do
        $SCC -o "$A/$t" tests/$t.c -lm
        run "ASan/UBSan: $t" "$A/$t"
    done
    $SCC -o "$A/fuzz_ed" tests/fuzz_ed.c -lm
    run "ASan/UBSan fuzz: editor SysEx and raw USB-MIDI packets (20000, seed 7)" "$A/fuzz_ed" 20000 7
    $SCC -o "$A/fuzz_proj" tests/fuzz_proj.c -lm
    run "ASan/UBSan fuzz: stored data -> import -> restore -> render (5000, seed 13)" "$A/fuzz_proj" 5000 13
    $SCC -o "$A/fuzz_smp" tests/fuzz_smp.c -lm
    run "ASan/UBSan fuzz: user sample slot headers -> scan -> SAMPLE (1000, seed 17)" "$A/fuzz_smp" 1000 17
fi

if [ $FW = 1 ]; then
    run "regression: target cost of the render loops (pi32v2 disassembly)" python3 tests/target_budget.py \
        build/midi-mania.dis tests/target_budget.txt
fi

# --------------------------------------------------------------------------------------- tools, web, desktop ---
run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
if command -v node >/dev/null 2>&1; then
    run "web pages: package builder, update protocol" node web/test_web.mjs
    run "web backup: capture, validation before writes, restore order" node web/test_backup.mjs
    run "the converter in the browser (web/mm_songs.js) = tools/gen_songs.py, chart for chart, byte for byte; damaged files refused" \
        node web/test_songs.mjs "$OUT/charts" assets/songs midi
else
    echo "== skip web tests (no node)"
fi
if ! command -v emcc >/dev/null 2>&1 || ! command -v node >/dev/null 2>&1; then
    echo "== skip the browser build (needs emcc and node)"
else
    run "browser build: the firmware to WebAssembly (platforms/web/build.sh -> build/web)" sh platforms/web/build.sh
    cc -O2 -ffp-contract=off -w $HALI -Ibuild/gen -o "$OUT/emu_native" platforms/web/native_check.c -lm
    run "browser build: the same song from the native build" "$OUT/emu_native" "$OUT/emu_native.f32"
    run "browser build: boot, keys, MIDI, screen, LEDs, PLAY, a save kept across instances, = native, cost" \
        node platforms/web/emu_test.mjs build/web/midi-mania.wasm "$OUT/emu_native.f32"
fi
if command -v sdl2-config >/dev/null 2>&1; then
    run "desktop build: platforms/sdl/build.sh -> build/sdl" sh platforms/sdl/build.sh
    run "desktop build: plays a song, draws, saves a setting (headless)" build/sdl/midi-mania --headless 2000 --no-midi
else
    echo "== skip the desktop build (no SDL2)"
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
