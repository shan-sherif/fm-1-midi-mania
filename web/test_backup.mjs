// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// fm1backup.js against a simulated device: 7-bit packing, CRC, manifest checks, a full capture,
// validation before the first write, restore order (live music last) and abort on a failed chunk.
import { BACKUP_IDS, BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc, bkManifest, readBackup, captureBackup, restoreBackup, BackupConnection } from "./fm1backup.js";

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(72)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const throws = (f) => { try { f(); return false; } catch { return true; } };
const athrows = async (f) => { try { await f(); return false; } catch { return true; } };

const rnd = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 131 + seed * 17 + (i >> 3)) & 255);
ok(bkR32(bkU32(0xdeadbeef)) === 0xdeadbeef && bkCrc(new TextEncoder().encode("123456789")) === 0xcbf43926, "backup: numbers and CRC-32");
const b = rnd(1000, 1);
ok(bkUnpack(bkPack(b), b.length).every((v, i) => v === b[i]), "backup: 7-bit pack / unpack round trip");
ok(throws(() => bkUnpack([...bkPack(b.subarray(0, 14)), 0], 14)), "backup: trailing bytes refused");

/* a device: objects by id, sample slots, the order of writes */
function device(objs, opt = {}) {
  const d = { objs: new Map(objs), log: [], staged: null };
  d.request = async ([cmd, a]) => {
    if (cmd === BACKUP_CMD.LIST) {
      const out = [1, 0, BACKUP_IDS.length];
      for (const id of BACKUP_IDS) { const v = d.objs.get(id) || new Uint8Array(0); out.push(id, ...bkU32(v.length), ...bkU32(v.length ? bkCrc(v) : 0)); }
      return out;
    }
    if (cmd === BACKUP_CMD.GET) {
      const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = d.objs.get(id);
      if (opt.changeOnGet === id && off === 0) v[0] ^= 1;
      return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))];
    }
    if (cmd === BACKUP_CMD.PUT) {
      const [op, id] = a;
      if (op === 0 && id > (opt.maxId ?? 9)) { d.log.push(`refused ${id}`); return [op, id, 1]; }   // (an older firmware)
      if (op === 0) { d.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, 0]; }
      if (op === 1) {
        if (opt.failChunk === id) return [op, id, 2];
        const off = bkR32(a, 2), p = a.slice(7), n = Math.min(256, d.staged.size - off);
        d.staged.bytes.push(...bkUnpack(p, n)); return [op, id, 0];
      }
      if (op === 2) { const s = Uint8Array.from(d.staged.bytes); const rc = bkCrc(s) === d.staged.crc ? 0 : 2; if (!rc) { d.objs.set(id, s); d.log.push(id); } return [op, id, rc]; }
      if (op === 3) { d.log.push(`abort ${id}`); return [op, id, 0]; }
    }
    if (cmd === 11 || cmd === 12 || cmd === 13 || cmd === 14) { if (cmd === 13 || cmd === 14) d.log.push(32 + a[0]); return [a[0], 0]; }
    throw new Error(`unexpected ${cmd}`);
  };
  return d;
}
const objs = [[0, rnd(3388, 2)], [1, rnd(1200, 3)], [2, rnd(3388, 4)], [6, rnd(3080, 5)]];
const dev = device(objs);
const file = await captureBackup(dev.request, "TEST");
ok(file.objects.length === BACKUP_IDS.length && file.objects[0].size === 3388 && file.objects[3].size === 0, "backup: capture lists every object, empty ones as 0");
ok(readBackup(JSON.stringify(file)).objects[2].bytes.every((v, i) => v === objs[2][1][i]), "backup: capture -> file -> bytes round trip");
ok(await athrows(() => captureBackup(device(objs.map(([i, v]) => [i, v.slice()]), { changeOnGet: 2 }).request, "TEST")), "backup: a device that changes during capture fails the capture");

const bad = JSON.parse(JSON.stringify(file)); bad.objects[2].crc ^= 1;
ok(throws(() => readBackup(bad)), "backup: a damaged file is refused");
const noRun = JSON.parse(JSON.stringify(file)); noRun.objects[0] = { ...noRun.objects[0], size: 0, crc: 0, data: "" };
ok(throws(() => readBackup(noRun)), "backup: a file without the current music is refused");
ok(throws(() => bkManifest([1, 0, 3])), "backup: a short manifest is refused");

