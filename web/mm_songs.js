// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 fm-1-midi-mania contributors
// A Standard MIDI File -> a Midi-Mania chart (docs/CHART_FORMAT.md), in the browser: tools/gen_songs.py ported line
// for line, so the same file gives the same chart byte for byte (web/test_songs.mjs checks it against the Python's
// --charts output). Read gen_songs.py for the rules (parts, the flute, the voice budget, the lead-in, the beats).
//
//   import { convertMidi, chartBytes, summary } from "./mm_songs.js";
//   const song = convertMidi(bytes, "My Song.mid");   // throws an Error with a message on a file it cannot read
//   const chart = chartBytes(song);                     // Uint8Array: what goes into the FM-1's song region
//
// Numbers as Python has them: round() rounds halves to even, // and % floor (pyRound, fdiv, fmod below).

const FS = 44100;
const NVOICE = 8;
const KEY_LO = 53, KEY_HI = 79;              // F3 .. G5
const CN_DRUM = 8;
export const KEYS = 0, DRUMS = 1, GUITAR = 2, BASS = 3;
export const ROLE_NAME = ["KEYS", "DRUMS", "GUITAR", "BASS"];
// General MIDI's instruments, by program (the names Studio shows for a file's instruments)
export const GM_NAMES = ("Grand Piano|Bright Piano|Electric Grand|Honky-tonk Piano|Electric Piano 1|Electric Piano 2|" +
  "Harpsichord|Clavinet|Celesta|Glockenspiel|Music Box|Vibraphone|Marimba|Xylophone|Tubular Bells|Dulcimer|" +
  "Drawbar Organ|Percussive Organ|Rock Organ|Church Organ|Reed Organ|Accordion|Harmonica|Tango Accordion|" +
  "Nylon Guitar|Steel Guitar|Jazz Guitar|Clean Guitar|Muted Guitar|Overdriven Guitar|Distortion Guitar|" +
  "Guitar Harmonics|Acoustic Bass|Finger Bass|Pick Bass|Fretless Bass|Slap Bass 1|Slap Bass 2|Synth Bass 1|" +
  "Synth Bass 2|Violin|Viola|Cello|Contrabass|Tremolo Strings|Pizzicato Strings|Harp|Timpani|Strings 1|" +
  "Strings 2|Synth Strings 1|Synth Strings 2|Choir Aahs|Voice Oohs|Synth Choir|Orchestra Hit|Trumpet|Trombone|" +
  "Tuba|Muted Trumpet|French Horn|Brass Section|Synth Brass 1|Synth Brass 2|Soprano Sax|Alto Sax|Tenor Sax|" +
  "Baritone Sax|Oboe|English Horn|Bassoon|Clarinet|Piccolo|Flute|Recorder|Pan Flute|Blown Bottle|Shakuhachi|" +
  "Whistle|Ocarina|Square Lead|Saw Lead|Calliope Lead|Chiff Lead|Charang Lead|Voice Lead|Fifths Lead|" +
  "Bass + Lead|New Age Pad|Warm Pad|Polysynth Pad|Choir Pad|Bowed Pad|Metallic Pad|Halo Pad|Sweep Pad|" +
  "Rain FX|Soundtrack FX|Crystal FX|Atmosphere FX|Brightness FX|Goblins FX|Echoes FX|Sci-fi FX|Sitar|Banjo|" +
  "Shamisen|Koto|Kalimba|Bagpipe|Fiddle|Shanai|Tinkle Bell|Agogo|Steel Drums|Woodblock|Taiko Drum|Melodic Tom|" +
  "Synth Drum|Reverse Cymbal|Guitar Fret Noise|Breath Noise|Seashore|Bird Tweet|Telephone|Helicopter|Applause|" +
  "Gunshot").split("|");
const NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export const noteName = (n) => `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`;   // (60: C4, middle C)
const ROLE_SOUND = [[4, 0], [10, 0], [9, 2], [12, 2]];
const PRIORITY = { [DRUMS]: 0, [KEYS]: 1, [GUITAR]: 2, [BASS]: 3 };
// each melodic part's range (gen_songs.py RANGE), lowest and highest kept: KEYS C2..C7 (61 keys), GUITAR E2..E6,
// BASS E1..G4; DRUMS every note
export const RANGE = { [KEYS]: [36, 96], [GUITAR]: [40, 88], [BASS]: [28, 67] };
const MIN_LEN = Math.floor(FS / 50);
const LEAD_IN = 2 * FS;
const TITLE_MAX = 39;
const CHART_MAGIC = 0x31434D4D, CHART_VERSION = 2, CHART_HEAD = 72;

function pyRound(x) {                         // Python's round(): halves to the even neighbour
  const f = Math.floor(x), d = x - f;
  if (d > 0.5) return f + 1;
  if (d < 0.5) return f;
  return f % 2 === 0 ? f : f + 1;
}
const fdiv = (a, b) => Math.floor(a / b);     // Python's // on ints (exact below 2^53)
const fmod = (a, b) => ((a % b) + b) % b;     // Python's % on ints

class Bytes {                                 // d[i] that throws past the end (Python: IndexError)
  constructor(u8) { this.d = u8; this.length = u8.length; }
  at(i) {
    if (i < 0 || i >= this.d.length) throw new Error("the file ends early");
    return this.d[i];
  }
  u32be(i) { return ((this.at(i) << 24) >>> 0) + (this.at(i + 1) << 16) + (this.at(i + 2) << 8) + this.at(i + 3); }
  u16be(i) { return (this.at(i) << 8) + this.at(i + 1); }
}

function vlq(d, i) {
  let v = 0;
  for (;;) {
    const b = d.at(i++);
    v = v * 128 + (b & 0x7F);
    if (!(b & 0x80)) return [v, i];
  }
}

const key2 = (a, b) => a + "," + b;

function parse(u8) {
  const d = new Bytes(u8);
  if (u8.length < 4 || String.fromCharCode(u8[0], u8[1], u8[2], u8[3]) !== "MThd") throw new Error("not a Standard MIDI File");
  const hl = d.u32be(4), ntrk = d.u16be(10), div = d.u16be(12);
  if (div & 0x8000) throw new Error("SMPTE time division is not supported");
  let i = 8 + hl;
  const tempos = [], notes = [], progs = new Map(), sigs = [];
  for (let t = 0; t < ntrk; t++) {
    if (String.fromCharCode(...u8.subarray(i, i + 4)) !== "MTrk") throw new Error(`track ${t}: no MTrk`);
    const ln = d.u32be(i + 4);
    let j = i + 8, tick = 0, run = 0;
    const end = i + 8 + ln, held = new Map();
    while (j < end) {
      let dt;
      [dt, j] = vlq(d, j);
      tick += dt;
      const st = d.at(j);
      if (st === 0xFF) {
        let n, k;
        [n, k] = vlq(d, j + 2);
        if (d.at(j + 1) === 0x51 && n === 3) {
          const b = u8.subarray(k, k + 3);
          let v = 0;
          for (const x of b) v = v * 256 + x;
          tempos.push([tick, v]);
        } else if (d.at(j + 1) === 0x58 && n >= 1) {
          sigs.push([tick, d.at(k)]);
        }
        j = k + n;
        continue;
      }
      if (st === 0xF0 || st === 0xF7) {
        let n, k;
        [n, k] = vlq(d, j + 1);
        j = k + n;
        continue;
      }
      if (st & 0x80) {
        run = st;
        j += 1;
      }
      const hi = run & 0xF0, ch = run & 0x0F;
      const a = d.at(j);
      const b = (hi !== 0xC0 && hi !== 0xD0) ? d.at(j + 1) : 0;
      j += (hi === 0xC0 || hi === 0xD0) ? 1 : 2;
      if (hi === 0x90 && b) {
        const k = key2(ch, a);
        if (!held.has(k)) held.set(k, []);
        held.get(k).push([tick, b]);
      } else if (hi === 0x80 || (hi === 0x90 && !b)) {
        const q = held.get(key2(ch, a));
        if (q && q.length) {
          const [on, vel] = q.shift();
          notes.push([on, tick, t, ch, a, vel]);
        }
      } else if (hi === 0xC0) {
        const k = key2(t, ch);
        if (!progs.has(k)) progs.set(k, a);
      }
    }
    for (const [k, q] of held) {               // never let go: to the track's end
      const [ch, a] = k.split(",").map(Number);
      for (const [on, vel] of q) notes.push([on, tick, t, ch, a, vel]);
    }
    i = end;
  }
  const cmp2 = (x, y) => x[0] - y[0] || x[1] - y[1];
  tempos.sort(cmp2);
  sigs.sort(cmp2);
  const bpb = sigs.length && sigs[0][1] >= 1 && sigs[0][1] <= 16 ? sigs[0][1] : 4;
  return { div, tempos, notes, progs, bpb };
}

