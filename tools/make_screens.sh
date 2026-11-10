#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 fm-1-midi-mania contributors
# The game's pictures for the README and the site, from the host build (tools/make_screens.c: the built-in songs
# played with every note hit on time):
#   screen-keys27.png screen-keys49.png screen-drums.png screen-guitar-strings.png screen-guitar-chromatic.png
#   screen-bass-strings.png screen-songs.png screen-sounds.png screen-sounds-list.png   480 x 480, each pixel doubled (sharp)
#   gameplay-keys.gif                                            FM Keys on 27 KEYS, 8 s at 15 frames a second
#   tools/make_screens.sh [OUT_DIR]      (default web/site_media/, the site's media/ and the README's pictures)
# Needs a C compiler and Python 3 with Pillow (the build's venv, ~/.venvs/midi-mania, when it is there). Run it again
# after a change to the game's screens; the songs are the built-in ones (assets/songs/, midi/).
set -e
cd "$(dirname "$0")/.."
OUT="${1:-web/site_media}"
[ -d "$HOME/.venvs/midi-mania/bin" ] && PATH="$HOME/.venvs/midi-mania/bin:$PATH" && export PATH
PY="${PYTHON:-python3}"
"$PY" -c "import PIL" 2>/dev/null || { echo "make_screens: Python's Pillow is needed (BUILDING.md)"; exit 1; }
[ -f build/gen/midimania_songs.h ] || ./build.sh gen
HALI="-Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 -Ifirmware/src"
# shellcheck disable=SC2086
${CC:-cc} -O1 -w -std=gnu99 $HALI -DUI_TEST_MM_ONLY -Ibuild/gen -o build/make_screens tools/make_screens.c -lm
FRAMES=build/screens
rm -rf "$FRAMES"
mkdir -p "$FRAMES" "$OUT"
build/make_screens "$FRAMES"
"$PY" - "$FRAMES" "$OUT" <<'EOF'
import sys
from pathlib import Path
from PIL import Image
src, out = Path(sys.argv[1]), Path(sys.argv[2])
for p in sorted(src.glob("view_*.ppm")):                          # each view: its pixels doubled, as PNG
    name = "screen-" + p.stem[len("view_"):].replace("_", "-") + ".png"
    Image.open(p).convert("RGB").resize((480, 480), Image.NEAREST).save(out / name, optimize=True)
    print(f"make_screens: {out / name}")
frames = [Image.open(p).convert("RGB").resize((480, 480), Image.NEAREST) for p in sorted(src.glob("gif_keys_*.ppm"))]
pal = frames[0].quantize(colors=128, method=Image.Quantize.MEDIANCUT)   # (one palette: no flicker between frames)
q = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
q[0].save(out / "gameplay-keys.gif", save_all=True, append_images=q[1:], duration=67, loop=0, optimize=True, disposal=1)
print(f"make_screens: {out / 'gameplay-keys.gif'} ({len(q)} frames, {(out / 'gameplay-keys.gif').stat().st_size} B)")
EOF
