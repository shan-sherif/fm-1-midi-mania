#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 fm-1-midi-mania contributors
"""The songs built into the firmware: every Standard MIDI File in assets/songs/ (the starter songs, committed: their
sources and licences in assets/songs/README.md), then in midi/ (yours, git-ignored) -> build/gen/midimania_songs.h.

    tools/gen_songs.py OUT.h [MIDI_DIR] [--charts DIR]

--charts DIR also writes each song as a chart file, DIR/<title>.mmc: the bytes a stored song has in the FM-1's
flash (docs/CHART_FORMAT.md, firmware/src/chart.h; the browser build's page writes the same).

Each song becomes a sorted array of chart notes (firmware/src/chart.h chart_note_t: times in samples at 44.1 kHz)
and an mm_song_t (firmware/src/player.c) with its title, length and the sound of each part. Python's standard
library only. tools/build.py --gen runs it, and build.sh runs it again before every host build.

Fitting a MIDI file to the FM-1: four modes, one per part, each on a fixed sound (ROLES below):
  part 1 KEYS    General MIDI programs 0..23 (pianos, chromatic percussion, organs)     SAMPLE PIANO
  part 2 DRUMS   channel 10                                                              DRUM (its kit)
  part 3 GUITAR  programs 24..31                                                         PHYS PLUCK
  part 4 BASS    programs 32..39                                                         FM6 FM BASS
  - a flute (General MIDI's pipe family, programs 72..79) with more notes than all the KEYS channels together
    plays KEYS in their place (they are dropped); with fewer, the flute is dropped;
  - every other instrument is dropped, but when no channel is KEYS the busiest dropped one plays KEYS (a melody on
    a harp, strings or a lead still has a part);
  - several channels of one mode play on its part together; a note doubled exactly there (the same start and
    pitch) is heard once;
  - at most NVOICE (8) notes sound at once, over all parts. Full: a note of a mode higher in PRIORITY (DRUMS, then
    KEYS, GUITAR, BASS) takes the voice of a held note of a lower one, which ends there; else it is dropped;
  - key: the note folded into the FM-1's 27 keys (F3..G5), the lane it falls in on HOME; the pitch sounds as
    written. A drum hit's lane is the firmware's (eng_drum.c drum_lane, player.c): its key here is 0.
"""
import re
import struct
import sys
from pathlib import Path

FS = 44100
NVOICE = 8
KEY_LO, KEY_HI = 53, 79                 # F3 .. G5
CN_PLAY, CN_DRUM = 4, 8
KEYS, DRUMS, GUITAR, BASS = 0, 1, 2, 3  # the modes = the parts 1..4 (firmware/src/player.c MM_KEYS ..)
ROLE_NAME = ("KEYS", "DRUMS", "GUITAR", "BASS")
# each mode's sound: (engine, preset). firmware/src/engines.c ENGINES[]: SAMPLE 4, PHYS 9, DRUM 10, FM6 12; the presets:
# build/gen/mm_samples.h (PIANO 0), firmware/src/eng_fm6.c (FM BASS 2), eng_phys.c (PLUCK 2), eng_drum.c (kit 0).
# The firmware fixes each part's engine (engines.c MM_PART_ENGINE); a song plays the preset the part has
ROLE_SOUND = ((4, 0), (10, 0), (9, 2), (12, 2))
# who keeps a voice when more than NVOICE would sound: DRUMS, then KEYS, GUITAR, BASS (bass gives way first). The
# default for every song (an upload page may one day let a user pick another order per song)
PRIORITY = {DRUMS: 0, KEYS: 1, GUITAR: 2, BASS: 3}
FLUTE_PROGRAMS = range(72, 80)          # General MIDI's pipe family: piccolo, flute, recorder, pan flute .. ocarina
MIN_LEN = FS // 50                      # 20 ms: a note with no length still gets a note-off
LEAD_IN = 2 * FS                        # a song starts 2 s before its first note (HOME: the notes fall from the top)
TITLE_MAX = 39


