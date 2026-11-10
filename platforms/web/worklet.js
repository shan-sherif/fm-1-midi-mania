// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Felucca in an AudioWorklet: midi-mania.wasm is the whole FM-1 firmware against a simulated FM-1
// (platforms/host/mm_host.c). Each render quantum asks it for 128 frames, which runs the device's clock forward;
// between quanta the page's input goes in and, about 30 times a second (4 when the page is hidden), the screen,
// the lights and any changed flash go out. After X0X's platforms/web/worklet.js (charlesvestal/fm1-x0x, GPL-3.0).

// The flash the page keeps: storage.c's areas (projects, user presets and their FM6 patches, the user sample
// slots 0x97000..0xDFFFF; the autosave 0xE5000..0xE6FFF (1.2) and FM6's voice bank 0xE7000 (1.4.1); the settings and
// the second FM6 copy 0xFC000..0xFFFFF), only the sectors that hold something (an erased sector is all 0xFF). {offset: bytes}
export const STORE_AREAS = [[0x97000, 0xE0000], [0xE5000, 0xE8000], [0xFC000, 0x100000], [0x100000, 0x200000]];
// the FM-1's song region (player.c), where Studio kept its songs before they had a region of their own
export const FM1_SONGS = [0xA0000, 0xDC000];
export function storeSectors(nor) {
  const out = {};
  for (const [a, b] of STORE_AREAS)
    for (let off = a; off < b; off += 4096) {
      const s = nor.subarray(off, off + 4096);
      if (s.some((v) => v !== 0xFF)) out[off] = s.slice();
    }
  return out;
}
// songBase: where this build keeps its songs (fh_song_base); songs saved at the FM-1's region by an earlier Studio
// move there, each sector to the same place in it
export function restoreSectors(nor, sectors, songBase = FM1_SONGS[0]) {
  for (const [k, v] of Object.entries(sectors || {})) {
    let off = +k;
    const b = v instanceof Uint8Array ? v : new Uint8Array(v);
    if (songBase !== FM1_SONGS[0] && off >= FM1_SONGS[0] && off < FM1_SONGS[1]) off += songBase - FM1_SONGS[0];
    if (off + 4096 > nor.length) continue;
    if (STORE_AREAS.some(([a, e]) => off >= a && off + 4096 <= e && off % 4096 === 0) && b.length === 4096)
      nor.set(b, off);
  }
}

export const HOLD_MS = 24;

// The stored songs (Midi-Mania: charts, docs/CHART_FORMAT.md, in the song region; firmware/src/player.c lists them
// after the built-in songs). The page's MIDI files, converted there (web/mm_songs.js), are written here as the FM-1
// writes those uploaded over USB (firmware/src/editor_songs.c): into the first gap of free 4 KiB sectors it fits; a
// removed one has its sectors erased, the others stay where they are. A song playing stops first (the list's order
// changes). The region is in STORE_AREAS, so the page keeps them with the other saves.
const SECTOR = 4096, MAGIC = 0x31434D4D;
const maxSongs = (ex) => (ex.fh_song_max ? ex.fh_song_max() : 16);
function region(ex) {
  return new Uint8Array(ex.memory.buffer, ex.fh_nor() + ex.fh_song_base(), ex.fh_song_size());
}
// the songs the firmware lists, in SONGS' order: {off, size, title, length (s), bpm, parts, notes, per: [notes by
// part]} (docs/CHART_FORMAT.md: the header's counts at 8 and 12, the notes after the beats, a note's flags at 11)
export function songsList(ex) {
  const r = region(ex), dv = new DataView(r.buffer, r.byteOffset, r.byteLength), out = [];
  for (let i = 0; i < ex.fh_song_count(); i++) {
    const off = ex.fh_song_off(i), nbeat = dv.getUint32(off + 8, true), nnote = dv.getUint32(off + 12, true);
    const per = [0, 0, 0, 0], n0 = off + 72 + nbeat * 4;
    let title = "";
    for (let k = 0; k < 24 && r[off + 24 + k]; k++) title += String.fromCharCode(r[off + 24 + k]);
    for (let k = 0; k < nnote && n0 + k * 12 + 12 <= r.length; k++) per[r[n0 + k * 12 + 11] & 3]++;
    out.push({ off, size: ex.fh_song_len(i), title, length: dv.getUint32(off + 16, true) / 44100,
               bpm: dv.getUint16(off + 20, true) / 10, parts: r[off + 70], notes: nnote, per });
  }
  return out;
}
export function songsUsed(ex) { return ex.fh_song_used(); }
// a chart (Uint8Array) into the first gap it fits: {ok, error}
export function songsAdd(ex, chart) {
  const r = region(ex);
  if (chart.length < 72 || new DataView(chart.buffer, chart.byteOffset, 4).getUint32(0, true) !== MAGIC)
    return { ok: false, error: "not a chart" };
  if (ex.fh_song_count() >= maxSongs(ex))
    return { ok: false, error: `no room: ${maxSongs(ex)} songs at most` };
  const at = ex.fh_song_room(chart.length);
  if (at < 0)
    return { ok: false, error: `no room: ${Math.ceil(chart.length / 1024)} KB needed, ${Math.floor((r.length - ex.fh_song_used()) / 1024)} KB free` };
  ex.fh_test_stop();
  r.fill(0xFF, at, Math.ceil((at + chart.length) / SECTOR) * SECTOR);   // (its sectors erased)
  r.set(chart, at);
  ex.fh_songs_rescan();
  return { ok: true };
}
// a copy of the chart at off (the page sends it to a real FM-1 over USB), or null
export function songsChart(ex, off) {
  const s = songsList(ex).find((x) => x.off === off);
  return s ? region(ex).slice(off, off + s.size) : null;
}
// the chart at off removed: its sectors erased
export function songsRemove(ex, off) {
  const s = songsList(ex).find((x) => x.off === off);
  if (!s) return;
  ex.fh_test_stop();
  region(ex).fill(0xFF, off, Math.ceil((off + s.size) / SECTOR) * SECTOR);
  ex.fh_songs_rescan();
}

