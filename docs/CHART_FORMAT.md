# Chart format (MMC1)

A chart is one song ready to play. `tools/gen_songs.py` makes the built-in songs from MIDI files; charts stored in
the FM-1's flash (loaded from a MIDI file on a computer) use the bytes below. The firmware's view
of the same bytes is [`firmware/src/chart.h`](../firmware/src/chart.h); the two must agree.

All numbers are little-endian. Times are **samples at 44,100 Hz** from the start of the song: the FM-1's audio clock,
so the device never converts ticks or tempos while playing.

```
chart_head_t      72 bytes (head_size)
uint32_t beat[nbeat]
chart_note_t note[nnote]     12 bytes each, sorted by time
```

## Header (72 bytes, version 2)

| Offset | Size | Field | |
| --- | --- | --- | --- |
| 0 | 4 | `magic` | `"MMC1"` (0x31434D4D) |
| 4 | 2 | `version` | 2 |
| 6 | 2 | `head_size` | 72; a later, longer header still loads in older firmware |
| 8 | 4 | `nbeat` | number of beat times |
| 12 | 4 | `nnote` | number of notes |
| 16 | 4 | `length` | the song's end, samples |
| 20 | 2 | `bpm_x10` | the first tempo × 10, for display |
| 22 | 1 | `beats_per_bar` | for bar lines |
| 23 | 1 | `level` | difficulty 1..10 |
| 24 | 24 | `title` | ASCII, zero-padded |
| 48 | 16 | `part[4]` | per synth part: `engine`, `preset`, `level`, reserved |
| 64 | 4 | `crc32` | CRC-32 (zlib's) of everything after the header |
| 68 | 2 | `beat0` | the first beat's place in its bar: beat `k` starts a bar when `(beat0 + k) % beats_per_bar` is 0 |
| 70 | 1 | `parts` | bit `p`: part `p` has notes |
| 71 | 1 | `live` | 0xFF: in use; 0x00: deleted (in flash: programmed over without an erase) |

## Beats

The time of every beat, in order. The tempo map lives here: a tempo change only moves the beats. The game draws bar
lines from them and the metronome can click on them.

## Notes (12 bytes each)

| Offset | Size | Field | |
| --- | --- | --- | --- |
| 0 | 4 | `time` | note-on, samples |
| 4 | 4 | `len` | samples to its note-off; 0 for a hit with no length |
| 8 | 1 | `key` | the FM-1 key the player presses, 0 (F3) .. 26 (G5); playable notes only |
| 9 | 1 | `pitch` | the MIDI note that sounds (can be outside the keys' range) |
| 10 | 1 | `vel` | velocity 1..127 |
| 11 | 1 | `flags` | bits 0–1: part (0..3); bit 2 `CN_PLAY`: the player's note; bit 3 `CN_DRUM`: `pitch` is a GM drum note |

`key` and `pitch` are separate on purpose. The converter can fold a melody into the 27 keys (or lay a drum part
across them) while the song still sounds as written.

## Rules for the converter

- Notes sorted by `time`.
- No two playable notes on the same key closer together than the judge's GOOD window (110 ms by default): one press
  could not tell them apart.
- Melodic notes inside their part's range (MIDI notes): KEYS 36–96 (C2–C7, 61 keys), GUITAR 40–88 (E2–E6), BASS
  28–67 (E1–G4). The others are dropped before the voice budget.
- At most 8 notes sounding at once across all parts (Felucca's voice budget).
- Drums (MIDI channel 10) on one part set to the DRUM engine.

## In the FM-1's flash

Stored songs live in the song region, flash `0xA0000..0xDBFFF` (240 KiB, 60 sectors of 4 KiB; Felucca's user sample
slots). Each chart starts on a sector boundary. At power-on the firmware (`firmware/src/player.c` `mm_song_scan`)
looks at every sector:

- a chart there whose CRC holds and whose `live` is 0xFF is listed after the built-in songs (16 at most) and played in
  place; its sectors are in use, and the scan goes on after its last one;
- anything else (erased, a deleted chart, a broken upload) is free, and the scan goes on at the next sector.

**Writing a song:** it goes into the first run of free sectors it fits (`mm_song_room`). Its first sector is erased,
then the rest of the chart from byte 256 on is written, each further sector erased as the writes reach it. The first
256 bytes, with the header, go last, once the CRC over the whole chart holds. A cut before then leaves no `MMC1` at
the sector's start, so nothing is listed and the sectors stay free.

**Deleting a song:** its sectors are erased, the header's first. The other songs never move, so a power cut loses at
most the song being written or deleted. (`live` = 0 still marks a chart deleted, but the firmware no longer writes it.)

Over USB: the editor protocol's song commands (`firmware/src/editor_songs.c`, `web/EDITOR_PROTOCOL.md` "Songs"), used
by Studio to send its songs on to a real FM-1 (`platforms/web/index.html` "On your FM-1", `web/fm1songs.js`). The browser build stores charts in its
simulated flash the same way (`fh_song_room`, `fh_song_used`, `fh_songs_rescan` in `platforms/host/mm_host.h`;
`platforms/web/worklet.js` `songsAdd`, `songsRemove`).
