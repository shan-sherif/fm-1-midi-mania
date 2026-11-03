# Architecture

Midi-Mania (from Felucca) is one program built for several platforms. The FM-1 is the reference. The other platforms
build the same sources against a simulated FM-1.

```
firmware/src           the application: engines' host, voices, FX, sequencer, UI, storage, MIDI, editor protocol
 ├─ firmware/engines   Felucca-Engines (MIT): ANALOG, the shared DSP, felucca_engine.h
 ├─ the FM-1           firmware/hal + firmware/bsp (felucca-bsp, MPL-2.0): the hardware behind the fm1_* calls
 └─ platforms/host     a simulated FM-1 behind the same calls, and its API mm_host.h
      ├─ platforms/web        WebAssembly in an AudioWorklet
      ├─ platforms/sdl        a desktop window (SDL2)
      └─ platforms/common     what the frontends share: the panel, input, the flash file, MIDI
```

## The layers

| Layer | Where | What it is | Licence |
| --- | --- | --- | --- |
| Application | `firmware/src/` | The game (`player.c`, `ui_songs.c`, `judge.c`) and what it keeps of Felucca: the engines' host (`engines.c`, `voice.c`), the FX, the UI, storage and projects, MIDI, the web editor's protocol. C with no register access: the FM-1's glue (`main.c`, `lcd.c`, the `*_hw.c` files) calls the HAL, which the host simulates. | GPL-3.0-only |
| Engines | `firmware/engines/` ([Felucca-Engines](https://github.com/hugelton/Felucca-Engines)) | ANALOG, the shared DSP and tables, and the engine interface (`felucca_engine.h`). | MIT |
| FM-1 hardware | `firmware/bsp/` ([felucca-bsp](https://github.com/hugelton/felucca-bsp)), `firmware/hal/` | The chip (clock, interrupts, flash, USB, the second core, the boot entry, libc) and the FM-1's board (keys, LEDs, audio, display, ADC). The only place with register access (`tools/build.py` enforces it). | MPL-2.0 (bsp), GPL-3.0-only (hal) |
| Update loader | `firmware/loader/` | The small program that installs a package. | GPL-3.0-only |
| Host | `platforms/host/` | A simulated FM-1 (its clock, panel, LEDs, screen and flash) for every other platform, with one API, `mm_host.h`. | GPL-3.0-only |
| Frontends | `platforms/web`, `platforms/sdl`, `platforms/common` | A window, sound, input and files for each platform; the panel drawn beside the screen, the keyboard and gamepad map and the flash file they share. | GPL-3.0-only |

## One translation unit

Each build compiles one C file that `#include`s the others in a fixed order. For the FM-1 this is
`firmware/src/midimania.c`. For the host it is `platforms/host/mm_host.c`, which a frontend includes. The order
matters: a file uses what the files before it define. Building this way lets the compiler see the whole program
(small and fast code on a 360 MHz DSP with 1 MiB of flash), and it keeps the HAL header-only.

The sound side (libc, engines, voices, FX, MIDI parsing, sequencer) is listed once for the host, in
`platforms/host/firmware.c`. The host tests (`tests/`) and the host build both use that list.

## Platforms

A platform is either a port of the HAL or a host frontend:

- **A HAL port** (the FM-1 today; other boards in felucca-bsp) gives the `fm1_*` functions that the application
  calls: time, the audio DMA and its interrupt, the key and encoder scan, the LEDs, the LCD, the flash, the ADC. It
  builds `midimania.c`. The FM-1's set is in `firmware/bsp/chip` and `firmware/bsp/boards/fm1`.
- **A host frontend** builds `platforms/host/mm_host.c`, which already provides the HAL as a simulation, and talks
  to it through `mm_host.h`: boot, render audio, send the controls and MIDI, read the screen and the LEDs, keep
  the flash. This is the easy way to bring Felucca to a computer, a handheld or an app.

[PORTING.md](PORTING.md) explains both.

The application knows nothing about the platform except through a few build flags:

| Flag | Set by | Meaning |
| --- | --- | --- |
| `MM_HOST` | `platforms/host/mm_host.c` | A host build: no hardware crash counter. |
| `MM_FLASH`, `MM_OTA`, `MM_CDC`, `MM_UAC`, `MM_UART`, … | `midimania.c`, `tools/build.py` | Optional parts of the FM-1 build ([BUILDING.md](../BUILDING.md)). |

## Engines

An engine is an `engine_t` (`felucca_engine.h`): its name, eight parameters with ranges and names, presets, the four
HOME knobs, and the functions the host calls per voice (`note_on`, `render`) and per block (`block`, `amp`, `post`).
The UI draws every engine from this description, so an engine needs no UI code of its own.

`firmware/src/engines.c` holds the table of engines, `ENGINES[]`. An engine's number is its index there. Saved sounds
store the number, so a number never moves.

## Storage

The FM-1's 1 MiB flash holds the firmware and the store (settings, user presets, four projects, user samples) in sectors written by `storage.c` with A/B copies. On a host the same bytes live in a file
(`platforms/common/flashfile.c`) or in the browser. The formats and their compatibility rules are in
[COMPATIBILITY.md](COMPATIBILITY.md).

## Tests

`tests/run_tests.sh` runs everything that can run on a computer: the engines' golden renders, every format's
loading, the game's UI, MIDI, USB descriptors, the update path, fuzzers under ASan and
UBSan, the browser and desktop builds. A sound change fails the golden hashes until they are
updated on purpose. Releases are to be tested on the FM-1 itself.