// The panel's knobs from a MIDI controller (the page's Web MIDI in): CC 20..26 turn SELECT, ALGORITHM, PRESETS,
// KNOB 1..4 (the encoder roles), a detent a step; CC 27 sets MASTER (a pot on the FM-1: absolute in every mode). The
// firmware's CC map (midi_control.c) has none of them, so they stay here. mode, as the controller sends a turn:
//   abs  absolute (the default): by how far the value moves, so it takes over from anywhere without a jump; last:
//        the value each knob CC sent before (the first one only takes its place)
//   bin  relative, 64 +- n (65 = +1, 63 = -1)
//   twos relative, two's complement (1 = +1, 127 = -1)
//   sign relative, sign bit (1 = +1, 65 = -1)
// -> {role, n} | {master: 0..1023} | null (not one of them: on to the firmware)
export const KNOB_CC = 20, MASTER_CC = 27, KNOB_MODES = ["abs", "bin", "twos", "sign"];
export function midiKnob(last, cc, value, mode = "abs") {
  if (cc === MASTER_CC) return { master: Math.round(value * 1023 / 127) };
  const role = cc - KNOB_CC;
  if (role < 0 || role >= 7) return null;
  if (mode === "bin") return { role, n: value - 64 };
  if (mode === "twos") return { role, n: value < 64 ? value : value - 128 };
  if (mode === "sign") return { role, n: value < 64 ? value : 64 - value };
  const was = last[role];
  last[role] = value;
  return { role, n: was === undefined ? 0 : value - was };
}

