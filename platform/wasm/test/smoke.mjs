// Smoke test for the wasm host and its JS wrapper:
//   node platform/wasm/test/smoke.mjs build-wasm/web
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

const web = resolve(process.argv[2] ?? "build-wasm/web");
const { loadEngine, MicrobiomeError, Input, formatClock } = await import(pathToFileURL(join(web, "microbiome.js")));
const engine = await loadEngine(readFileSync(join(web, "microbiome.wasm")));
const pack = readFileSync(join(web, "forest.mbp"));

function check(name, fn) {
  fn();
  console.log(`ok - ${name}`);
}

check("load + pack info", () => {
  const info = engine.load(pack);
  assert.equal(info.name, "Forest");
  assert.deepEqual(info.world, [1280, 300]);
  assert.deepEqual(info.display, [320, 240]);
  assert.deepEqual(info.view, [320, 240]);
  assert.equal(info.scale, 1);
  assert.deepEqual(info.vars.slice(0, 4).map((v) => v.name), ["minute", "hour", "month", "day"]);
  const phase = info.vars.find((v) => v.name === "phase");
  assert.deepEqual(phase.labels, ["night", "dawn", "day", "dusk"]);
  assert.deepEqual(info.spawns.map((s) => s.actor), ["deer", "rabbit", "fox", "owl", "songbird"]);
  assert.ok(info.actors.find((a) => a.name === "deer").states.includes("graze"));
  assert.equal(info.layers.length, 5);
  assert.ok(info.places.some((p) => p.name === "burrow"));
  assert.equal(info.obstacles[0].name, "pond");
});

check("render", () => {
  const px = engine.pixels();
  assert.equal(px.length, 320 * 240 * 4);
  const colours = new Set();
  for (let i = 0; i < px.length; i += 4) {
    assert.equal(px[i + 3], 255, "opaque");
    colours.add((px[i] << 16) | (px[i + 1] << 8) | px[i + 2]);
  }
  assert.ok(colours.size > 10, `a real picture (${colours.size} colours)`);
});

check("state + spawn eligibility", () => {
  engine.load(pack, { time: "12:00", pins: { weather: "clear" } });
  for (let i = 0; i < 6000; i++) engine.update(100);
  const s = engine.state();
  const vars = Object.fromEntries(engine.info.vars.map((v, i) => [v.name, s.vars[i]]));
  assert.equal(vars.phase.label, "day");
  assert.equal(vars.weather.label, "clear");
  assert.equal(vars.weather.pinned, true);
  assert.equal(vars.phase.pinned, false);
  assert.ok(s.actors.length > 0, "animals by day");
  for (const a of s.actors) {
    assert.equal(engine.info.actors[a.type].name, a.name);
    assert.equal(engine.info.actors[a.type].states[a.stateIndex], a.state);
  }
  const owl = s.spawns[engine.info.spawns.findIndex((r) => r.actor === "owl")];
  assert.ok(owl.blocked.includes("when"));
  assert.equal(owl.failVar, "phase");
  assert.ok(s.nextSpawnMs >= 0 && s.nextSpawnMs <= 4000);
});

check("time, date, pins, camera", () => {
  engine.setTime("23:30");
  engine.setDate(7, 4);
  engine.pin("weather", "precip");
  engine.update(0);
  let s = engine.state();
  assert.equal(formatClock(s.minute), "23:30");
  assert.deepEqual(s.date.slice(1), [7, 4]);
  const label = (n) => s.vars[engine.info.vars.findIndex((v) => v.name === n)].label;
  assert.equal(label("phase"), "night");
  assert.equal(label("season"), "summer");
  assert.equal(label("weather"), "precip");
  engine.unpin("weather");
  engine.centerCamera(0, 0);
  s = engine.state();
  assert.deepEqual(s.camera.slice(0, 2), [0, 0]);
  engine.update(100, Input.RIGHT);
  assert.ok(engine.state().camera[0] > 0, "panning moves the camera");
  assert.throws(() => engine.pin("nope", 1), MicrobiomeError);
  assert.throws(() => engine.pin("weather", "sunny"), MicrobiomeError);
});

check("hot reload keeps the view", () => {
  engine.load(pack);
  engine.setTime("21:30");
  engine.pin("weather", "clear");
  engine.centerCamera(1000, 200);
  engine.setTimeScale(60);
  const before = engine.state();
  engine.load(pack, { keepView: true });
  const after = engine.state();
  assert.ok(after.minute >= 21 * 60 + 30 && after.minute <= 21 * 60 + 32, formatClock(after.minute));
  assert.deepEqual(after.camera, before.camera);
  assert.equal(after.timeScale, 60);
  const w = engine.info.vars.findIndex((v) => v.name === "weather");
  assert.equal(after.vars[w].label, "clear");
  assert.equal(after.vars[w].pinned, true);
});

check("display sizes, panorama, RGB565", () => {
  let info = engine.load(pack, { width: 480, height: 480 });
  assert.equal(info.scale, 2);
  assert.deepEqual(info.view, [240, 240]);
  assert.equal(engine.pixels().length, 480 * 480 * 4);
  info = engine.load(pack, { width: 1280, height: 300, scale: 1, warmup: 0 });
  assert.deepEqual(info.view, [1280, 300]);
  engine.load(pack, { lcd565: true });
  const px = engine.pixels();
  for (let i = 0; i < px.length; i += 4) {
    assert.equal(px[i] & 7, 0);
    assert.equal(px[i + 1] & 3, 0);
    assert.equal(px[i + 2] & 7, 0);
  }
});

check("bad packs are rejected, not crashed on", () => {
  const bad = Uint8Array.from(pack);
  bad[0] ^= 0xff;
  assert.throws(() => engine.load(bad), (e) => e instanceof MicrobiomeError && /magic/i.test(e.message));
  assert.equal(engine.loaded, false);
  assert.throws(() => engine.load(pack.subarray(0, 1000)), MicrobiomeError);
  assert.throws(() => engine.pixels(), MicrobiomeError);
  engine.load(pack); // and recovers
  assert.equal(engine.pixels().length, 320 * 240 * 4);
});

console.log("wasm smoke: all passed");