def vlq(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, i


def parse(path):
    """-> (division, tempos [(tick, us per quarter)], notes [(tick_on, tick_off, track, channel, pitch, vel)],
    programs {(track, channel): program}, beats a bar: the first time signature's numerator, 4 without one)"""
    d = path.read_bytes()
    if d[:4] != b"MThd":
        raise ValueError("not a Standard MIDI File")
    hl, _fmt, ntrk, div = struct.unpack(">IHHH", d[4:14])
    if div & 0x8000:
        raise ValueError("SMPTE time division is not supported")
    i, tempos, notes, progs, sigs = 8 + hl, [], [], {}, []
    for t in range(ntrk):
        if d[i:i + 4] != b"MTrk":
            raise ValueError(f"track {t}: no MTrk")
        ln = struct.unpack(">I", d[i + 4:i + 8])[0]
        j, end, tick, run, held = i + 8, i + 8 + ln, 0, 0, {}
        while j < end:
            dt, j = vlq(d, j)
            tick += dt
            st = d[j]
            if st == 0xFF:
                n, k = vlq(d, j + 2)
                if d[j + 1] == 0x51 and n == 3:
                    tempos.append((tick, int.from_bytes(d[k:k + 3], "big")))
                elif d[j + 1] == 0x58 and n >= 1:
                    sigs.append((tick, d[k]))
                j = k + n
                continue
            if st in (0xF0, 0xF7):
                n, k = vlq(d, j + 1)
                j = k + n
                continue
            if st & 0x80:
                run = st
                j += 1
            hi, ch = run & 0xF0, run & 0x0F
            a = d[j]
            b = d[j + 1] if hi not in (0xC0, 0xD0) else 0
            j += 1 if hi in (0xC0, 0xD0) else 2
            if hi == 0x90 and b:
                held.setdefault((ch, a), []).append((tick, b))
            elif hi == 0x80 or (hi == 0x90 and not b):
                q = held.get((ch, a))
                if q:
                    on, vel = q.pop(0)
                    notes.append((on, tick, t, ch, a, vel))
            elif hi == 0xC0:
                progs.setdefault((t, ch), a)
        for (ch, a), q in held.items():                 # never let go: to the track's end
            for on, vel in q:
                notes.append((on, tick, t, ch, a, vel))
        i = end
    tempos.sort()
    sigs.sort()
    bpb = sigs[0][1] if sigs and 1 <= sigs[0][1] <= 16 else 4
    return div, tempos, notes, progs, bpb


def tick_to_sample(div, tempos):
    marks, s, last, us = [], 0.0, 0, 500000             # (tick, seconds there, us per quarter from there)
    for tk, u in tempos:
        s += (tk - last) * us / div / 1e6
        marks.append((tk, s, u))
        last, us = tk, u

    def f(tick):
        t0, s0, u = 0, 0.0, 500000
        for tk, sk, uk in marks:
            if tk > tick:
                break
            t0, s0, u = tk, sk, uk
        return round((s0 + (tick - t0) * u / div / 1e6) * FS)
    return f


def role_of(channel, program):
    """the mode a channel plays in, None: dropped"""
    if channel == 9:
        return DRUMS
    p = 0 if program is None else program
    if p <= 23:
        return KEYS
    if p <= 31:
        return GUITAR
    if p <= 39:
        return BASS
    return None


def title_of(path):
    t = re.sub(r"[^\x20-\x7E]", "?", path.stem)
    return t if len(t) <= TITLE_MAX else t[:TITLE_MAX - 1] + "."


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def program_of(progs, track, ch):
    """a channel's General MIDI program: its track's change, else the first on that channel anywhere"""
    if (track, ch) in progs:
        return progs[(track, ch)]
    for (t, c), p in sorted(progs.items()):
        if c == ch:
            return p
    return None


def convert(path):
    div, tempos, raw, progs, bpb = parse(path)
    to_s = tick_to_sample(div, tempos)
    units = {}                                          # (track, channel) -> its notes
    for n in raw:
        units.setdefault((n[2], n[3]), []).append(n)
    role = {u: role_of(u[1], program_of(progs, *u)) for u in units}
    # a flute (the pipe family, programs 72..79) with more notes than the piano parts plays KEYS instead of them
    flutes = [u for u in units if u[1] != 9 and (program_of(progs, *u) or 0) in FLUTE_PROGRAMS]
    n_keys = sum(len(units[u]) for u in units if role[u] == KEYS)
    n_flute = sum(len(units[u]) for u in flutes)
    keys_from = "piano"
    if flutes and n_flute > n_keys:
        for u in units:
            if role[u] == KEYS:
                role[u] = None
        for u in flutes:
            role[u] = KEYS
        keys_from = f"flute ({n_flute} notes against the piano's {n_keys})"
    elif flutes:
        keys_from = f"piano ({n_keys} notes against the flute's {n_flute})"
    if not any(r == KEYS for r in role.values()):       # no KEYS: the busiest dropped melodic channel plays it
        spare = sorted((u for u in units if role[u] is None), key=lambda u: -len(units[u]))
        if spare:
            role[spare[0]] = KEYS
    dropped = sorted({program_of(progs, *u) for u in units if role[u] is None}, key=lambda p: -1 if p is None else p)
    seen, out, dup = set(), [], 0
    for on, off, t, ch, p, vel in sorted(raw, key=lambda n: (n[0], n[2], n[3], n[4])):
        r = role[(t, ch)]
        if r is None:
            continue
        if (on, p, r) in seen:                          # doubled in its mode: once
            dup += 1
            continue
        seen.add((on, p, r))
        s0 = to_s(on)
        ln = MIN_LEN if r == DRUMS else max(to_s(off) - s0, MIN_LEN)   # (a drum rings out by itself: a hit)
        k = p
        while k < KEY_LO:
            k += 12
        while k > KEY_HI:
            k -= 12
        key = 0 if r == DRUMS else k - KEY_LO
        out.append((s0, ln, key, p, max(1, min(vel, 127)), r | (CN_DRUM if r == DRUMS else 0)))
    out.sort(key=lambda n: (n[0], PRIORITY[n[5] & 3], n[3]))
    # the voice budget: NVOICE at once. Full: a note of a mode that matters more (PRIORITY) takes the voice of a
    # held note of one that matters less (the lowest, then the one held longest), which ends there; else it is dropped
    kept, sounding, cut, cut_by = [], [], 0, [0, 0, 0, 0]   # sounding: indices into kept
    for n in out:
        sounding = [i for i in sounding if kept[i][0] + kept[i][1] > n[0]]
        if len(sounding) >= NVOICE:
            pn = PRIORITY[n[5] & 3]
            worst = max(sounding, key=lambda i: (PRIORITY[kept[i][5] & 3], -kept[i][0]))
            w = kept[worst]
            if PRIORITY[w[5] & 3] <= pn or n[0] - w[0] < MIN_LEN:
                cut += 1
                cut_by[n[5] & 3] += 1
                continue
            kept[worst] = (w[0], n[0] - w[0]) + w[2:]       # held until here
            sounding.remove(worst)
        sounding.append(len(kept))
        kept.append(n)
    kept.sort(key=lambda n: (n[0], n[3]))
    # the start: LEAD_IN before the first note (HOME: its notes fall from the top of the panel first). More silence than
    # that is cut; less (a song starting at once, Amen Break) is padded with whole bars of the first tempo, so the beats
    # (the metronome, COUNT-IN) stay on the bar: lead < 0
    beat_s = to_s(div) - to_s(0)
    first = kept[0][0] if kept else 0
    if first >= LEAD_IN:
        lead = first - LEAD_IN
    else:
        bars = -(-(LEAD_IN - first) // max(1, bpb * beat_s))   # (whole bars, rounded up)
        lead = -bars * bpb * beat_s
    kept = [(n[0] - lead,) + n[1:] for n in kept]
    length = max((n[0] + n[1] for n in kept), default=0)
    # the beats (the metronome: player.c): every quarter note of the tempo map, from the cut on (or through the
    # padding, before the file's first), to the song's end; beat0: the first one's number in its bar's count (the
    # bar's first beat: beat number % bpb == 0)
    beats, beat0 = [], None
    k = lead // beat_s if lead < 0 else 0
    while True:
        t = (to_s(k * div) if k >= 0 else k * beat_s) - lead
        if t > length:
            break
        if t >= 0:
            if beat0 is None:
                beat0 = k % bpb
            beats.append(t)
        k += 1
    bpm = 60e6 / tempos[0][1] if tempos else 120.0
    per = [sum(1 for n in kept if (n[5] & 3) == r) for r in range(4)]
    return {"title": title_of(path), "notes": kept, "length": length, "bpm_x10": round(bpm * 10),
            "per": per, "dup": dup, "cut": cut, "cut_by": cut_by, "dropped": dropped, "lead": lead,
            "keys_from": keys_from, "beats": beats, "beat0": beat0 or 0, "bpb": bpb}


CHART_MAGIC, CHART_VERSION, CHART_HEAD = 0x31434D4D, 2, 72


def chart_bytes(s):
    """song s as a chart (docs/CHART_FORMAT.md): header, beats, notes; the CRC-32 (zlib's) of the beats and notes"""
    import zlib
    parts, sounds = 0, b""
    for p in range(4):
        if s["per"][p]:
            parts |= 1 << p
            sounds += struct.pack("<BBBB", ROLE_SOUND[p][0], ROLE_SOUND[p][1], 100, 0)
        else:
            sounds += bytes(4)
    body = struct.pack(f"<{len(s['beats'])}I", *s["beats"])
    body += b"".join(struct.pack("<IIBBBB", *n) for n in s["notes"])
    title = s["title"].encode("ascii", "replace")[:24]
    head = struct.pack("<IHHIIIHBB24s", CHART_MAGIC, CHART_VERSION, CHART_HEAD, len(s["beats"]), len(s["notes"]),
                       s["length"], s["bpm_x10"], s["bpb"], 0, title)
    head += sounds + struct.pack("<IHBB", zlib.crc32(body) & 0xFFFFFFFF, s["beat0"], parts, 0xFF)
    assert len(head) == CHART_HEAD
    return head + body


def main():
    args = sys.argv[1:]
    charts = None
    if "--charts" in args:
        i = args.index("--charts")
        charts = Path(args[i + 1])
        del args[i:i + 2]
    if not args:
        sys.exit(__doc__)
    out = Path(args[0])
    root = Path(__file__).resolve().parent.parent      # default: the starter songs, then yours
    srcs = [Path(args[1])] if len(args) > 1 else [root / "assets" / "songs", root / "midi"]
    files = [p for src in srcs if src.is_dir()
             for p in sorted(src.glob("*")) if p.suffix.lower() in (".mid", ".midi")]
    songs = []
    for f in files:
        try:
            s = convert(f)
        except (ValueError, IndexError, struct.error) as e:
            print(f"songs: {f.name}: skipped ({e})")
            continue
        if not s["notes"]:
            print(f"songs: {f.name}: skipped (no notes)")
            continue
        songs.append(s)
        modes = ", ".join(f"{ROLE_NAME[r]} {s['per'][r]}" for r in range(4) if s["per"][r])
        cuts = ", ".join(f"{ROLE_NAME[r]} {s['cut_by'][r]}" for r in range(4) if s["cut_by"][r])
        cuts = f" ({cuts})" if cuts else ""
        lead = (f"; {s['lead'] / FS:.1f} s of silence cut" if s['lead'] > 0 else
                f"; {-s['lead'] / FS:.1f} s of silence added before it" if s['lead'] < 0 else "")
        print(f"songs: {f.name}: {len(s['notes'])} notes ({modes}), {len(s['beats'])} beats ({s['bpb']} a bar), "
              f"{s['length'] / FS:.1f} s, "
              f"{s['bpm_x10'] / 10:g} BPM; dropped: {s['dup']} doubled, {s['cut']} over {NVOICE} voices{cuts}, "
              f"programs {s['dropped'] or 'none'}{lead}"
              f"{'; KEYS from the ' + s['keys_from'] if s['keys_from'] != 'piano' else ''}")
    lines = ["/* generated by tools/gen_songs.py from midi/: do not edit. firmware/src/player.c includes it */",
             f"#define MM_NSONGS {len(songs)}u"]
    for i, s in enumerate(songs):
        lines.append(f"static const chart_note_t MM_SONG{i}_NOTE[{len(s['notes'])}] = {{")
        for n in s["notes"]:
            lines.append("    {%du, %du, %d, %d, %d, %d}," % n)
        lines.append("};")
        b = s["beats"] or [0]
        lines.append(f"static const uint32_t MM_SONG{i}_BEAT[{len(b)}] = {{")
        for k in range(0, len(b), 10):
            lines.append("    " + " ".join(f"{t}u," for t in b[k:k + 10]))
        lines.append("};")
    lines.append("static const mm_song_t MM_SONGS[%d] = {" % max(len(songs), 1))
    for i, s in enumerate(songs):
        used = 0
        sounds = []
        for p in range(4):
            if s["per"][p]:
                used |= 1 << p
                sounds.append("{%d, %d, 100, 0}" % ROLE_SOUND[p])
            else:
                sounds.append("{0, 0, 0, 0}")
        lines.append(f"    {{{c_str(s['title'])}, MM_SONG{i}_NOTE, {len(s['notes'])}u, {s['length']}u, "
                     f"{s['bpm_x10']}u, {used}u, {{{', '.join(sounds)}}}, "
                     f"MM_SONG{i}_BEAT, {len(s['beats'])}u, {s['beat0']}u, {s['bpb']}u}},")
    if not songs:
        lines.append('    {"NO SONGS", 0, 0u, 0u, 0u, 0u, {{0, 0, 0, 0}}, 0, 0u, 0u, 4u},')
    lines.append("};")
    text = "\n".join(lines) + "\n"
    if not out.exists() or out.read_text() != text:    # unchanged: keep its time (no rebuild for nothing)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text)
    print(f"songs: {len(songs)} -> {out}")
    if charts:
        charts.mkdir(parents=True, exist_ok=True)
        for s in songs:
            name = re.sub(r"[^A-Za-z0-9 ._-]", "_", s["title"]).strip() or "song"
            (charts / f"{name}.mmc").write_bytes(chart_bytes(s))
        print(f"songs: {len(songs)} charts -> {charts}")


main()