const target = device([]);
const damaged = JSON.parse(JSON.stringify(file)); damaged.objects[6].crc ^= 1;
ok(await athrows(() => restoreBackup(target.request, damaged)) && target.log.length === 0, "backup: restore validates every byte before the first write");
await restoreBackup(target.request, file);
const order = target.log.filter((x) => typeof x === "number");
ok(order.at(-1) === 0 && order.at(-2) === 1 && order.indexOf(2) < order.indexOf(1), "backup: restore order: projects, banks, samples, settings, live music last");
ok([0, 1, 2, 6].every((id) => target.objs.get(id).every((v, i) => v === objs.find((o) => o[0] === id)[1][i])), "backup: restored objects equal the source");
{   /* the FM6 patch bank (id 8) and the archives of firmware before it (11 objects, no id 8) */
  ok(BACKUP_IDS.length === 13 && file.objects.some((o) => o.id === 8) && file.objects.some((o) => o.id === 9),
    "backup: 13 objects: id 8 (the retired FM6 bank, empty) and id 9 (the user presets' FM6 patches)");
  const v2 = JSON.parse(JSON.stringify(file)); v2.objects = v2.objects.filter((o) => o.id !== 9);
  ok(readBackup(v2).objects.length === 12, "backup: an archive of 1.0..1.0.2 (12 objects, the bank as id 8) still reads");
  const old = JSON.parse(JSON.stringify(file)); old.objects = old.objects.filter((o) => o.id !== 8 && o.id !== 9);
  ok(readBackup(old).objects.length === 11, "backup: an archive of the 11 objects before FM6 still reads");
  const odd = JSON.parse(JSON.stringify(file)); odd.objects = odd.objects.filter((o) => o.id !== 33);
  ok(throws(() => readBackup(odd)), "backup: an archive missing another object is refused");
}
{   /* 1.0.3: an old archive with a bank restores its id 8 (after 6, 7); a new archive onto 1.0.2 skips id 9 */
  const bankBytes = rnd(3472, 8), patches = rnd(3728, 9);
  const v2dev = device([...objs, [8, bankBytes]]);
  const v2file = await captureBackup(async ([cmd, a]) => {   // (a 1.0.2 device: 12 objects)
    const r = await v2dev.request([cmd, a]);
    if (cmd !== BACKUP_CMD.LIST) return r;
    const n = r[2], keep = [];
    for (let i = 0; i < n; i++) if (r[3 + i * 11] !== 9) keep.push(...r.slice(3 + i * 11, 14 + i * 11));
    return [1, 0, keep.length / 11, ...keep];
  }, "1.0.2");
  const to103 = device([]);
  await restoreBackup(to103.request, v2file);
  const lg = to103.log.filter((x) => typeof x === "number");
  ok(v2file.objects.length === 12 && lg.indexOf(8) > lg.indexOf(7) && lg.indexOf(7) > lg.indexOf(6) &&
     to103.objs.get(8).every((v, i) => v === bankBytes[i]), "backup: a 1.0.2 archive (id 8, the bank) restores, its bank after the user presets");
  const newFile = await captureBackup(device([...objs, [9, patches]]).request, "1.0.3");
  const to102 = device([], { maxId: 8 });
  await restoreBackup(to102.request, newFile);
  ok(to102.log.includes("refused 9") && !to102.objs.has(9) && to102.log.at(-1) === 0,
    "backup: a 1.0.3 archive onto older firmware: id 9 skipped, the rest restored");
  const to103b = device([]);
  await restoreBackup(to103b.request, newFile);
  ok(to103b.objs.get(9).every((v, i) => v === patches[i]), "backup: a 1.0.3 archive restores id 9");
}
{   /* Midi-Mania 0.2.0: the format's name, and the stored songs in the file (web/fm1songs.js captureSongs) */
  const chart = (title, nnote, seed) => {          // a chart's bytes: an MMC1 header and nnote notes, 2 beats
    const c = rnd(72 + 8 + nnote * 12, seed), v = new DataView(c.buffer);
    v.setUint32(0, 0x31434d4d, true); v.setUint16(4, 2, true); v.setUint16(6, 72, true);
    v.setUint32(8, 2, true); v.setUint32(12, nnote, true);
    return { title, bytes: c };
  };
  const songs = [chart("Song A", 3, 1), chart("Song B", 400, 2)];
  ok(file.format === "midi-mania-backup", "backup: new files are \"midi-mania-backup\"");
  const old = JSON.parse(JSON.stringify(file)); old.format = "felucca-backup"; delete old.songs;
  ok(readBackup(old).songs.length === 0, "backup: a \"felucca-backup\" file (the installers before 0.2.0) still reads, no songs");
  const withSongs = await captureBackup(device(objs).request, "FM-1 MIDI-MANIA v0.2.0", () => {}, songs);
  const back = readBackup(JSON.stringify(withSongs)).songs;
  ok(withSongs.songs.length === 2 && back.length === 2 && back[1].title === "Song B" && back[1].size === 4880 &&
     back.every((s, i) => s.bytes.every((v, k) => v === songs[i].bytes[k])), "backup: the songs kept in the file, byte for byte");
  const hurt = JSON.parse(JSON.stringify(withSongs)); hurt.songs[1].crc ^= 1;
  ok(throws(() => readBackup(hurt)), "backup: a song whose checksum fails is refused (the whole file)");
  const notChart = JSON.parse(JSON.stringify(withSongs));
  notChart.songs[0] = { ...notChart.songs[0], size: 117, data: btoa(String.fromCharCode(...rnd(117, 9))) };
  notChart.songs[0].crc = bkCrc(rnd(117, 9));
  ok(throws(() => readBackup(notChart)), "backup: a song that is not a chart (no MMC1, its size not the header's) is refused");
  const many = JSON.parse(JSON.stringify(withSongs)); many.songs = Array(17).fill(many.songs[0]);
  ok(throws(() => readBackup(many)), "backup: more than 16 songs is refused");
  const target2 = device([]);
  await restoreBackup(target2.request, withSongs);
  ok(target2.log.filter((x) => typeof x === "number").at(-1) === 0, "backup: restoreBackup takes a file with songs (the songs go separately)");
}
const failing = device([], { failChunk: 2 });
ok(await athrows(() => restoreBackup(failing.request, file)) && failing.log.includes("abort 2") && !failing.log.includes(0), "backup: a refused chunk aborts that object and stops before the live music");