function tickToSample(div, tempos) {
  const marks = [];                           // [tick, seconds there, us per quarter from there]
  let s = 0.0, last = 0, us = 500000;
  for (const [tk, u] of tempos) {
    s += (tk - last) * us / div / 1e6;
    marks.push([tk, s, u]);
    last = tk;
    us = u;
  }
  return (tick) => {
    let t0 = 0, s0 = 0.0, u = 500000;
    for (const [tk, sk, uk] of marks) {
      if (tk > tick) break;
      t0 = tk; s0 = sk; u = uk;
    }
    return pyRound((s0 + (tick - t0) * u / div / 1e6) * FS);
  };
}

function roleOf(channel, program) {
  if (channel === 9) return DRUMS;
  const p = program == null ? 0 : program;
  if (p <= 23) return KEYS;
  if (p <= 31) return GUITAR;
  if (p <= 39) return BASS;
  return null;
}

function titleOf(name) {
  let stem = name.replace(/^.*[\\/]/, "");
  const dot = stem.lastIndexOf(".");
  if (dot > 0) stem = stem.slice(0, dot);       // (Path.stem)
  const t = Array.from(stem).map((c) => (c >= " " && c <= "~" && c.length === 1 ? c : "?")).join("");
  return t.length <= TITLE_MAX ? t : t.slice(0, TITLE_MAX - 1) + ".";
}

function programOf(progs, track, ch) {
  const k = key2(track, ch);
  if (progs.has(k)) return progs.get(k);
  const all = [...progs].map(([kk, p]) => [...kk.split(",").map(Number), p]).sort((x, y) => x[0] - y[0] || x[1] - y[1]);
  for (const [, c, p] of all) if (c === ch) return p;
  return null;
}

// a file's notes by instrument: "track,channel" -> its notes
function unitsOf(raw) {
  const units = new Map();
  for (const n of raw) {
    const k = key2(n[2], n[3]);
    if (!units.has(k)) units.set(k, []);
    units.get(k).push(n);
  }
  return units;
}
const uk = (k) => k.split(",").map(Number);
// each instrument's part, as tools/gen_songs.py chooses it (its program's family; a busier flute over the pianos;
// no KEYS: the busiest one left out plays KEYS): {role: Map "track,channel" -> part or null, keysFrom}
function defaultRoles(units, progs) {
  const role = new Map();
  for (const k of units.keys()) { const [t, c] = uk(k); role.set(k, roleOf(c, programOf(progs, t, c))); }
  const flutes = [...units.keys()].filter((k) => { const [t, c] = uk(k); const p = programOf(progs, t, c) || 0; return c !== 9 && p >= 72 && p < 80; });
  let nKeys = 0, nFlute = 0;
  for (const [k, ns] of units) if (role.get(k) === KEYS) nKeys += ns.length;
  for (const k of flutes) nFlute += units.get(k).length;
  let keysFrom = "piano";
  if (flutes.length && nFlute > nKeys) {
    for (const k of units.keys()) if (role.get(k) === KEYS) role.set(k, null);
    for (const k of flutes) role.set(k, KEYS);
    keysFrom = `flute (${nFlute} notes against the piano's ${nKeys})`;
  } else if (flutes.length) {
    keysFrom = `piano (${nKeys} notes against the flute's ${nFlute})`;
  }
  if (![...role.values()].some((r) => r === KEYS)) {   // no KEYS: the busiest dropped melodic channel plays it
    const spare = [...units.keys()].filter((k) => role.get(k) === null).sort((a, b) => units.get(b).length - units.get(a).length);
    if (spare.length) role.set(spare[0], KEYS);
  }
  return { role, keysFrom };
}

