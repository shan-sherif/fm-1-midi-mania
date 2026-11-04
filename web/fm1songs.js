// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 fm-1-midi-mania contributors
// Midi-Mania: the FM-1's stored songs over USB-MIDI SysEx (firmware/src/editor_songs.c; web/EDITOR_PROTOCOL.md "Songs").
// Every function takes request([cmd, args], {timeout}) -> the reply's bytes after its cmd (fm1backup.js
// BackupConnection.request; the tests: the firmware's own C protocol, tests/songs_bridge.c).
export const SONG_CMD = { LIST: 88, BEGIN: 89, WRITE: 90, END: 91, DELETE: 92, READ: 93 };
export const SONG_CHUNK = 256;                 // bytes a WRITE (the first 256 go last, with END)
export const SONG_AREA = 60 * 4096;            // the song region (player.c MM_SONG_SIZE): 240 KB for every song
const SONG_ERR = {
  begin: { 1: "the song's size is not a chart's", 2: "not enough room on the FM-1: delete a song first",
           3: "the FM-1 holds 16 songs at most: delete one first", 4: "the FM-1's flash could not be erased",
           5: "the FM-1 is busy: stop the song playing and try again" },
  write: { 1: "a bad chunk", 2: "the FM-1's flash could not be erased", 3: "the FM-1's flash could not be written",
           4: "the upload was interrupted (the FM-1 restarted, or another page used it)" },
  end: { 1: "the upload was interrupted", 2: "the chart's header does not match its size",
         3: "the song arrived damaged (CRC): try again", 4: "the FM-1's flash could not be written",
         5: "the FM-1 is busy: stop the song playing and try again" },
  del: { 1: "that song is no longer on the FM-1", 4: "the FM-1's flash could not be erased",
         5: "the FM-1 is busy: stop the song playing and try again" },
};
const songFail = (kind, rc) => Object.assign(new Error(SONG_ERR[kind][rc] || `error ${rc}`), { rc });
const u21 = (n) => [n & 127, (n >>> 7) & 127, (n >>> 14) & 127];

// 7 bytes in 8: a byte of their top bits, then the 7 bytes' low 7 bits (editor.c ed_unpack7)
export function songPack7(bytes) {
  const a = [];
  for (let off = 0; off < bytes.length; off += 7) {
    const chunk = bytes.subarray(off, off + 7);
    a.push(chunk.reduce((m, b, i) => m | (b >>> 7) << i, 0), ...Array.from(chunk, (b) => b & 127));
  }
  return a;
}

// SONG_LIST's reply: {builtIn, max, sectors, free, gap (sectors: the biggest song that fits), songs: [{sector,
// sectors, parts, bpm, seconds, title}]}; null from a firmware without the song commands (an empty reply)
export function parseList(a) {
  if (!a || a.length < 6) return null;
  const out = { builtIn: a[0], max: a[2], sectors: a[3], free: a[4], gap: a[5], songs: [] };
  let p = 6;
  for (let i = 0; i < a[1]; i++) {
    if (p + 8 > a.length) throw new Error("a damaged song list");
    const s = { sector: a[p], sectors: a[p + 1], parts: a[p + 2], bpm: (a[p + 3] | a[p + 4] << 7) / 10,
                seconds: a[p + 5] | a[p + 6] << 7 };
    p += 7;
    const end = a.indexOf(0, p);
    if (end < 0) throw new Error("a damaged song list");
    s.title = String.fromCharCode(...a.slice(p, end));
    p = end + 1;
    out.songs.push(s);
  }
  return out;
}
// A list's storage line: the space used, and how many more songs fit, estimated from the average size of those
// already there (the space decides long before the 16-song limit does)
export function roomText(l) {
  const n = l.songs.length, used = l.sectors - l.free, left = l.max - n;
  let more;
  if (!left) more = `the most the FM-1 keeps (${l.max}): delete one to add another`;
  else if (!n) more = `${l.free * 4} KB free: the more notes a song has, the more room it takes`;
  else {
    const est = Math.min(left, Math.floor(l.free / (used / n)));
    more = (est ? `room for about ${est} more song${est > 1 ? "s" : ""} of this size`
                : "no room for another song of this size") +
           ` · ${l.free * 4} KB free, largest song that fits: ${l.gap * 4} KB`;
  }
  return `${n} song${n === 1 ? "" : "s"} · ${used * 4} KB of ${l.sectors * 4} KB used · ${more}`;
}
export async function listSongs(request) {
  return parseList(await request([SONG_CMD.LIST, []], { timeout: 2000 }));
}
// a chart (Uint8Array, web/mm_songs.js chartBytes) onto the FM-1: the sector it went to. onProgress(done, total)
export async function uploadChart(request, chart, onProgress = () => {}) {
  const T = { timeout: 4000 };                // (an erase: up to 0.4 s)
  if (chart.length > SONG_AREA)
    throw new Error(`the song is too big for the FM-1 (${Math.ceil(chart.length / 1024)} KB; ${SONG_AREA / 1024} KB at most)`);
  let r = await request([SONG_CMD.BEGIN, u21(chart.length)], T);
  if (r.length < 2) throw new Error("no answer from the FM-1");
  if (r[0]) throw songFail("begin", r[0]);
  const sector = r[1];
  onProgress(0, chart.length);
  for (let off = SONG_CHUNK; off < chart.length; off += SONG_CHUNK) {
    r = await request([SONG_CMD.WRITE, [...u21(off), ...songPack7(chart.subarray(off, off + SONG_CHUNK))]], T);
    if (r.length < 4 || (r[0] | r[1] << 7 | r[2] << 14) !== off) throw new Error("the FM-1 answered out of turn");
    if (r[3]) throw songFail("write", r[3]);
    onProgress(Math.min(chart.length, off + SONG_CHUNK) - SONG_CHUNK, chart.length);
  }
  r = await request([SONG_CMD.END, songPack7(chart.subarray(0, SONG_CHUNK))], T);
  if (r.length < 1) throw new Error("no answer from the FM-1");
  if (r[0]) throw songFail("end", r[0]);
  onProgress(chart.length, chart.length);
  return sector;
}
export async function deleteSong(request, sector) {
  const r = await request([SONG_CMD.DELETE, [sector & 127]], { timeout: 8000 });   // (up to 60 erases)
  if (r.length < 1) throw new Error("no answer from the FM-1");
  if (r[0]) throw songFail("del", r[0]);
}

