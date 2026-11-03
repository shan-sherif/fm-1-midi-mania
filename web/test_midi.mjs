// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 fm-1-midi-mania contributors
// A small Standard MIDI File for the tests (web/test_songs.mjs, platforms/web/emu_test.mjs), made here so no song
// file has to live in the repository. It touches what the converter must get right: format 1 with a tempo track, a
// tempo change, 3/4 time, drums on channel 10 written with running status, note-offs as note-ons of velocity 0, a
// note never let go, a piano part (KEYS), a bass part (BASS), and a lead-in shorter than 2 s (whole bars added).
const PPQ = 480;

function vlq(v) {
  const out = [v & 0x7F];
  while ((v >>= 7)) out.unshift(0x80 | (v & 0x7F));
  return out;
}
function track(events) {                                // [tick, bytes]: sorted, note-offs first at a tick
  events.sort((a, b) => a[0] - b[0] || ((a[1][0] & 0xF0) === 0x80 ? -1 : 0) - ((b[1][0] & 0xF0) === 0x80 ? -1 : 0));
  const d = [];
  let now = 0, run = 0;
  for (const [t, b] of events) {
    d.push(...vlq(t - now));
    now = t;
    if (b[0] < 0xF0 && b[0] === run) d.push(...b.slice(1));   // running status
    else { d.push(...b); run = b[0] < 0xF0 ? b[0] : 0; }
  }
  d.push(0, 0xFF, 0x2F, 0);
  return [0x4D, 0x54, 0x72, 0x6B, (d.length >>> 24) & 255, (d.length >>> 16) & 255, (d.length >>> 8) & 255, d.length & 255, ...d];
}

export function testMidi() {
  const bar = 3 * PPQ, meta = [], drums = [], keys = [], bass = [];
  meta.push([0, [0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20]]);          // 120 BPM
  meta.push([0, [0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08]]);    // 3/4
  meta.push([4 * bar, [0xFF, 0x51, 0x03, 0x06, 0x1A, 0x80]]);     // 150 BPM from bar 5
  for (let b = 0; b < 8; b++) {
    for (let q = 0; q < 3; q++) {
      const t = b * bar + q * PPQ, n = q === 0 ? 36 : q === 1 ? 38 : 42;
      drums.push([t, [0x99, n, 100]], [t + 60, [0x99, n, 0]]);    // (0x99 again: running status)
    }
    drums.push([b * bar + PPQ / 2, [0x99, 46, 80]], [b * bar + PPQ / 2 + 60, [0x89, 46, 0]]);
  }
  keys.push([0, [0xC0, 0]]);
  const melody = [60, 62, 64, 65, 67, 69, 71, 72, 84, 48, 79, 77];   // (84 and 48: folded onto the 27 keys)
  melody.forEach((p, i) => keys.push([PPQ / 2 + i * PPQ, [0x90, p, 90]], [PPQ / 2 + i * PPQ + PPQ - 40, [0x80, p, 0]]));
  keys.push([7 * bar, [0x90, 76, 70]]);                              // never let go: to the track's end
  bass.push([0, [0xC1, 33]]);
  for (let b = 0; b < 8; b++) bass.push([b * bar, [0x91, 36, 80]], [b * bar + PPQ, [0x91, 36, 0]]);
  const tracks = [track(meta), track(drums), track(keys), track(bass)];
  return Uint8Array.from([0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 1, 0, tracks.length, PPQ >> 8, PPQ & 255, ...tracks.flat()]);
}