if (typeof registerProcessor === "function") {
  const clock = globalThis.performance ? () => globalThis.performance.now() : () => Date.now();
  class Felucca extends AudioWorkletProcessor {
    constructor() {
      super();
      this.ex = null;
      this.sentDraws = -1;
      this.sentWrites = -1;
      this.sentLeds = "";
      this.lastFrame = 0;
      this.period = 1 / 30;
      this.busy = 0;
      this.frames = 0;
      this.queue = [];                                // button / key changes, each held >= HOLD_MS of device time
      this.nextOk = 0;
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    async onMessage(m) {
      if (m.type === "load") {
        try {
          const { instance } = await WebAssembly.instantiate(m.wasm, {});
          const ex = instance.exports;
          ex._initialize();
          ex.fh_nor_erase();
          restoreSectors(new Uint8Array(ex.memory.buffer, ex.fh_nor(), ex.fh_nor_size()), m.sectors, ex.fh_song_base());
          ex.fh_master(m.master ?? 700);
          ex.fh_boot();
          this.ex = ex;
          this.sentWrites = ex.fh_flash_writes();
          this.port.postMessage({ type: "ready", rate: ex.fh_sample_rate() });
        } catch (err) {
          this.port.postMessage({ type: "error", message: String(err && err.message || err) });
        }
        return;
      }
      const ex = this.ex;
      if (!ex) return;
      if (m.type === "buttons" || m.type === "keys") this.queue.push(m);
      else if (m.type === "enc") ex.fh_enc(m.role, m.n | 0);
      else if (m.type === "master") ex.fh_master(m.value | 0);
      else if (m.type === "midi") ex.fh_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0);
      else if (m.type === "preview") {          // a MIDI file heard before it is loaded: [ms, part, note, vel] by time
        if (ex.fh_preview_stop) ex.fh_preview_stop();
        this.preview = { ev: m.events, i: 0, t0: ex.fh_now_ms(), end: m.end | 0, id: m.id };
      }
      else if (m.type === "preview-stop") this.previewEnd(false);
      else if (m.type === "visible") this.period = m.on ? 1 / 30 : 1 / 4;
      else if (m.type === "song-get") {         // a stored chart's bytes, for the page to send to a real FM-1
        const chart = songsChart(ex, m.off | 0);
        this.port.postMessage({ type: "song-bytes", id: m.id, chart: chart && chart.buffer }, chart ? [chart.buffer] : []);
      }
      else if (m.type === "song-add" || m.type === "song-remove" || m.type === "songs") {   // the page's song list
        let res = { ok: true };
        if (m.type === "song-add") res = songsAdd(ex, new Uint8Array(m.chart));
        else if (m.type === "song-remove") songsRemove(ex, m.off | 0);
        if (m.type !== "songs") {               // (the flash changed: the page keeps it, as after a save)
          const sectors = storeSectors(new Uint8Array(ex.memory.buffer, ex.fh_nor(), ex.fh_nor_size()));
          this.port.postMessage({ type: "frame", sectors });
        }
        this.port.postMessage({ type: "songs", list: songsList(ex), used: songsUsed(ex), size: ex.fh_song_size(),
                                error: res.ok ? "" : res.error, name: m.name || "" });
      }
    }

    publish() {
      const ex = this.ex, mem = ex.memory.buffer;
      const msg = { type: "frame" };
      const leds = [ex.fh_lit_buttons(), ex.fh_lit_keys(), ex.fh_dim_buttons(), ex.fh_dim_keys(), ex.fh_dim_level(),
                    ex.fh_breath_buttons(), ex.fh_breath_keys(), ex.fh_mid_keys ? ex.fh_mid_keys() : 0];
      const an = ex.fh_anim_levels ? ex.fh_anim_levels() : 0;   // the power-on sweep: each LED's level
      const anim = an ? Array.from(new Uint8Array(mem, an, 41)) : null;
      const key = leds.join(",") + (anim ? ";" + anim.join(",") : "");
      if (key !== this.sentLeds) {
        this.sentLeds = key;
        msg.leds = leds;
        msg.anim = anim;
      }
      if (this.frames >= 44100) {                     // the share of real time spent running the device
        msg.load = this.busy / (this.frames / 44.1);
        this.busy = this.frames = 0;
      }
      const transfer = [];
      const draws = ex.fh_screen_draws();
      if (draws !== this.sentDraws) {
        this.sentDraws = draws;
        msg.fb = new Uint16Array(mem, ex.fh_screen(), 240 * 240).slice();
        transfer.push(msg.fb.buffer);
      }
      const writes = ex.fh_flash_writes();
      if (writes !== this.sentWrites) {               // a save is done within one main-loop pass: whole here
        this.sentWrites = writes;
        msg.sectors = storeSectors(new Uint8Array(mem, ex.fh_nor(), ex.fh_nor_size()));
      }
      if (this.preview) msg.preview = { id: this.preview.id, at: this.preview.at || 0, end: this.preview.end };
      if (msg.leds || msg.fb || msg.sectors || msg.load !== undefined || msg.preview) this.port.postMessage(msg, transfer);
    }

    // A click's down and up can arrive within one render quantum, but the FM-1's contacts never close for less than
    // its debounce, and the UI reads them once a frame: each change of the buttons or the keys stays HOLD_MS of the
    // device's time before the next one goes in (a tap: down for a frame and more, then up).
    input() {
      const ex = this.ex, now = ex.fh_now_ms();
      while (this.queue.length && now - this.nextOk >= 0) {
        const m = this.queue.shift();
        if (m.type === "buttons") ex.fh_buttons(m.mask >>> 0);
        else ex.fh_keys(m.mask >>> 0);
        this.nextOk = now + HOLD_MS;
      }
    }

    // the preview's notes due by the device's clock now (a render block, ~3 ms, is the finest step), its position for
    // the page once a frame; at its end (or stopped) every note let go and the page told
    previewStep() {
      const p = this.preview, ex = this.ex, at = ex.fh_now_ms() - p.t0;
      while (p.i < p.ev.length && p.ev[p.i][0] <= at) {
        const [, part, note, vel] = p.ev[p.i++];
        ex.fh_preview_note(part, note, vel);
      }
      p.at = at;
      if (p.i >= p.ev.length && at >= p.end) this.previewEnd(true);
    }
    previewEnd(done) {
      if (!this.preview) return;
      const id = this.preview.id;
      this.preview = null;
      if (this.ex && this.ex.fh_preview_stop) this.ex.fh_preview_stop();
      this.port.postMessage({ type: "preview-end", id, done });
    }

    process(inputs, outputs) {
      const ex = this.ex;
      if (!ex) return true;
      if (this.queue.length) this.input();
      if (this.preview && ex.fh_preview_note) this.previewStep();
      const out = outputs[0], n = out[0].length;
      const t0 = clock();
      ex.fh_render(n);
      this.busy += clock() - t0;
      this.frames += n;
      out[0].set(new Float32Array(ex.memory.buffer, ex.fh_out_l(), n));
      if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.fh_out_r(), n));
      if (currentTime - this.lastFrame >= this.period) {
        this.lastFrame = currentTime;
        this.publish();
      }
      return true;
    }
  }
  registerProcessor("midi-mania", Felucca);
}