// ---- the songs in a backup (web/fm1backup.js "songs"; the installer, web/index_pkg.html)
// songPack7's reverse: n bytes from a mask byte, then up to 7 bytes (editor_backup.c ed_bk_pack)
function songUnpack7(a, n) {
  const out = new Uint8Array(n);
  let i = 0, o = 0;
  while (o < n) {
    const mask = a[i++], k = Math.min(7, n - o);
    for (let j = 0; j < k; j++) {
      if (a[i] == null || a[i] > 127) throw new Error("a damaged song chunk");
      out[o++] = a[i++] | ((mask >> j) & 1) << 7;
    }
  }
  return out;
}
// the chart stored at a sector, byte for byte (SONG_READ), its size from its header
export async function readChart(request, sector, onChunk = () => {}) {
  const chunk = async (off, n) => {
    const r = await request([SONG_CMD.READ, [sector & 127, ...u21(off)]], { timeout: 2000 });
    if (r.length < 5) throw new Error("this FM-1 cannot read its songs back (update it)");
    if (r[0] !== sector || (r[1] | r[2] << 7 | r[3] << 14) !== off) throw new Error("the FM-1 answered out of turn");
    if (r[4]) throw new Error(r[4] === 1 ? "that song is no longer on the FM-1" : "a bad song read");
    onChunk(n);
    return songUnpack7(r.slice(5), n);
  };
  const first = await chunk(0, 72);                    // (the header: 72 bytes, then the chunk's rest unread)
  const dv = new DataView(first.buffer);
  if (dv.getUint32(0, true) !== 0x31434D4D) throw new Error("not a song on the FM-1");
  const size = dv.getUint16(6, true) + dv.getUint32(8, true) * 4 + dv.getUint32(12, true) * 12;
  if (size < 72 || size > SONG_AREA) throw new Error("a song with a damaged header");
  const out = new Uint8Array(size);
  for (let off = 0; off < size; off += SONG_CHUNK)
    out.set(await chunk(off, Math.min(SONG_CHUNK, size - off)), off);
  return out;
}
// every stored song: [{title, bytes}]; null from a firmware without song uploads
export async function captureSongs(request, onProgress = () => {}) {
  const l = await listSongs(request);
  if (!l) return null;
  const total = l.songs.reduce((n, s) => n + s.sectors * 4096, 0);
  let done = 0;
  const out = [];
  for (const s of l.songs)
    out.push({ title: s.title, bytes: await readChart(request, s.sector, (n) => onProgress(done += n, total)) });
  return out;
}
// the songs of a backup back onto the FM-1 ([{title, bytes}]), each one not already there (the same title and size):
// {sent, skipped, failed: [{title, error}]}; a song that does not fit is reported, the others still go
export async function restoreSongs(request, songs, onProgress = () => {}) {
  const l = await listSongs(request);
  if (!l) throw new Error("this FM-1 has no song uploads: install Midi-Mania 0.2.0 or later first");
  const have = new Set(l.songs.map((s) => `${s.title}\n${s.sectors}`));
  const total = songs.reduce((n, s) => n + s.bytes.length, 0);
  const res = { sent: 0, skipped: 0, failed: [] };
  let done = 0;
  for (const s of songs) {
    if (have.has(`${s.title}\n${Math.ceil(s.bytes.length / 4096)}`)) { res.skipped++; onProgress(done += s.bytes.length, total); continue; }
    const base = done;
    try {
      await uploadChart(request, s.bytes, (d) => onProgress(base + d, total));
      res.sent++;
    } catch (e) { res.failed.push({ title: s.title, error: e.message }); }
    onProgress(done = base + s.bytes.length, total);
  }
  return res;
}
