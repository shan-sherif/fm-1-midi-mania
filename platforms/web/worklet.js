// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Felucca in an AudioWorklet: midi-mania.wasm is the whole FM-1 firmware against a simulated FM-1
// (platforms/host/mm_host.c). Each render quantum asks it for 128 frames, which runs the device's clock forward;
// between quanta the page's input goes in and, about 30 times a second (4 when the page is hidden), the screen,
// the lights and any changed flash go out. After X0X's platforms/web/worklet.js (charlesvestal/fm1-x0x, GPL-3.0).

// The flash the page keeps: storage.c's areas (projects, user presets and their FM6 patches, the user sample
// slots 0x97000..0xDFFFF; the autosave 0xE5000..0xE6FFF (1.2) and FM6's voice bank 0xE7000 (1.4.1); the settings and
// the second FM6 copy 0xFC000..0xFFFFF), only the sectors that hold something (an erased sector is all 0xFF). {offset: bytes}
export const STORE_AREAS = [[0x97000, 0xE0000], [0xE5000, 0xE8000], [0xFC000, 0x100000]];
export function storeSectors(nor) {
  const out = {};
  for (const [a, b] of STORE_AREAS)
    for (let off = a; off < b; off += 4096) {
      const s = nor.subarray(off, off + 4096);
      if (s.some((v) => v !== 0xFF)) out[off] = s.slice();
    }
  return out;
}
export function restoreSectors(nor, sectors) {
  for (const [k, v] of Object.entries(sectors || {})) {
    const off = +k, b = v instanceof Uint8Array ? v : new Uint8Array(v);
    if (STORE_AREAS.some(([a, e]) => off >= a && off + 4096 <= e && off % 4096 === 0) && b.length === 4096)
      nor.set(b, off);
  }
}

export const HOLD_MS = 24;

// The stored songs (Midi-Mania: charts, docs/CHART_FORMAT.md, in the song region; firmware/src/player.c lists them
// after the built-in songs). The page's MIDI files, converted there (web/mm_songs.js), are written here as the FM-1
// will write them: from the first free 4 KiB sector; a removed one is taken out and the rest packed again. The region
// is in STORE_AREAS, so the page keeps them with the other saves.
const SECTOR = 4096, MAGIC = 0x31434D4D;
function region(ex) {
  return new Uint8Array(ex.memory.buffer, ex.fh_nor() + ex.fh_song_base(), ex.fh_song_size());
}
// the charts there, in order: {off, size, title, live, length (s), bpm, parts}
export function songsList(ex) {
  const r = region(ex), dv = new DataView(r.buffer, r.byteOffset, r.byteLength), out = [];
  for (let off = 0; off + 72 <= r.length; ) {
    if (dv.getUint32(off, true) !== MAGIC) break;
    const head = dv.getUint16(off + 6, true), size = head + dv.getUint32(off + 8, true) * 4 + dv.getUint32(off + 12, true) * 12;
    if (head < 72 || off + size > r.length) break;
    let title = "";
    for (let i = 0; i < 24 && r[off + 24 + i]; i++) title += String.fromCharCode(r[off + 24 + i]);
    out.push({ off, size, title, live: r[off + 71] === 0xFF, length: dv.getUint32(off + 16, true) / 44100,
               bpm: dv.getUint16(off + 20, true) / 10, parts: r[off + 70] });
    off = Math.ceil((off + size) / SECTOR) * SECTOR;
  }
  return out;
}
export function songsUsed(ex) { return ex.fh_song_free(); }
// a chart (Uint8Array) after the others: {ok, error}. The song playing keeps playing (the others do not move)
export function songsAdd(ex, chart) {
  const r = region(ex), at = ex.fh_song_free();
  if (chart.length < 72 || new DataView(chart.buffer, chart.byteOffset, 4).getUint32(0, true) !== MAGIC)
    return { ok: false, error: "not a chart" };
  if (at + chart.length > r.length)
    return { ok: false, error: `no room: ${Math.ceil(chart.length / 1024)} KB needed, ${Math.floor((r.length - at) / 1024)} KB free` };
  r.fill(0xFF, at, Math.min(r.length, Math.ceil((at + chart.length) / SECTOR) * SECTOR));   // (its sectors erased)
  r.set(chart, at);
  ex.fh_songs_rescan();
  return { ok: true };
}
// the chart at off taken out, the rest packed from the region's start (a song playing stops: songs move)
export function songsRemove(ex, off) {
  const keep = songsList(ex).filter((s) => s.live && s.off !== off).map((s) => region(ex).slice(s.off, s.off + s.size));
  ex.fh_test_stop();
  region(ex).fill(0xFF);
  ex.fh_songs_rescan();
  for (const c of keep) songsAdd(ex, c);
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
          restoreSectors(new Uint8Array(ex.memory.buffer, ex.fh_nor(), ex.fh_nor_size()), m.sectors);
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
      else if (m.type === "visible") this.period = m.on ? 1 / 30 : 1 / 4;
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
      if (msg.leds || msg.fb || msg.sectors || msg.load !== undefined) this.port.postMessage(msg, transfer);
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

    process(inputs, outputs) {
      const ex = this.ex;
      if (!ex) return true;
      if (this.queue.length) this.input();
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