// The parts an instrument can be given: the drum channel (10) DRUMS or none; any other KEYS, GUITAR, BASS or none
const roleOk = (channel, r) => r === null || (channel === 9 ? r === DRUMS : r === KEYS || r === GUITAR || r === BASS);

// a MIDI file's instruments, for choosing their parts before converting (Studio, Your songs): [{key "track,channel",
// track, channel (0..15), program (null: none), name, notes, lo, hi (MIDI notes), role (the part convertMidi gives
// it by itself: KEYS DRUMS GUITAR BASS, null left out)}], by their first note
export function midiInstruments(u8) {
  const { notes: raw, progs } = parse(u8);
  const units = unitsOf(raw), { role } = defaultRoles(units, progs);
  return [...units].map(([key, ns]) => {
    const [track, channel] = uk(key), program = programOf(progs, track, channel);
    let lo = 127, hi = 0;
    for (const n of ns) { lo = Math.min(lo, n[4]); hi = Math.max(hi, n[4]); }
    return { key, track, channel, program, name: channel === 9 ? "Drum kit" : GM_NAMES[program == null ? 0 : program],
             notes: ns.length, lo, hi, role: role.get(key), first: ns.reduce((m, n) => Math.min(m, n[0]), Infinity) };
  }).sort((a, b) => a.first - b.first || a.channel - b.channel).map(({ first, ...i }) => i);
}

// The whole file for Studio's piano roll (its Start and End handles): every note in the file's own time, samples,
// [on, off, instrument key (midiInstruments'), pitch]; its beats (samples, from the file's start, past its end) and
// beats to a bar; end: the last note's end. Nothing here changes a chart
export function midiRoll(u8) {
  const { div, tempos, notes: raw, bpb } = parse(u8);
  const toS = tickToSample(div, tempos), notes = raw.map((n) => [toS(n[0]), toS(n[1]), key2(n[2], n[3]), n[4]]);
  let end = 0;
  for (const n of notes) end = Math.max(end, n[1]);
  const beats = [];
  for (let k = 0; ; k++) { const t = toS(k * div); beats.push(t); if (t > end || k > 100000) break; }
  return { notes, beats, bpb: bpb || 4, end };
}