/* Midi-Mania: no sample slots (its songs are there), so its firmware never answers the sample commands (11..14):
 * restoring with samples: false skips them, and finishes; with them asked for, it would wait for a reply */
{
  const mm = device([]);
  const plain = mm.request;
  mm.request = async (r, o) => {
    if (r[0] >= 11 && r[0] <= 14) throw Object.assign(new Error("Backup timed out (cmd " + r[0] + ")"), { timedOut: true });
    return plain(r, o);
  };
  const file2 = await captureBackup(device([[0, rnd(900, 7)], [1, rnd(300, 8)]]).request, "FM-1 MIDI-MANIA v0.8.0");
  let res = null, err = null;
  try { res = await restoreBackup(mm.request, file2, () => {}, { samples: false }); } catch (e) { err = e; }
  ok(res && !err && res.skipped === 0 && mm.log.includes(0) && mm.log.includes(1),
    "backup: restore on Midi-Mania (samples: false): the sample slots skipped, the rest restored");
  ok(await athrows(() => restoreBackup(mm.request, file2)), "backup: (the sample commands asked of Midi-Mania: no reply, as on the FM-1)");
}

/* the connection over (simulated) MIDI: a slow reply is asked for again, and the late one does not answer the next
 * request; a write (no retries) still fails at once */
{
  const objs = [[0, rnd(1500, 2)], [1, rnd(700, 3)]];
  const dev = device(objs);
  let sent = 0;
  const input = { state: "connected", onmidimessage: null };
  const output = { state: "connected", send: async (m) => {
    const cmd = m[4], args = m.slice(5, -1), n = ++sent;
    const reply = await dev.request([cmd, args]);
    const late = cmd === BACKUP_CMD.GET && n % 3 === 0;           // (every third chunk: slower than its timeout)
    setTimeout(() => input.onmidimessage && input.onmidimessage({ data: Uint8Array.from([240, 125, 70, 76, cmd, ...reply, 247]) }),
               late ? 2300 : 5);
  } };
  const link = new BackupConnection(input, output);
  const t0 = Date.now();
  let file = null, err = null;
  try { file = await captureBackup((r, o) => link.request(r, o), "FM-1 MIDI-MANIA v0.9.0"); } catch (e) { err = e; }
  ok(file && file.objects.length === BACKUP_IDS.length && !err,
    `backup: slow replies asked again, late ones ignored: the capture completes (${sent} requests, ${((Date.now() - t0) / 1000).toFixed(0)} s)`);
  const quiet = new BackupConnection({ state: "connected", onmidimessage: null }, { state: "connected", send: () => {} });
  const t1 = Date.now();
  ok(await athrows(() => quiet.request([BACKUP_CMD.PUT, [0]], { timeout: 300 })) && Date.now() - t1 < 1000,
    "backup: a write that gets no reply is not sent again (fails after its timeout)");
}

console.log(fails ? `BACKUP WEB TESTS FAILED (${fails})` : "backup web tests passed");
process.exit(fails ? 1 : 0);
