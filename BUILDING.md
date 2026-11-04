# Building Midi-Mania

Midi-Mania builds for the FM-1 and for the host platforms (the browser, a desktop with SDL2). The host platforms
need only a C compiler and Python; the FM-1 also needs JieLi's toolchain and SDK. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
explains how the pieces fit.

| Command | What | Needs |
| --- | --- | --- |
| `./build.sh` | the FM-1 firmware: `build/midi-mania.fwsc` (and `build/midi-mania.bin`, `build/loader/ota.bin`) | everything below |
| `./build.sh --release X.Y.Z` | a release build of it (below) | the same |
| `./build.sh gen` | the generated headers only: `build/gen` (tables, fonts, icons, palettes, samples) | Python 3, Pillow, fontTools |
| `./build.sh web` | the browser build, Midi-Mania Studio: `build/web` | `gen`, Emscripten |
| `./build.sh sdl` | the desktop build: `build/sdl/midi-mania` | `gen`, a C compiler, SDL2 |
| `./build.sh host` | `gen`, then every host platform whose tools are there | |
| `./build.sh test` | the tests (`tests/run_tests.sh`) | see [Tests](#tests) |

The FM-1 build makes three files in `build/`:

| File | What |
| --- | --- |
| `midi-mania.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `midi-mania.fwsc` | the installable package (app + loader) |

## Get the source

Everything is in this repository, with no submodules. Six engines (`firmware/engines/`, from
[Felucca-Engines](https://github.com/hugelton/Felucca-Engines), MIT) and the hardware layer (`firmware/bsp/`, from
[felucca-bsp](https://github.com/hugelton/felucca-bsp), MPL-2.0) are plain folders, copied from upstream (see
[README.md, Based on Felucca](README.md#based-on-felucca)). A normal clone has everything:

```
git clone <this repository's URL>
```

On Windows, read [docs/BUILDING_WINDOWS.md](docs/BUILDING_WINDOWS.md) first.

## Prerequisites (macOS)

For every build:

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time)

For the FM-1:

- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

On Linux x86-64 the toolchain runs natively and Docker is not needed.

For the host platforms and the tests (each optional):

- a C compiler (clang or gcc);
- SDL2 for the desktop build (`brew install sdl2`; Debian / Ubuntu: `libsdl2-dev`, and `libasound2-dev` for MIDI);
- Emscripten (`emcc`) for the browser build;
- Node.js for the web tests.

## Build the FM-1 firmware

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.1.0` makes a release build: the package identity becomes `FM-1_801`
and the version string `v0.1.0`; the package is `build/midi-mania-0.1.0.fwsc`, and
`build/release-0.1.0/` holds what a release ships: the package, the app
(`midi-mania-0.1.0-app.bin`), `SHA256SUMS`, the sample attribution, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it). The identity changes with
the first two numbers only: 0.1.0 and 0.1.1 are both `FM-1_801` (Felucca's are `FM-1_9XY`).

The build checks the image before it writes the package: the entry, the code in RAM, the sizes against the memory map
(`firmware/app.ld`), and that no file outside `firmware/hal` (and felucca-bsp) touches a hardware register.

Build options (environment, `0` or `1`; defaults in `firmware/src/midimania.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `MM_FLASH` | 1 | settings, presets and projects in flash |
| `MM_OTA` | 1 | update entry (needs `MM_FLASH`) |
| `MM_CDC` | 1 | USB serial console |
| `MM_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `MM_UAC` | 1 | USB audio input (the master output, stereo, 44.1 or 48 kHz) |
| `MM_UAC_48K` | 1 | `0`: the USB audio input at 44.1 kHz only, as up to 1.0.5.2 (`firmware/bsp/chip/usb.c`) |
| `MM_UAC_AS_FIRST` | 1 | the audio control header lists the audio streaming interface before the MIDI one, so the USB audio driver of older macOS (up to 15) finds it; `0`: 1.0 to 1.5's order, byte for byte (`firmware/bsp/chip/usb.c`) |
| `MM_CPU2` | 1 | the second core is built in (MENU > SYSTEM > 2ND CORE, OFF by default); `0`: none of it, 1.6's one-core code and RAM (`firmware/bsp/chip/fm1_cpu1.h`, `firmware/src/fx.c`) |
| `MM_UART` | 1 | TRS MIDI IN |
| `MM_ICONS` | 1 | parameter icons on the knob cards |

`MM_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/bsp/chip/usb.c`.

`LCD_BAUD` (`1` to `4`, default `2`) sets the screen's SPI clock, 60 MHz / (`LCD_BAUD` + 1): `2` is 20 MHz (the
default since 0.2.0, tried on an FM-1), `4` 12 MHz (Felucca's), `1` 30 MHz (not tried). If a build shows a garbled
picture, `LCD_BAUD=4` puts Felucca's clock back (`firmware/src/lcd.c`).

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works, with no
instrument sets in the SAMPLE engine: they play a plain sine
at the note's pitch instead, and the screen says NO SAMPLE.

## Host platforms

Each host platform builds the firmware's sources against a simulated FM-1 (`platforms/host`), so they need only
`build/gen`, not the FM-1's toolchain:

```
./build.sh gen
./build.sh web                                  # build/web: open with a local server
python3 -m http.server -d build/web 8790        # http://localhost:8790/
./build.sh sdl && build/sdl/midi-mania             # the desktop build
```

- The browser build is WebAssembly in an AudioWorklet. It keeps the flash's storage sectors (settings, user samples)
  in the browser.
- [platforms/sdl](platforms/sdl/README.md) describes the desktop build: the keys, a gamepad, MIDI in, the flash file.
- Not on any host platform: USB (MIDI out, USB audio, the web editor, updates), TRS MIDI and the serial console.

[platforms/README.md](platforms/README.md) lists the platforms, and [docs/PORTING.md](docs/PORTING.md) explains how to add one.

## Tests

```
tests/run_tests.sh
```

Runs everything that runs on a computer. Run it after `./build.sh` (it uses `build/` and needs `AC79_SDK` set as for the
build); with only `./build.sh gen` it runs the tests that need no FM-1 package and skips the rest. They cover
Midi-Mania's own parts (the judge, the song player, the game's UI) and the code it keeps from Felucca: the engines
(DRUM, PHYS, FM6, SAMPLE's PIANO, the conversion of old DIGITAL sounds), the modulation matrix, golden renders and CPU
budgets (on one core and two), flash storage, settings, backups, the FM6 voice bank, the keys and knobs, MIDI (USB,
TRS), USB audio and its descriptors, the editor protocol, crafted stored data and short fuzz runs under ASan/UBSan, the
update entry and loader, the command-line installer and the web pages. With `DAISYSP` pointing at a DaisySP checkout,
the PHYS models are also compared with their floating-point originals; without it that test is skipped.

The platforms are tested too: the browser build (with Emscripten and Node.js: it boots, plays, saves a setting, and
renders the same samples as the native build) and the desktop build (headless: it plays and saves a setting).

## Install

Use the web installer in Chrome or Edge:
<https://hugelton.github.io/Felucca/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/midi-mania.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/midi-mania.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).
