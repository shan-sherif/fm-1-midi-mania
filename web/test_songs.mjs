// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 fm-1-midi-mania contributors
// web/mm_songs.js (the converter in the browser) against tools/gen_songs.py: every MIDI file in the folders given
// converts to the same chart byte for byte as the Python's --charts output; damaged files are refused with an
// error, never a hang or a crash.
//   node web/test_songs.mjs CHARTS_DIR MIDI_DIR...      (tests/run_tests.sh: build/host/charts midi)
// Also always web/test_midi.mjs's file (made here: midi/ is empty in CI), through tools/gen_songs.py in a temp folder.
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { convertMidi, chartBytes, crc32, summary, midiInstruments, noteName, GM_NAMES, DRUMS, KEYS, GUITAR, BASS } from "./mm_songs.js";
import { testMidi } from "./test_midi.mjs";

let bad = 0;
const check = (name, ok) => { console.log(`songs.js: ${name.padEnd(100)} ${ok ? "ok" : "FAIL"}`); if (!ok) bad++; };

const [chartsDir, ...midiDirs] = process.argv.slice(2);
const charts = new Map();                       // title (24 bytes) -> the Python's chart
const readCharts = (dir) => {
  for (const f of fs.existsSync(dir) ? fs.readdirSync(dir) : []) {
    if (!f.endsWith(".mmc")) continue;
    const b = new Uint8Array(fs.readFileSync(path.join(dir, f)));
    charts.set(Buffer.from(b.subarray(24, 48)).toString("latin1").replace(/\0+$/, ""), b);
  }
};
readCharts(chartsDir);
{   // the made-up file: its own folder, converted by the Python too
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "mm-songs-"));
  fs.mkdirSync(path.join(tmp, "midi"));
  fs.writeFileSync(path.join(tmp, "midi", "Test Song.mid"), testMidi());
  const tool = new URL("../tools/gen_songs.py", import.meta.url).pathname;
  execFileSync("python3", [tool, path.join(tmp, "songs.h"), path.join(tmp, "midi"), "--charts", path.join(tmp, "charts")]);
  readCharts(path.join(tmp, "charts"));
  midiDirs.unshift(path.join(tmp, "midi"));
}
check("CRC-32: zlib's (\"123456789\" -> cbf43926)", crc32(new TextEncoder().encode("123456789")) === 0xcbf43926);

let files = 0;
for (const dir of midiDirs) {
  if (!fs.existsSync(dir)) continue;
  for (const f of fs.readdirSync(dir).sort()) {
    if (!/\.midi?$/i.test(f)) continue;
    const u8 = new Uint8Array(fs.readFileSync(path.join(dir, f)));
    let js = null, err = "";
    try { js = chartBytes(convertMidi(u8, f)); } catch (e) { err = e.message; }
    const title = js ? Buffer.from(js.subarray(24, 48)).toString("latin1").replace(/\0+$/, "") : "";
    const py = charts.get(title);
    check(`${f}: ${js ? summary(convertMidi(u8, f)) : err}`, js && py && js.length === py.length && Buffer.compare(Buffer.from(js), Buffer.from(py)) === 0);
    files++;
    // damaged copies: cut short at many places, and bytes changed at random (a fixed seed): an Error or a chart
    let seed = 12345, refused = 0, crashed = 0;
    const rnd = (n) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % n; };
    const tryOne = (b) => {
      try { chartBytes(convertMidi(b, f)); } catch (e) { if (e instanceof Error && e.message) refused++; else crashed++; }
    };
    for (let k = 1; k < 40; k++) tryOne(u8.subarray(0, Math.floor(u8.length * k / 40)));
    for (let k = 0; k < 60; k++) {
      const b = u8.slice();
      for (let m = 0; m < 4; m++) b[rnd(b.length)] = rnd(256);
      tryOne(b);
    }
    check(`${f}: 99 damaged copies, each an error message or a chart (${refused} refused), none crashed`, !crashed);
  }
}
check(`${files} MIDI files compared`, files > 0);
console.log(bad ? `songs.js: ${bad} FAILED` : "songs.js: all ok");
// choosing the parts (Studio: Your songs): the file's instruments listed with the converter's own choice; your choice
// over it, a part an instrument cannot take ignored; the same choice as the converter's: the same chart
{
  const u8 = testMidi(), inst = midiInstruments(u8), by = (n) => inst.find((i) => i.name === n);
  const piano = by("Grand Piano"), bass = by("Finger Bass"), drums = by("Drum kit");
  check(`midiInstruments: ${inst.map((i) => `${i.name} ch${i.channel + 1} ${i.notes} ${noteName(i.lo)}..${noteName(i.hi)}`).join(", ")}`,
        inst.length === 3 && GM_NAMES.length === 128 && noteName(60) === "C4" &&
        piano && piano.role === KEYS && piano.notes === 13 && piano.lo === 48 && piano.hi === 84 && piano.channel === 0 &&
        bass && bass.role === BASS && bass.notes === 8 && bass.program === 33 &&
        drums && drums.role === DRUMS && drums.channel === 9);
  const same = Object.fromEntries(inst.map((i) => [i.key, i.role]));
  const a = chartBytes(convertMidi(u8, "Test Song.mid")), b = chartBytes(convertMidi(u8, "Test Song.mid", { roles: same }));
  check("your choice the same as the converter's: the same chart, byte for byte",
        a.length === b.length && a.every((v, i) => v === b[i]));
  const g = convertMidi(u8, "Test Song.mid", { roles: { [bass.key]: GUITAR } });
  check("the bass put on GUITAR: its 8 notes on part 3, none on BASS", g.per[GUITAR] === 8 && g.per[BASS] === 0);
  const o = convertMidi(u8, "Test Song.mid", { roles: { [piano.key]: null } });
  check("the piano left out: no KEYS, the drums and bass kept", o.per[KEYS] === 0 && o.per[DRUMS] > 0 && o.per[BASS] === 8);
  const x = convertMidi(u8, "Test Song.mid", { roles: { [drums.key]: KEYS, [piano.key]: DRUMS } });
  check("a part an instrument cannot take (the drum kit on KEYS, the piano on DRUMS): ignored",
        x.per[DRUMS] === convertMidi(u8, "Test Song.mid").per[DRUMS] && x.per[KEYS] === convertMidi(u8, "Test Song.mid").per[KEYS]);
}

process.exit(bad ? 1 : 0);
