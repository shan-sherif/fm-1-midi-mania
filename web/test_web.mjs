// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; 2026 fm-1-midi-mania contributors
//
// Node checks of the installer's JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - fm1pkg.js: productOf and logicalImage on build/midi-mania.fwsc (skipped without a build)
// - fm1ota.js: a full install and an unplug during the write against a simulated FM-1
// (Felucca's sound editor, web/editor.html, and its checks are not part of Midi-Mania.)

import { execFileSync } from "node:child_process";
import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { logicalImage, productOf } from "./fm1pkg.js";
import { Updater, pack7, unpack7 } from "./fm1ota.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(64)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const py = (code, ...args) => execFileSync("python3", ["-c", code, ...args], { maxBuffer: 1 << 26 });
const HERE = new URL(".", import.meta.url).pathname;
/* MM_ROOT: the tree with a ./build.sh build (the package check), overridable */
const FW_ROOT = process.env.MM_ROOT || join(HERE, "..");

/* ------------------------------------------------------- packages: JS == Python --- */
async function packages() {
  const pkg = join(FW_ROOT, "build/midi-mania.fwsc");
  if (!existsSync(pkg)) {
    console.log("packages: skipped (run ./build.sh first)");
    return;
  }
  const raw = readFileSync(pkg);
  const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, pkg);
  ok(eq(logicalImage(raw), logical), "fm1pkg.js logicalImage");
  ok(/^FM-1_137(\d{3})?$/.test(productOf(raw)), "fm1pkg.js productOf");
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity, unknownChip = false, chip = 0x856014, identity = "FM-1_015", refuse = false } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.unknownChip = unknownChip; this.chip = chip;
    this.home = identity; this.refuse = refuse;
    this.served = 0; this.bad = 0;
    this.written = 0;                                  // (the loader's reads of the image: what it would write)
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot(identity, "FM-1");
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (d.length > 2 && unpack7(d.slice(1, -1))[2] === 0x4B) {
      // the installer's check only, to the loader: the update record cleared, back to the firmware that was there
      if (this.identity.startsWith("ota-")) { this.cancelled = true; setTimeout(() => this.boot("FM-1_015", "FM-1"), 300); }
    } else if (d.length > 2 && unpack7(d.slice(1, -1))[2] === 0x4A) {
      // "which flash chip": Midi-Mania (its app, its loader) answers; the official firmware (FM-1_015) does not
      if (this.identity !== "FM-1_015") this.tx([0xF0, ...pack7([0, 0x59, 0x4A, 0, 0, 0, 0x4A, this.chip >> 16, (this.chip >> 8) & 0xFF, this.chip & 0xFF, 0]), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : this.refuse ? [[0, 64], [0x40, 160]]               // (another firmware: reads the head, then refuses: stops)
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr < 0xE0000000 && this.identity.startsWith("ota-")) this.written++;
      // (a flash chip it does not know: Midi-Mania's loader restarts into the firmware that was there, writing nothing)
      if (addr === 0xE0000000) setTimeout(() => this.unknownChip ? this.boot("FM-1_015", "FM-1") : this.boot("ota-FM-1_800", "FM-1 Midi-Mania Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot("FM-1_800", "Midi-Mania"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

async function updater() {
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, "FM-1_800", (k) => steps.push(k));
  ok(got === "FM-1_800" && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: install: running firmware -> loader -> Midi-Mania (${dev.served} reads)`);

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot("ota-FM-1_800", "FM-1 Midi-Mania Update");
  const t0 = Date.now();
  const done = await new Updater(dev2.access).resume(image);
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write -> stops at once");

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, "FM-1_800").then(() => null, (x) => x);
  ok(e && e.code === "lost", "fm1ota.js: unplugged in step 1 -> error code 'lost'");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, "FM-1_800").then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");

  const dev4 = new FakeFM1(image, { unknownChip: true });
  const t4 = Date.now();
  const e4 = await new Updater(dev4.access).install(image, "FM-1_800").then(() => null, (x) => x);
  ok(e4 && e4.code === "unknownchip" && dev4.written === 0 && dev4.identity === "FM-1_015" && Date.now() - t4 < 25000,
    `fm1ota.js: an unknown flash chip (back to its own firmware, no loader) -> 'unknownchip', nothing written (${((Date.now() - t4) / 1000).toFixed(0)} s)`);

  // the flash chip: asked of Midi-Mania (its app, its loader); the official firmware does not say (null)
  const dev5 = new FakeFM1(image, { chip: 0xB36014 });
  const c0 = await new Updater(dev5.access).chip();
  dev5.boot("FM-1_800", "Midi-Mania");
  const c1 = await new Updater(dev5.access).chip();
  const chips = [];
  const dev6 = new FakeFM1(image, { chip: 0xB36014 });
  await new Updater(dev6.access).install(image, "FM-1_800", (k, a) => { if (k === "chip") chips.push(a); });
  ok(c0 && c0.chip === null && c0.text === "FM-1_015" && c1 && c1.chip === 0xB36014 && c1.text === "FM-1_800" &&
     chips.length === 2 && chips[0] === null && chips[1] === 0xB36014,
    "fm1ota.js: the flash chip: Midi-Mania (app, loader) says 0xB36014, the official firmware nothing");

  // the check only, from the official firmware: step 1, the loader asked its chip and cancelled, back to the official
  // firmware, Midi-Mania never written
  const dev7 = new FakeFM1(image, { chip: 0xB36014 });
  const seen = [];
  const c7 = await new Updater(dev7.access).probe(image, "FM-1_800", (k) => seen.push(k));
  ok(c7 === 0xB36014 && dev7.cancelled && dev7.written === 0 && dev7.identity === "FM-1_015" && seen.includes("cancel") && seen.at(-1) === "back",
    "fm1ota.js: check only: the loader says 0xB36014, cancels, back to the official firmware, nothing written");
  const dev8 = new FakeFM1(image, { unknownChip: true });
  const e8 = await new Updater(dev8.access).probe(image, "FM-1_800").then(() => null, (x) => x);
  ok(e8 && e8.code === "unknownchip" && dev8.written === 0 && !dev8.cancelled,
    "fm1ota.js: check only, an unknown chip: 'unknownchip', nothing written");

  // another custom firmware that takes only its own or the official update (SLOOP): it refuses in step 1
  const dev9 = new FakeFM1(image, { identity: "FM-1_928", refuse: true });
  const e9 = await new Updater(dev9.access).install(image, "FM-1_137").then(() => null, (x) => x);
  ok(e9 && e9.code === "foreign" && e9.detail === "FM-1_928" && dev9.written === 0 && dev9.identity === "FM-1_928",
    "fm1ota.js: another custom firmware refusing the package -> 'foreign' (go back to V15 first), nothing written");
}

await packages();
await updater();
console.log(failed ? `WEB TESTS FAILED (${failed})` : "web tests passed");
process.exit(failed ? 1 : 0);