// opt.roles: {"track,channel": part or null} (midiInstruments' keys): your choice for those instruments, over the
// converter's own (a part an instrument cannot take is ignored). Without it, the chart is tools/gen_songs.py's, byte
// for byte (web/test_songs.mjs)
// opt.roles: your parts (midiInstruments' keys -> a part or null); opt.from, opt.to: only the notes starting from
// opt.from to opt.to seconds into the file (Studio's Start and End: a song made shorter), each one held at most to
// opt.to. Without them, the same song as tools/gen_songs.py makes, byte for byte
export function convertMidi(u8, name, opt = {}) {
  const { div, tempos, notes: raw, progs, bpb } = parse(u8);
  const toS = tickToSample(div, tempos);
  const units = unitsOf(raw);
  let { role, keysFrom } = defaultRoles(units, progs);
  if (opt.roles) {
    for (const [k, r] of Object.entries(opt.roles))
      if (units.has(k) && roleOk(uk(k)[1], r)) role.set(k, r);
    keysFrom = "piano";                         // (the parts are yours: no note about where KEYS came from)
  }
  const droppedSet = new Set();
  for (const k of units.keys()) if (role.get(k) === null) { const [t, c] = uk(k); droppedSet.add(programOf(progs, t, c)); }
  const dropped = [...droppedSet].sort((a, b) => (a == null ? -1 : a) - (b == null ? -1 : b));
  const seen = new Set(), out = [], oorBy = [0, 0, 0, 0];
  let dup = 0;
  const sortedRaw = raw.slice().sort((x, y) => x[0] - y[0] || x[2] - y[2] || x[3] - y[3] || x[4] - y[4]);
  const trim = opt.from > 0 || opt.to != null;
  const fromS = trim ? Math.round((opt.from || 0) * FS) : 0, toS2 = trim && opt.to != null ? Math.round(opt.to * FS) : Infinity;
  for (const [on, off, t, ch, p, vel] of sortedRaw) {
    const r = role.get(key2(t, ch));
    if (r === null) continue;
    if (trim) { const s0 = toS(on); if (s0 < fromS || s0 >= toS2) continue; }   // (outside Start..End: not in it)
    if (RANGE[r] && (p < RANGE[r][0] || p > RANGE[r][1])) { oorBy[r]++; continue; }   // outside its range: dropped
    const sk = on + "," + p + "," + r;
    if (seen.has(sk)) { dup++; continue; }
    seen.add(sk);
    const s0 = toS(on);
    const ln = r === DRUMS ? MIN_LEN : Math.max(Math.min(toS(off), toS2) - s0, MIN_LEN);
    let k = p;
    while (k < KEY_LO) k += 12;
    while (k > KEY_HI) k -= 12;
    const key = r === DRUMS ? 0 : k - KEY_LO;
    out.push([s0, ln, key, p, Math.max(1, Math.min(vel, 127)), r | (r === DRUMS ? CN_DRUM : 0)]);
  }
  out.sort((x, y) => x[0] - y[0] || PRIORITY[x[5] & 3] - PRIORITY[y[5] & 3] || x[3] - y[3]);
  // the voice budget (gen_songs.py): NVOICE at once; a note of a part that matters more takes a voice of one that
  // matters less (the lowest, then the one held longest), which ends there; else it is dropped
  const kept = [], cutBy = [0, 0, 0, 0];
  let sounding = [], cut = 0;
  for (const n of out) {
    sounding = sounding.filter((i) => kept[i][0] + kept[i][1] > n[0]);
    if (sounding.length >= NVOICE) {
      const pn = PRIORITY[n[5] & 3];
      let worst = sounding[0];
      for (const i of sounding) {               // (Python max: the first of the largest)
        const a = [PRIORITY[kept[i][5] & 3], -kept[i][0]], b = [PRIORITY[kept[worst][5] & 3], -kept[worst][0]];
        if (a[0] > b[0] || (a[0] === b[0] && a[1] > b[1])) worst = i;
      }
      const w = kept[worst];
      if (PRIORITY[w[5] & 3] <= pn || n[0] - w[0] < MIN_LEN) {
        cut++;
        cutBy[n[5] & 3]++;
        continue;
      }
      kept[worst] = [w[0], n[0] - w[0], ...w.slice(2)];
      sounding.splice(sounding.indexOf(worst), 1);
    }
    sounding.push(kept.length);
    kept.push(n);
  }
  kept.sort((x, y) => x[0] - y[0] || x[3] - y[3]);
  const beatS = toS(div) - toS(0);
  const first = kept.length ? kept[0][0] : 0;
  let lead;
  if (first >= LEAD_IN) {
    lead = first - LEAD_IN;
  } else {
    const bars = -fdiv(-(LEAD_IN - first), Math.max(1, bpb * beatS));
    lead = -bars * bpb * beatS;
  }
  const notes = kept.map((n) => [n[0] - lead, ...n.slice(1)]);
  let length = 0;
  for (const n of notes) length = Math.max(length, n[0] + n[1]);
  const beats = [];
  let beat0 = null;
  let k = lead < 0 ? fdiv(lead, beatS) : 0;
  for (;;) {
    const t = (k >= 0 ? toS(k * div) : k * beatS) - lead;
    if (t > length) break;
    if (t >= 0) {
      if (beat0 === null) beat0 = fmod(k, bpb);
      beats.push(t);
    }
    k++;
  }
  const bpm = tempos.length ? 60e6 / tempos[0][1] : 120.0;
  const per = [0, 1, 2, 3].map((r) => notes.filter((n) => (n[5] & 3) === r).length);
  return {
    title: titleOf(name), notes, length, bpm_x10: pyRound(bpm * 10), per, dup, oor: oorBy.reduce((a, b) => a + b, 0), oor_by: oorBy, cut,
    cut_by: cutBy, dropped, lead,
    keys_from: keysFrom, beats, beat0: beat0 || 0, bpb,
  };
}

