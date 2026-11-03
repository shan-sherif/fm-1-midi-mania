# Game architecture

fm-1-midi-mania is a firmware for the M-VAVE FM-1, forked from [Felucca](https://github.com/hugelton/Felucca)
([README.md](../README.md#based-on-felucca)). Felucca's code supplies the sound and the hardware support. The game replaces its
groovebox side (sequencer, arpeggiator, pattern editing): the device plays a song's backing and shows the notes you
play on the keys, then judges your timing.

How the inherited code fits together (layers, the single translation unit, the host builds, storage) is in
[ARCHITECTURE.md](ARCHITECTURE.md), Felucca's document. This one covers the game and what changes.

## Layout

```
fm-1-midi-mania/
├─ firmware/
│   ├─ src/         the application: Felucca's files (ours to change now) and the game's (chart.h, judge.c, ...)
│   ├─ hal/         Felucca's own hardware headers
│   ├─ bsp/         the FM-1 hardware layer (from felucca-bsp, MPL-2.0)
│   ├─ engines/     the sound engines (from Felucca-Engines, MIT)
│   └─ loader/      the update loader
├─ platforms/       host builds: desktop (SDL2), browser, on a simulated FM-1
├─ tools/           build, generators, installer; the MIDI → chart converter (planned)
├─ tests/           tests that run on a computer
├─ web/             the installer page, the MIDI converter (mm_songs.js)
├─ assets/          fonts, icons, samples
├─ docs/            documentation
└─ Felucca/         (git-ignored) a plain Felucca clone, for reference only
```

## The single translation unit

The build compiles one file, `firmware/src/midimania.c`, which `#include`s every other `.c` file in a fixed order. The
game follows the same rule: a new file is added to that list (and to `platforms/host/firmware.c` if it is part of
the sound side the host builds too). Renaming `midimania.c` and the `FELUCCA_*` flags is on the TODO list. It touches
`tools/build.py` and every platform, so it is done in one go, once the build works.

## What we keep, replace and drop (plan)

| Felucca file(s) | Plan | Why |
| --- | --- | --- |
| bsp, hal, `lcd.c`, `gfx.c`, `audio.c`, `timer5.c`, `master.c`, `keytouch.c` | keep | the hardware, the screen, the audio ISR |
| engines, `engines.c`, `params.c`, `mod.c`, `voice.c`, `fx.c`, `click.c` | keep | the sound: 4 parts, 8 voices, FX, metronome |
| `usb.c`, `midi_uart.c`, `midi_control.c` | keep | MIDI in (playing along from a computer) |
| `storage.c`, `storage_hw.c` | keep | A/B flash sectors with CRC: settings, high scores, charts |
| `ota.c`, `ota_hw.c`, `firmware/loader/` | keep | updates over USB: never lose the way back to stock firmware |
| `seq.c` (keyboard, arp, sequencer, transport) | **replace** with `play.c` | chart playback and the keys into the judge |
| `ui*.c`, `menu_items.c`, `editor*.c` | **replace** with game screens | song list, play screen, results, settings |
| `project.c`, `song_chain.c`, `motion.c`, `upreset.c` | **drop** | patterns, songs, automation, user presets |
| arpeggiator, chord keys, SLICER | **drop** | not used by the game. The `P_A*` parameters can go once no stored format needs them |
| `main.c`, `midimania.c` | **change** | our boot, main loop and include order |

Dropping a file means removing it from the include list and fixing what referred to it. Felucca's files depend on
each other a lot (the UI reads the sequencer's state, storage saves patterns), so each removal is its own step, built
and tested before the next. The flash budget in the README says which removals matter most.

## Where the game runs

The same two contexts as Felucca:

1. **The audio interrupt** (every 128 frames, 2.9 ms). `fx.c`'s `mix_block` calls `events_block` before rendering;
   `play.c` provides it. Each block it:
   - moves the song position on by the block's samples;
   - starts and ends the **backing** notes in this block (`trk_note_on` / `trk_note_off`), with sample-accurate
     timing;
   - sounds the keys pressed (the player's part) and passes each press to the judge (`judge.c`, `judge_press`);
   - turns notes whose window has passed into misses (`judge_tick`).
2. **The main loop** (about 60 frames per second) draws the falling notes, the judgements and the score, reads the
   buttons and encoders, and saves high scores while nothing plays.

### Press timing

The key scan (`timer5.c`, 10 kHz) stamps every key closing with the time it happened (`fm1_in.note_t0[k]`). Felucca
uses this stamp to measure key latency (`seq.c` `kb_lat`). We turn it into a song-sample time for `judge_press`, so a
press is judged to about 0.1 ms, not to the 2.9 ms audio block it arrived in. On top of that, a per-player
**calibration offset** (`judge_t.offset`) corrects for how the player hears and presses.

## Charts

A song is converted on a computer from a Standard MIDI File to a **chart** ([CHART_FORMAT.md](CHART_FORMAT.md)): the
notes the player plays (each with its key, 0..26), the backing notes for the four parts, and the beat times. All the
hard decisions are made by the converter rather than the device:
- which MIDI track is the player's;
- folding notes into the 27 keys;
- fitting the backing into 4 parts and 8 voices;
- drums onto the DRUM engine.

This keeps the firmware small and the format simple.

## Flash budget

The FM-1 has **1 MiB of flash**, shared by the firmware, the update area and the data store. `firmware/app.ld` gives
the app's code and constants (including the built-in samples) `0x8DFBC` bytes, about **568 KiB**. `tools/build.py`
refuses an image that does not fit. Charts go in the data area: the region Felucca uses for user samples
(`0xA0000..0xDBFFF`, 240 KiB) is the likely home. At 12 bytes a note, a 2,000-note song is about 24 KiB.
