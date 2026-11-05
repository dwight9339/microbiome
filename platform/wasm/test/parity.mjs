// Pixel parity: the wasm build must render exactly what the native mbsnap does.
//   node platform/wasm/test/parity.mjs build-wasm/web build/mbsnap
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { inflateSync } from "node:zlib";

const web = resolve(process.argv[2] ?? "build-wasm/web");
const mbsnap = resolve(process.argv[3] ?? "build/mbsnap");
const { loadEngine } = await import(pathToFileURL(join(web, "microbiome.js")));
const engine = await loadEngine(readFileSync(join(web, "microbiome.wasm")));
const packPath = join(web, "forest.mbp");
const pack = readFileSync(packPath);
const tmp = mkdtempSync(join(tmpdir(), "mb-parity-"));

// Decode the 8-bit RGB, filter-0 PNGs that mbsnap writes.
function readPng(path) {
  const buf = readFileSync(path);
  let off = 8, w = 0, h = 0;
  const idat = [];
  while (off < buf.length) {
    const len = buf.readUInt32BE(off);
    const type = buf.toString("latin1", off + 4, off + 8);
    const data = buf.subarray(off + 8, off + 8 + len);
    if (type === "IHDR") { w = data.readUInt32BE(0); h = data.readUInt32BE(4); }
    if (type === "IDAT") idat.push(data);
    off += 12 + len;
  }
  const raw = inflateSync(Buffer.concat(idat));
  const rgb = Buffer.alloc(w * h * 3);
  for (let y = 0; y < h; y++) {
    assert.equal(raw[y * (w * 3 + 1)], 0, "mbsnap writes filter 0");
    raw.copy(rgb, y * w * 3, y * (w * 3 + 1) + 1, (y + 1) * (w * 3 + 1));
  }
  return { w, h, rgb };
}

// Mirrors mbsnap's main(): init without warm-up, set time and pins, warm up
// through the core at the given speed, then step sim seconds in 100 ms updates.
function wasmShot({ size = [320, 240], scale = 0, time, pins, seed = 1, warmup = 90, sim = 0, speed = 1, center, lcd565 = false }) {
  engine.load(pack, { width: size[0], height: size[1], scale, seed, warmup: 0, time, pins, lcd565 });
  engine.setTimeScale(speed);
  engine.warmup(warmup);
  for (let t = 0; t < sim * 10; t++) engine.update(100);
  if (center) engine.centerCamera(...center);
  return engine.pixels();
}

const cases = [
  { name: "default", args: [], opts: {} },
  { name: "dawn", args: ["--time", "06:30"], opts: { time: "06:30" } },
  { name: "night snow", args: ["--time", "22:00", "--set", "weather=precip", "--set", "season=winter"],
    opts: { time: "22:00", pins: { weather: "precip", season: "winter" } } },
  { name: "summer night fireflies", args: ["--time", "21:00", "--set", "season=summer", "--set", "weather=clear", "--sim", "120"],
    opts: { time: "21:00", pins: { season: "summer", weather: "clear" }, sim: 120 } },
  { name: "480x480, seed 7, fast", args: ["--size", "480x480", "--seed", "7", "--speed", "60", "--sim", "300"],
    opts: { size: [480, 480], seed: 7, speed: 60, sim: 300 } },
  { name: "centred", args: ["--center", "760,250", "--time", "10:00"], opts: { center: [760, 250], time: "10:00" } },
  { name: "RGB565", args: ["--rgb565", "--time", "18:45"], opts: { lcd565: true, time: "18:45" } },
  { name: "panorama", args: ["--panorama", "--time", "07:00"], opts: { size: [1280, 300], scale: 1, time: "07:00" } },
];

let failed = 0;
for (const c of cases) {
  const out = join(tmp, "shot.png");
  execFileSync(mbsnap, [packPath, out, ...c.args], { stdio: "pipe" });
  const ref = readPng(out);
  const px = wasmShot(c.opts);
  assert.equal(px.length, ref.w * ref.h * 4, `${c.name}: size`);
  let diff = 0;
  for (let i = 0, j = 0; j < ref.rgb.length; i += 4, j += 3)
    if (px[i] !== ref.rgb[j] || px[i + 1] !== ref.rgb[j + 1] || px[i + 2] !== ref.rgb[j + 2]) diff++;
  console.log(`${diff ? "FAIL" : "ok"} - ${c.name} (${ref.w}x${ref.h})${diff ? `: ${diff} pixels differ` : ""}`);
  if (diff) failed++;
}
rmSync(tmp, { recursive: true, force: true });
if (failed) {
  console.error(`wasm parity: ${failed} of ${cases.length} cases differ from mbsnap`);
  process.exit(1);
}
console.log("wasm parity: identical to mbsnap");
