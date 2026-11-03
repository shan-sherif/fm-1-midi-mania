# Tools

Computer-side tools. The build, generators and installer here came from Felucca (see their own headers).

| Tool | Status | What |
| --- | --- | --- |
| `wsl_test_copy.sh` | works | The full test suite in a disposable copy on WSL's Linux disk (`~/fm-1-midi-mania-test`), much faster than from the Windows folder. Run it in WSL from the repository |
| `gen_songs.py` | works | Every `.mid` in `midi/` → `build/gen/midimania_songs.h`: the songs built into the firmware (chart notes, [docs/CHART_FORMAT.md](../docs/CHART_FORMAT.md)). The build runs it |
| `chart_upload.py` | planned | Send a chart to the FM-1 over USB (after Felucca's `fm1_sample_upload.py`) |

## mid2chart.py (plan)

Python 3 with `mido`. Steps:

1. Read the file and its tempo map. Turn every tick into a sample time at 44,100 Hz and write the beat times.
2. Pick the player's track (by name or number, or the busiest melodic one). Its notes become `CN_PLAY`.
3. Fold the player's notes into the 27 keys, F3..G5 (`key`), keeping `pitch` as written. Drop or merge notes on
   the same key closer together than the GOOD window.
4. Map the other tracks onto 4 parts: channel 10 → a DRUM part; the rest by role (bass, chords, lead), with an
   engine and preset each. Thin the backing so no more than 8 notes sound at once.
5. Difficulty levels: thin the player's notes for easy, add the bass or chords for hard.
6. Write the header, the beats and the notes sorted by time, with the CRC.
