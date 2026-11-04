<img src="assets/logo.svg" width="96" height="96" alt="The FM-1 Midi-Mania logo: four keys, the middle one pink">

# FM-1 Midi-Mania

**FM-1 Midi-Mania** (Midi-Mania for short) is a rhythm-game firmware for the **M-VAVE FM-1** synthesizer. Turn a MIDI song into a chart and load it onto the FM-1.
The FM-1 plays the backing on its own synth engines while notes fall towards the keys on its screen, and you play
your part on its real keys. (Scoring, every press judged PERFECT, GREAT, GOOD or MISS, is still to come.)

This is based on **[Felucca](https://github.com/hugelton/Felucca)** by Leo Kuroshita (@kurogedelic),
Hügelton Instruments. It keeps Felucca's sound engines, audio, display, storage and USB, and replaces its groovebox side (step sequencer, arpeggiator, pattern editing) with the game.

## Try it

- **[FM-1 Midi-Mania Studio](https://shan-sherif.github.io/fm-1-midi-mania/webapp/studio/)**: the FM-1 in your browser,
  the same code the instrument runs. Play the built-in songs or FREE PLAY, and drop your own MIDI files on the page
  to load them as songs. No FM-1 needed; Chrome, Edge, Firefox or Safari. **Have an FM-1?** Connect it by USB on the
  same page (Chrome or Edge; Midi-Mania 0.2.0 or later) to send your songs to it, and to list and delete the ones on
  it. Up to 16 songs, 240 KB in all.
- **[The installer](https://shan-sherif.github.io/fm-1-midi-mania/webapp/installer/)**: puts Midi-Mania on your FM-1
  over USB, from Chrome or Edge. **Return to official V15** on the same page puts the stock firmware back.
- **[The site](https://shan-sherif.github.io/fm-1-midi-mania/)**: all of them, from one page.
- **[Releases](https://github.com/shan-sherif/fm-1-midi-mania/releases)**: each version's package and notes.

> **Status: early.** Working on the FM-1 and in the desktop and browser builds:
> - **SONGS** (the SEQ button): the built-in songs, then your own (sent from Studio over USB, or loaded in Studio itself);
> - the **play page** (HOME): your part's notes falling to the hit line, the keys lit as they sound; **PLAY YOU**
>   leaves your part to you (a key on time plays the song's note);
> - the chart format (`firmware/src/chart.h`) and the timing judge (`firmware/src/judge.c`, with its test).
>
> Not yet: judging your presses on screen (scores).

## Playing a MIDI file

1. The easy way: drop it on **[FM-1 Midi-Mania Studio](https://shan-sherif.github.io/fm-1-midi-mania/webapp/studio/)**
   (Your songs) to try it in the browser, then connect your FM-1 there and send it on. It is converted in the
   browser as below.
   To build songs into the firmware instead, put `.mid` files in `midi/`. Files there are git-ignored: songs are
   usually copyrighted, so they stay on your computer. Two songs are always built in, to try straight
   away on the FM-1 and in the browser ([assets/songs/](assets/songs/README.md) records their sources):
   - **Bach's Prelude in C (BWV 846)** for KEYS, from the
     [Mutopia Project](https://www.mutopiaproject.org/cgibin/piece-info.cgi?id=5) (public domain);
   - **FM Beats** for DRUMS, an original groove written for this project, using all eight drum lanes.
2. Build: `./build.sh sdl` or `./build.sh web`. `tools/gen_songs.py` converts every file in `assets/songs/` and
   `midi/` into `build/gen/midimania_songs.h`, so the songs are built into the firmware.
3. On the FM-1 (or the desktop or browser build), the panel:

| Button | Opens |
| --- | --- |
| **HOME** | the **game** (the play page): your part's notes falling towards the hit line, the key LEDs lit as they sound (ARP too). Held: MENU |
| **SEQ** | **SONGS**, the song list (also at power-on); SEQ again goes back to the game |
| **FX** | **GAME**, the game's settings |
| **EDIT** | the **MIXER** |
| **SCL** | **METRONOME** |
| **SAVE** | **PRESETS**: the selected part's sounds |
| **PLAY** | plays the selected song (and opens the game), or stops the one playing |
| ENV, LFO, GLO, REC | nothing |

   **ALGORITHM** picks your part from anywhere: **1 KEYS** (piano) or **2 DRUMS** (those the song has; GUITAR and BASS are
   played by the FM-1 and become playable in a later release). The keys play it; the header shows it at the right
   ("1 KEYS"). **PRESETS** picks its sound, shown at the bottom left.

   In **SONGS**:
   - **SELECT** picks a song. The first row is **FREE PLAY** (selected at power-on): no song, your part on its sound;
     on the play page every key you press (on the panel, or a MIDI note on your part's channel: 1 KEYS, 2 DRUMS)
     appears at the hit line and moves away from it (the opposite way to a
     song's notes), and the header says FREE PLAY. PLAY stops it;
   - **PRESETS** picks your part's **SOUND** (its list: The four parts, below);
   - **ALGORITHM** picks your part (the PART row);
   - **KNOB 2** sets the tempo, 1 BPM a detent, from half to twice the song's own BPM (shown beside it);
     **OCT-** puts it back;
   - **KNOB 3 PLAY:** **AUTO**, the FM-1 plays your part, or **YOU**, your part is silent and you play it on the keys
     (its keys still light as the notes arrive). A note you don't press stays silent; a key pressed within 110 ms of
     one of your notes on it plays the song's note, otherwise the key's own sound;
   - **KNOB 4 RANGE:** which of your part's notes are yours (the FM-1 plays the rest): for KEYS, GUITAR and BASS,
     **ALL**, **RIGHT HAND** (middle C and up), **LEFT HAND** (below it) or **NO LOW** (C3 and up); for DRUMS,
     **ALL 8**, **5 PADS** (no TOM, BELL, CLAP), **4 PADS** (KICK, SNR, CH, OH) or **2 PADS** (KICK, SNR);
   - **PLAY** starts it and opens the game.

   In the **game** (HOME), the header shows the song playing: the play icon, the metronome swinging on its beats
   (the accent colour on a bar's first), its tempo, and your part at the right. The panel shows your part's notes:
   - **KEYS, GUITAR, BASS:** lanes laid out like the FM-1's keyboard, sharps red and naturals blue; each note's start
     has a bright edge and its end a gap, so a note struck again reads as two;
   - **DRUMS:** eight lanes, left to right BELL (red), OH and CH (blue), SNR and RIM (yellow), KICK (purple),
     CLAP (red), TOM (green), the names in the same colours. The key under each lane lights: the black keys F#3 to
     A#4 in that order.
   - Columns none of your notes come down (outside your RANGE, or not in the song) are greyed out.
   - The keyboard strip (DRUMS: the pads) lights for the song's notes as they sound and for the keys you press, on
     the panel or by MIDI on your part's channel.

   Its four cards: **KNOB 1 SPEED**, **KNOB 2 OFFSET**, **KNOB 3 TEMPO** (live), **KNOB 4 SCROLL**. While a song
   plays they are hidden and the notes panel fills their space; turning any knob shows them for 3 s. TEMPO shows a
   play icon and flashes on every beat (the accent colour on a bar's first beat).
   On DRUMS the black keys F#3 to A#4 play the drum of the lane above them.
4. **GAME** (FX), the same settings on a page of their own:
   - **KNOB 1 SCROLL:** **DOWN**, notes fall to a line at the bottom of the panel, or **UP**, they rise to a line at
     the top (the keyboard strip and the drum pads move with it);
   - **KNOB 2 NOTE SPEED:** 1 (slow: 4 s from the top of the panel to the line) to 16 (fastest: 0.4 s); 4 is 2 s, 8 is 1 s. Kept for each part: DRUMS start at 8, the others at 4;
   - **KNOB 3 OFFSET:** −100 to +100 ms, your timing calibration for the judge to come; **OCT-** sets it to 0;
   - **HOME** or **FX** goes back to the game.
5. **METRONOME** (SCL), the same settings as MENU's, kept after power-off:
   - **KNOB 1 CLICK:** OFF / ON: the playing song's beats click, the first of each bar higher (it follows TEMPO);
   - **KNOB 2 LEVEL:** LOW / MID / HIGH;
   - **KNOB 3 COUNT-IN:** OFF / 1 BAR / 2 BARS of clicks before the song, at its first beat's tempo, ending a beat before
     its first beat (whatever CLICK is); the song waits for them if its start is shorter;
   - **HOME** or **SCL** goes back to the game.

### The four parts
Each part is one instrument with its own sounds (`firmware/src/engines.c` `MM_SOUNDS`):

| Part | Sounds (engine) |
| --- | --- |
| **1 KEYS** | PIANO, FLUTE (SAMPLE); TINE EP, ORGAN (FM6); HARP, KALIMBA, MARIMBA (PHYS) |
| **2 DRUMS** | the DRUM engine's kits |
| **3 GUITAR** | PLUCK, HARP, KALIMBA, DRONE STRING (PHYS) |
| **4 BASS** | FM BASS, PLUCK (FM6); SUB BASS, SQR BASS (ANALOG) |

Pick your part's sound in SONGS with **PRESETS** (the SOUND row); SAVE > PRESETS picks any part's. Nothing (a preset,
a MIDI program change) can put a sound from outside the list on a part. A song plays on the sounds you picked. MIDI
IN channels 1–4 play parts 1–4.

### What Midi-Mania leaves out of Felucca
- **The sequencer:** no STEP, DETAIL, AUTOMATION, SEQ TOOLS, recording, song chain, PHRASES, PROJECT or TOOLS, and
  no ENV, LFO, MOD or EDIT pages. The panel as above.
- **Engines:** five, **DRUM, SAMPLE, PHYS, FM6, ANALOG**, each part fixed to its own sounds. Felucca's PHASE, LOFI,
  VOICE, TRIO, WHEEL, GRAIN, NOISE, SLICE, ADPCM and DIGITAL engines and its plugin engines are gone (a stored sound
  of one plays a sine; the engine numbers stay as Felucca's, so its stored data still reads). No user presets, no
  FM6 operator pages. The FLUTE and SAX samples' numbers play PIANO.
- **Effects:** a light reverb (ROOM) only. No delay, chorus, distortion, insert effects, SLICER or perform FX.
- **Keys:** they play their own notes: no layers, SCALE, chord keys or arpeggiator.
- **Platforms:** the libretro core.

Kept: the MIXER, PRESETS, naming, MIDI learn, MIDI IN, the metronome (now on the song's beats), MENU, backups and
updates. The CLOCK page (BPM, SWING, external clock) is gone: it drove the sequencer.

The removed engines, plugins, platforms and their tests are deleted from the tree. Some of the sequencer's, the
layers' and the hidden pages' code is still compiled though never reached: it is woven into code that stays (the
keyboard, MIDI IN, the stores), so it comes out one module at a time.

How a file is fitted to the FM-1 (`tools/gen_songs.py`): four modes, one per part, each on a fixed sound.

| Mode | Part | MIDI instruments | Instrument |
| --- | --- | --- | --- |
| KEYS | 1 | General MIDI programs 0–23 (pianos, chromatic percussion, organs) | SAMPLE PIANO |
| DRUMS | 2 | channel 10 | DRUM kit |
| GUITAR | 3 | programs 24–31 | PHYS PLUCK |
| BASS | 4 | programs 32–39 | FM6 FM BASS |

- A flute (General MIDI's flute family, programs 72–79: piccolo, flute, recorder, pan flute ... ocarina) is
  compared with the piano parts: whichever has more notes plays KEYS, and the other is dropped.
- Every other instrument is dropped. If nothing is KEYS, the busiest dropped instrument plays KEYS, so a melody on
  a harp, strings or a lead still has a part.
- Notes doubled within a mode play once.
- At most 8 notes sound at once. A note of a mode that matters more takes the voice of a held note of one that
  matters less (DRUMS, then KEYS, GUITAR, BASS: bass gives way first); otherwise it is dropped.
- A song starts at least 2 seconds before its first note: longer silence is cut; a song that starts at once (Amen
  Break) gets whole bars of silence added, so its notes fall in from the top and the beats (the metronome,
  COUNT-IN) stay on the bar.
- A song plays on its parts' instruments, with the preset you picked for each (SAVE > PRESETS).

## How it works

1. **On a computer**, `tools/gen_songs.py` converts a Standard MIDI File into a chart:
   - the notes you play, each placed on one of the FM-1's 27 keys;
   - the backing parts, fitted to the FM-1's 4 synth parts and 8 voices;
   - the beat times.
2. **The chart goes to the FM-1:** sent over USB from Studio and stored in its flash
   (`firmware/src/editor_songs.c`, [docs/CHART_FORMAT.md](docs/CHART_FORMAT.md)), or built into the firmware
   (`assets/songs/`, `midi/`).
3. **On the FM-1**, the audio interrupt plays the backing with sample-accurate timing. It sounds your keys and passes
   each press, timed by the key scan to about 0.1 ms, to the judge.

More: [docs/GAME_ARCHITECTURE.md](docs/GAME_ARCHITECTURE.md) · [docs/GAME_DESIGN.md](docs/GAME_DESIGN.md) ·
[docs/CHART_FORMAT.md](docs/CHART_FORMAT.md) · and from Felucca: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/PORTING.md](docs/PORTING.md)

## Layout

```
firmware/src/       the application (Felucca's, now ours) and the game: midimania.c (the FM-1's top), player.c,
                    ui_songs.c, chart.h, judge.c
firmware/hal/       hardware headers
firmware/bsp/       the FM-1 hardware layer (from felucca-bsp, MPL-2.0)
firmware/engines/   ANALOG and the shared DSP (from Felucca-Engines, MIT)
firmware/loader/    the update loader
platforms/          host builds on a simulated FM-1: desktop (SDL2) and browser (platforms/host/mm_host.c)
tools/              build, generators, installer, MIDI → chart converter (gen_songs.py)
tests/              tests that run on a computer (tests/run_tests.sh)
web/                the installer; the MIDI converter (mm_songs.js) and the FM-1 song upload (fm1songs.js) Studio uses
assets/             fonts, icons, samples
docs/               documentation
```

The repository stands alone: no submodules. A normal `git clone` has everything.

## Building

- **The FM-1 firmware** needs Linux: JieLi's compiler runs only on Linux x86-64. On Windows, use **WSL 2** (or
  Docker).
- **The desktop and browser builds** run the same firmware on a simulated FM-1.
- With WSL you still test everything from Windows:
  - the desktop build opens a window on Windows (WSLg);
  - the browser build, **FM-1 Midi-Mania Studio**, opens in Chrome or Edge;
  - the FM-1 is installed from the browser on Windows.

The installer page (`web/`) only installs; it doesn't build anything.
See [BUILDING.md](BUILDING.md), and [docs/BUILDING_WINDOWS.md](docs/BUILDING_WINDOWS.md) for Windows.

## Renamed from Felucca
- The name: **FM-1 Midi-Mania** (the power-on splash with its logo, MENU > ABOUT, the crash screen, the web pages'
  titles and icons, the desktop build's window, this README), Midi-Mania for short in running text; the repository
  `fm-1-midi-mania`. The logo: [assets/logo.svg](assets/logo.svg) (the artwork: `assets/logo.png`; on the FM-1 it is
  drawn, `firmware/src/ui_draw.c` `draw_logo`).
- The device: the USB product "FM-1 Midi-Mania" (manufacturer "Shan-Sherif"; the update loader "FM-1 Midi-Mania
  Update"), the version string "FM-1 MIDI-MANIA vX.Y" (the installer and Studio check it), the serial console's
  banner, the desktop build's MIDI port "FM-1 Midi-Mania". Before 0.2.0 they were "Midi-Mania", "Midi-Mania Update"
  and "MIDI-MANIA vX.Y"; the tools and pages find the FM-1 by "midi-mania" in its name, so both.
- The package identity `FM-1_8XY` (`tools/build.py`; Felucca's are `FM-1_9XY`, the stock firmware's `FM-1_015`) and
  the loader mark `MIDIMANIA-LOADER-1`. The way back to stock (the installer's **Return to official V15**) is
  unchanged.
- Files and names: `firmware/src/midimania.c`, `platforms/host/mm_host.*`, `platforms/sdl/mm_sdl.c`, the build flags
  `MM_*` (were `FELUCCA_*`), the build's outputs `build/midi-mania.*` (`.bin`, `.fwsc`, `.wasm`), the browser
  build's storage keys.
- Kept as Felucca's: the copyright notices, the credits and "based on Felucca" lines; the vendored libraries' own
  names (Felucca-Engines' `felucca_engine.h`, felucca-bsp); the editor's sound file formats (`felucca-patch`,
  `felucca-library`), so sounds saved with Felucca still load. The backup file is `"midi-mania-backup"` since 0.2.0
  (with the stored songs); `"felucca-backup"` files still restore.

## Installing on an FM-1

Use [the installer](https://shan-sherif.github.io/fm-1-midi-mania/webapp/installer/) in Chrome or Edge, at your own
risk. Its **Return to official V15** (with the official `FM-1.fwsc` from M-VAVE) takes the FM-1 back to stock. If an install fails and the FM-1 does not start, recovery needs
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

Before **Return to official V15**, the installer saves a backup file to your computer (`Midi-Mania-backup-DATE.json`):
your settings, sounds and uploaded songs. After installing Midi-Mania again, **Restore a backup** on the same page puts
it back. Songs already on the FM-1 stay, and the backup's other songs are added. The file is `"midi-mania-backup"`;
older ones, saved as `"felucca-backup"` (Felucca's name for the same format) before 0.2.0, still restore, without songs.

## Based on Felucca

Copied on 2026-11-02

| Here | From | Commit |
| --- | --- | --- |
| everything else | [hugelton/Felucca](https://github.com/hugelton/Felucca) | `40762f2` |
| `firmware/bsp/` | [hugelton/felucca-bsp](https://github.com/hugelton/felucca-bsp) | `5942e68` |
| `firmware/engines/` | [hugelton/Felucca-Engines](https://github.com/hugelton/Felucca-Engines) | `0f015ad` |

## Licence and credits

GPL-3.0-only ([LICENSE](LICENSE)), as Felucca. The engines (`firmware/engines/`) are MIT and the hardware layer
(`firmware/bsp/`) is MPL-2.0. Bundled fonts, samples and ported DSP keep their own licences ([LICENSING.md](LICENSING.md),
[LICENSES/](LICENSES/)). Files carried over from Felucca keep its copyright notice. Thanks to Felucca's author and
contributors: [CREDITS.md](CREDITS.md).

M-VAVE and FM-1 are trademarks of their respective owners. This project is not affiliated with or endorsed by them.