let crcTable = null;
export function crc32(u8) {                   // zlib's CRC-32
  if (!crcTable) {
    crcTable = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let j = 0; j < 8; j++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      crcTable[n] = c >>> 0;
    }
  }
  let c = 0xFFFFFFFF;
  for (let i = 0; i < u8.length; i++) c = crcTable[(c ^ u8[i]) & 0xFF] ^ (c >>> 8);
  return (c ^ 0xFFFFFFFF) >>> 0;
}

export function chartBytes(s) {
  const body = new Uint8Array(s.beats.length * 4 + s.notes.length * 12);
  const bv = new DataView(body.buffer);
  let o = 0;
  for (const t of s.beats) { bv.setUint32(o, t, true); o += 4; }
  for (const n of s.notes) {
    bv.setUint32(o, n[0], true); bv.setUint32(o + 4, n[1], true);
    body[o + 8] = n[2]; body[o + 9] = n[3]; body[o + 10] = n[4]; body[o + 11] = n[5];
    o += 12;
  }
  const out = new Uint8Array(CHART_HEAD + body.length);
  const h = new DataView(out.buffer);
  h.setUint32(0, CHART_MAGIC, true);
  h.setUint16(4, CHART_VERSION, true);
  h.setUint16(6, CHART_HEAD, true);
  h.setUint32(8, s.beats.length, true);
  h.setUint32(12, s.notes.length, true);
  h.setUint32(16, s.length, true);
  h.setUint16(20, s.bpm_x10, true);
  out[22] = s.bpb;
  out[23] = 0;                                  // level
  const title = Array.from(s.title).slice(0, 24);
  title.forEach((c, i) => { const v = c.charCodeAt(0); out[24 + i] = v < 128 ? v : 63; });
  let parts = 0;
  for (let p = 0; p < 4; p++) {
    if (s.per[p]) {
      parts |= 1 << p;
      out.set([ROLE_SOUND[p][0], ROLE_SOUND[p][1], 100, 0], 48 + 4 * p);
    }
  }
  h.setUint32(64, crc32(body), true);
  h.setUint16(68, s.beat0, true);
  out[70] = parts;
  out[71] = 0xFF;
  out.set(body, CHART_HEAD);
  return out;
}

export function summary(s) {                  // the converter's line, as gen_songs.py prints it
  const modes = [0, 1, 2, 3].filter((r) => s.per[r]).map((r) => `${ROLE_NAME[r]} ${s.per[r]}`).join(", ");
  return `${s.notes.length} notes (${modes}), ${s.beats.length} beats (${s.bpb} a bar), ${(s.length / FS).toFixed(1)} s, ` +
    `${s.bpm_x10 / 10} BPM; dropped: ${s.dup} doubled, ${s.oor} out of range, ${s.cut} over ${NVOICE} voices`;
}
