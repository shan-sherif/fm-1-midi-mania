// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 fm-1-midi-mania contributors
// The song upload page's code (web/fm1songs.js) against the firmware's own editor protocol (tests/songs_bridge.c:
// editor.c and editor_songs.c on a simulated song region): MIDI files converted as the page converts them
// (web/mm_songs.js), uploaded, listed, kept over a power-on, deleted, and the errors the page shows.
//   node web/test_songs_usb.mjs BRIDGE MIDI_DIR      (tests/run_tests.sh: build/host/songs_bridge assets/songs)
import fs from "node:fs";
import path from "node:path";
import readline from "node:readline";
import { spawn } from "node:child_process";
import { convertMidi, chartBytes } from "./mm_songs.js";
import { testMidi } from "./test_midi.mjs";
import { SONG_CMD, listSongs, uploadChart, deleteSong, parseList, roomText, readChart, captureSongs, restoreSongs } from "./fm1songs.js";

let bad = 0;
const check = (name, ok) => { console.log(`songs usb: ${name.padEnd(100)} ${ok ? "ok" : "FAIL"}`); if (!ok) bad++; };
const [bridgePath, midiDir] = process.argv.slice(2);

const bridge = spawn(bridgePath, [], { stdio: ["pipe", "pipe", "inherit"] });
const lines = readline.createInterface({ input: bridge.stdout });
const waiting = [];
lines.on("line", (l) => waiting.shift()?.(l));
const send = (text) => new Promise((resolve) => { waiting.push(resolve); bridge.stdin.write(text + "\n"); });
// fm1backup.js BackupConnection.request's shape: [cmd, args] -> the reply's bytes after its cmd
const request = async ([cmd, args]) => {
  const l = await send(`${cmd} ${args.map((b) => b.toString(16)).join(" ")}`);
  return l.trim() ? l.trim().split(/\s+/).map((h) => parseInt(h, 16)) : [];
};
const powerOn = async () => +(await send("S"));
const throwsWith = async (f, re) => { try { await f(); return false; } catch (e) { return re.test(e.message); } };

const chartOf = (file) => chartBytes(convertMidi(new Uint8Array(fs.readFileSync(file)), path.basename(file)));
const mids = fs.readdirSync(midiDir).filter((f) => /\.midi?$/i.test(f)).sort().map((f) => path.join(midiDir, f));
const charts = mids.map(chartOf);
const titleOf = (c) => Buffer.from(c.subarray(24, 48)).toString("latin1").replace(/\0+$/, "");

let l = await listSongs(request);
check(`LIST, none stored: ${l.builtIn} built in, 16 at most, 60 sectors all free`,
      l && l.songs.length === 0 && l.max === 16 && l.sectors === 60 && l.free === 60 && l.gap === 60);

{
  const marks = [];
  let ok = mids.length >= 2;
  for (const c of charts) {
    const sector = await uploadChart(request, c, (done, total) => marks.push([done, total]));
    ok &&= sector >= 0;
  }
  l = await listSongs(request);
  const used = charts.reduce((n, c) => n + Math.ceil(c.length / 4096), 0);
  check(`${mids.map((f) => path.basename(f)).join(", ")} converted and uploaded: listed with their titles, ` +
        `${used} sectors used; progress from 0 to the end`,
        ok && l.songs.length === charts.length && l.songs.every((s, i) => s.title === titleOf(charts[i])) &&
        l.free === 60 - used && marks[0][0] === 0 && marks.at(-1)[0] === marks.at(-1)[1] &&
        marks.every(([d, t], i) => d <= t && (!i || d >= marks[i - 1][0] || t !== marks[i - 1][1])));
  check("kept over a power-on: listed again by the firmware's scan",
        await powerOn() === charts.length && (await listSongs(request)).songs.length === charts.length);
}

{
  const first = l.songs[0], small = chartBytes(convertMidi(testMidi(), "Into The Gap.mid"));
  await deleteSong(request, first.sector);
  const after = await listSongs(request);
  check(`DELETE of "${first.title}": gone, the others where they were`,
        after.songs.length === charts.length - 1 && after.songs.every((s, i) => s.sector === l.songs[i + 1].sector));
  const sector = await uploadChart(request, small);
  const again = await listSongs(request);
  check("the next upload goes into the gap it left (sector 0)",
        sector === 0 && again.songs[0].title === "Into The Gap" && again.songs.length === charts.length);
  check("DELETE of a sector no song starts at: the page's message",
        await throwsWith(() => deleteSong(request, 59), /no longer on the FM-1/));
}

