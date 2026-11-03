# Platforms

> These are Felucca's host platforms as copied (see [README.md](../README.md#based-on-felucca)). They let us play the game
> without an FM-1. The "try it" link and the statuses below are Felucca's, not this firmware's. When the game
> replaces Felucca files, the host's own list of the firmware's sound files, `host/firmware.c`, has to follow
> (`play.c` in place of `seq.c`, and so on: [docs/GAME_ARCHITECTURE.md](../docs/GAME_ARCHITECTURE.md)).

Felucca's sources on platforms other than the FM-1. The FM-1's own build is in `firmware/` (see
[BUILDING.md](../BUILDING.md)). How the pieces fit is in [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md), and how
to bring Midi-Mania to a new platform is in [docs/PORTING.md](../docs/PORTING.md).

| Folder | What | Status |
| --- | --- | --- |
| [`host/`](host/) | The host: the firmware against a simulated FM-1, and its API (`mm_host.h`). Every platform below builds it. | — |
| [`common/`](common/) | What the frontends share: the panel drawn beside the screen, the keyboard and gamepad map, the flash file, MIDI in. | — |
| [`web/`](web/) | The browser: WebAssembly in an AudioWorklet, and its page. | Supported |
| [`sdl/`](sdl/README.md) | A desktop with SDL2 (macOS, Linux). | Beta, source only |

Every platform needs the generated headers in `build/gen` (the tables, fonts and samples). Make them without the
FM-1's toolchain:

```
./build.sh gen
```

Then build a platform with its script, or with `./build.sh web` or `./build.sh sdl`.

## What each platform has

| | FM-1 | Browser | SDL2 |
| --- | --- | --- | --- |
| Sound, screen, panel | yes | yes | yes |
| MIDI in | USB, TRS | Web MIDI | ALSA, CoreMIDI |
| MIDI out, the web editor, updates | yes | — | — |
| Where the flash is kept | its own flash | the browser | a file |
