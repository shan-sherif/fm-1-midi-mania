# Game design

An original rhythm game for a keyboard instrument. The FM-1 plays a song; notes fall towards the keys on the screen.
You play them on the real keys and hear your own part on the FM-1's synth engines. Your timing is judged.

Everything here is a first draft to be tried on the device and changed.

## The hardware we design for

- **27 keys**, F3 to G5 (two octaves and a bit), with pressure on most of them (Felucca's AFTERTOUCH).
- **14 buttons**: FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+. The game gives them new jobs; the
  printed labels stay.
- **7 encoders**: SELECT, ALGORITHM, PRESETS, KNOB 1–4.
- **A 240 × 240 colour screen.**
- **8 voices** in total, shared by **4 synth parts**.

## Modes

| Mode | What happens |
| --- | --- |
| **Play** | The song runs; every playable note is judged. Score, combo, result screen. |
| **Practice** | The song waits at each note until you press the right key. Optional slower speed. Not scored. |
| **Listen** | The device plays your part too (autoplay), so you can learn the song. |
| **Free** | Just the instrument: the keys play the song's sound. |

## Judging

| Grade | Window (either side of the note) | Points |
| --- | --- | --- |
| PERFECT | ±30 ms | 3 |
| GREAT | ±60 ms | 2 |
| GOOD | ±110 ms | 1 |
| MISS | outside, or not pressed | 0, combo ends |

- A press with no note near it is ignored for now (to do: decide if it should count against you).
- **EARLY / LATE** shown with each grade, from the press-minus-note time.
- **Calibration**: a short tap-along screen measures the player's average offset and stores it in the settings.
- Long notes (held for `len`) are judged on the press only for now. Judging the release is to do.

## The play screen (first idea)

27 lanes across 240 pixels is about 9 pixels each: narrow, but the FM-1's keys are small too. Lanes follow the
keyboard: black-key lanes are darker and shorter. Notes fall from the top to a judgement line just above a drawn
keyboard strip. The grade, combo and score sit in the top bar.

Other layout to try: a **zoomed view** that shows one octave and a half around where the song is, sliding as the
melody moves.

## Charts and difficulty

The converter makes the levels from one MIDI file:
- **easy**: the melody only, thinned to the beats;
- **normal**: the full melody;
- **hard**: the melody and the bass, or chords.

## Open questions

- How far ahead should notes appear (scroll speed), and should the player be able to set it?
- Should the encoders do anything during play (volume, scroll speed)?
- How does a song get onto the device: USB from a computer (SysEx, like Felucca's sample upload), or built in?
- Which songs can ship with it? Only ones we have the rights to: our own, public domain compositions with our own
  arrangement, or CC-licensed MIDI files.