{   // the backup's songs (the installer: before Return to official V15, and Restore a backup)
  const sent = new Map([...charts.map((c) => [titleOf(c), c]),
                        ["Into The Gap", chartBytes(convertMidi(testMidi(), "Into The Gap.mid"))]]);
  const same = (a, b) => a && b && a.length === b.length && a.every((v, i) => v === b[i]);
  const marks = [];
  const songs = await captureSongs(request, (d, t) => marks.push([d, t]));
  const listed = (await listSongs(request)).songs;
  check(`captureSongs (SONG_READ): the ${listed.length} stored songs read back, each the chart sent, byte for byte`,
        songs.length === listed.length && songs.every((s, i) => s.title === listed[i].title && same(s.bytes, sent.get(s.title))) &&
        marks.length > 0 && marks.every(([d, t]) => d <= t));
  let r = await restoreSongs(request, songs);
  check("restoreSongs onto the same FM-1: every song already there, none sent twice",
        r.sent === 0 && r.skipped === songs.length && !r.failed.length && (await listSongs(request)).songs.length === listed.length);
  for (const s of listed) await deleteSong(request, s.sector);
  r = await restoreSongs(request, songs);
  const back = await captureSongs(request);
  check("restoreSongs onto an FM-1 without them: all sent back, the same bytes when read again",
        r.sent === songs.length && !r.failed.length && back.length === songs.length &&
        back.every((s) => same(s.bytes, sent.get(s.title))));
  check("readChart of a sector no song starts at: the page's message",
        await throwsWith(() => readChart(request, 59), /no longer on the FM-1/));
}

{
  const broken = chartBytes(convertMidi(testMidi(), "Broken.mid"));
  broken[broken.length - 1] ^= 0x20;            // (a byte damaged on the way: its CRC fails)
  const before = (await listSongs(request)).songs.length;
  check("a chart damaged on the way: the page's CRC message, nothing listed",
        await throwsWith(() => uploadChart(request, broken), /damaged \(CRC\)/) &&
        (await listSongs(request)).songs.length === before);
  const huge = (sectors) => {                   // a chart that many sectors long: its notes count says so
    const c = new Uint8Array(sectors * 4096 - 100), t = chartBytes(convertMidi(testMidi(), "Huge.mid"));
    c.set(t.subarray(0, 72));
    const dv = new DataView(c.buffer);
    dv.setUint32(8, 0, true);
    dv.setUint32(12, (c.length - 72) / 12, true);
    return c.subarray(0, 72 + Math.floor((c.length - 72) / 12) * 12);
  };
  check("a chart bigger than the room left: the page's 'not enough room' message",
        await throwsWith(() => uploadChart(request, huge(59)), /not enough room/));
  check("a chart bigger than the whole song memory: 'too big', before anything is sent",
        await throwsWith(() => uploadChart(request, huge(61)), /too big for the FM-1/));
  let n = before;
  const tiny = chartBytes(convertMidi(testMidi(), "Filler.mid"));
  while (n < 16 && (await listSongs(request)).gap >= Math.ceil(tiny.length / 4096)) { await uploadChart(request, tiny); n++; }
  check(`16 songs stored: a 17th refused with the page's message`,
        n === 16 && await throwsWith(() => uploadChart(request, tiny), /16 songs at most/));
  for (const s of (await listSongs(request)).songs) await deleteSong(request, s.sector);
  l = await listSongs(request);
  check("every song deleted: none listed, 60 sectors free", l.songs.length === 0 && l.free === 60);
}

check("a firmware without the song commands (an empty reply): parseList says so (null)",
      parseList([]) === null && SONG_CMD.LIST === 88);

// the storage line: the estimate from the average song (4 songs in 51 sectors: 9 free fit none more like them)
const room = (n, free, gap, size = 1) =>
  roomText({ max: 16, sectors: 60, free, gap, songs: Array.from({ length: n }, () => ({ sectors: size })) });
check("roomText: 4 songs in 204 KB: no room for another of this size, 36 KB free",
      /^4 songs · 204 KB of 240 KB used · no room for another song of this size · 36 KB free, largest song that fits: 36 KB$/
        .test(room(4, 9, 9)));
check("roomText: 2 songs in 12 KB: 38 more of 6 KB by space, capped at 14 by the 16-song limit",
      /room for about 14 more songs of this size/.test(room(2, 57, 57)));
check("roomText: 3 songs in 32 KB: 19 by space, 13 by the limit -> 13; 1 song: singular",
      /about 13 more songs/.test(room(3, 52, 52)) && /^1 song · /.test(room(1, 59, 59)) &&
      /about 1 more song of/.test(roomText({ max: 16, sectors: 60, free: 30, gap: 30, songs: [{}] })));
check("roomText: none yet: the free space; 16: delete one first",
      /^0 songs · 0 KB of 240 KB used · 240 KB free/.test(room(0, 60, 60)) &&
      /the most the FM-1 keeps \(16\): delete one/.test(room(16, 40, 40)));

bridge.stdin.end();
process.exit(bad ? 1 : 0);
