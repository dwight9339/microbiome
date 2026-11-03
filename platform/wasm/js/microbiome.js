// microbiome.js - the engine core (microbiome.wasm) as an ES module.
//
//   import { loadEngine, Input } from "./microbiome.js";
//   const engine = await loadEngine(new URL("./microbiome.wasm", import.meta.url));
//   engine.load(await (await fetch("forest.mbp")).arrayBuffer(), { width: 320, height: 240 });
//   function frame(dt) {
//     engine.update(dt, Input.LEFT);
//     engine.draw(ctx);                       // or engine.pixels() for raw RGBA
//   }
//
// Works in browsers and Node >= 18 (pass the .wasm bytes in Node). No dependencies.
// Types: microbiome.d.ts.

export const Input = Object.freeze({ LEFT: 1, RIGHT: 2, UP: 4, DOWN: 8, A: 16, B: 32 });

export class MicrobiomeError extends Error {
  constructor(code, message) {
    super(message);
    this.name = "MicrobiomeError";
    this.code = code;
  }
}

/** Instantiate the engine module. `source` is a URL/string to fetch, a Response,
 *  the module bytes (ArrayBuffer / typed array), or a compiled WebAssembly.Module. */
export async function loadEngine(source) {
  let module;
  if (source instanceof WebAssembly.Module) {
    module = source;
  } else if (source instanceof ArrayBuffer || ArrayBuffer.isView(source)) {
    module = await WebAssembly.compile(source);
  } else {
    const res = source instanceof Response ? source : await fetch(source);
    if (!res.ok) throw new MicrobiomeError(-1, `cannot fetch ${res.url}: ${res.status}`);
    module = await WebAssembly.compile(await res.arrayBuffer());
  }
  const instance = await WebAssembly.instantiate(module, {});
  return new Engine(instance.exports);
}

const decoder = new TextDecoder();
const encoder = new TextEncoder();

export class Engine {
  #x;
  #info = null;
  #opts = null;

  constructor(exports) {
    this.#x = exports;
  }

  /** Fixed facts about the loaded pack and display (names, enum labels,
   *  layout). Null until a pack is loaded. */
  get info() {
    return this.#info;
  }

  get loaded() {
    return this.#info !== null;
  }

  /**
   * Load (or reload) a compiled pack. Throws MicrobiomeError if the pack is
   * rejected; the previous pack, if any, is no longer usable after that.
   *
   * Options: width, height (display px, default 320x240), scale (0 = auto),
   * seed, lcd565 (show RGB565 colours), warmup (seconds simulated so the scene
   * isn't empty, default 90), and time / date / pins, applied before the
   * warm-up so its population matches them (as mbsnap does).
   *
   * keepView: camera, time of day, date, time scale and pinned variables
   * (matched by name) carry over from the previous pack, the way mbsim
   * hot-reloads. Display options default to the previous load's.
   */
  load(pack, options = {}) {
    const prev = options.keepView && this.loaded ? this.#snapshot() : null;
    const { time, date, pins, keepView, ...display } = options;
    const opts = {
      width: 320, height: 240, scale: 0, seed: 1, warmup: 90, lcd565: false,
      ...(prev ? this.#opts : {}),
      ...display,
    };
    const bytes = pack instanceof Uint8Array ? pack
      : ArrayBuffer.isView(pack) ? new Uint8Array(pack.buffer, pack.byteOffset, pack.byteLength)
      : new Uint8Array(pack);
    this.#info = null;
    const ptr = this.#x.mbw_pack_buffer(bytes.length);
    if (!ptr) throw new MicrobiomeError(101, "out of memory (or empty pack)");
    new Uint8Array(this.#x.memory.buffer, ptr, bytes.length).set(bytes);
    const err = this.#x.mbw_init(opts.width, opts.height, opts.scale, opts.seed >>> 0, 0, opts.lcd565 ? 1 : 0);
    if (err) throw new MicrobiomeError(err, this.#cstr(this.#x.mbw_err_str(err)));
    try {
      this.#info = this.#json(this.#x.mbw_pack_json());
    } catch (e) {
      this.#x.mbw_unload(); // never leave an engine running without its info
      throw e;
    }
    this.#opts = opts;

    if (prev) {
      this.setDate(prev.date[1], prev.date[2], prev.date[0]);
      this.setTime(prev.minute);
      for (const p of prev.pins) {
        try {
          this.pin(p.name, p.label);
        } catch {
          // that variable or label is gone from the new pack
        }
      }
    }
    if (date) this.setDate(...date);
    if (time !== undefined) this.setTime(time);
    for (const [name, value] of Object.entries(pins ?? {})) this.pin(name, value);

    // Warm up without letting the camera autopilot wander off its start.
    const cam = this.state().camera;
    for (let t = 0; t < opts.warmup * 10; t++) this.update(100);
    const center = prev ? prev.center : [cam[0] + (cam[2] >> 1), cam[1] + (cam[3] >> 1)];
    this.centerCamera(center[0], center[1]);
    if (prev) this.setTimeScale(prev.timeScale);
    return this.#info;
  }

  /** Advance the simulation. dt is clamped to 250 ms by the engine. */
  update(dtMs, input = 0) {
    this.#x.mbw_update(Math.max(0, Math.round(dtMs)) >>> 0, input >>> 0);
  }

  /** Render a frame and return its RGBA pixels (display width x height).
   *  The array is a view into wasm memory: copy it if you keep it past the
   *  next engine call. */
  pixels() {
    const ptr = this.#x.mbw_render();
    if (!ptr) throw new MicrobiomeError(100, "no pack loaded");
    const [w, h] = this.#info.display;
    return new Uint8ClampedArray(this.#x.memory.buffer, ptr, w * h * 4);
  }

  /** Render into a 2D canvas context (browser). */
  draw(ctx, x = 0, y = 0) {
    const [w, h] = this.#info.display;
    ctx.putImageData(new ImageData(new Uint8ClampedArray(this.pixels()), w, h), x, y);
  }

  /** Everything that changes per frame: clock, camera, variables, actors,
   *  spawn-rule eligibility. Arrays are indexed like the matching `info` arrays. */
  state() {
    return this.#json(this.#x.mbw_state_json());
  }

  /** Time of day as a minute (0..1439) or "HH:MM". Switches to the simulated clock. */
  setTime(t) {
    this.#x.mbw_set_minute(typeof t === "string" ? parseClock(t) : t >>> 0);
  }

  setDate(month, day, year = 2026) {
    this.#x.mbw_set_date(year, month, day);
  }

  setTimeScale(scale) {
    this.#x.mbw_set_time_scale(Math.max(1, scale >>> 0));
  }

  /** Pin a world variable to an enum label or a number (like mbsnap --set). */
  pin(name, value) {
    const idx = this.#varIndex(name);
    let v = value;
    if (typeof value === "string") {
      v = this.#info.vars[idx].labels.indexOf(value);
      if (v < 0) {
        v = Number(value);
        if (!Number.isInteger(v)) throw new MicrobiomeError(-1, `${name} has no value '${value}'`);
      }
    }
    this.#x.mbw_var_pin(idx, v | 0);
  }

  unpin(name) {
    this.#x.mbw_var_unpin(this.#varIndex(name));
  }

  /** Centre the camera on world (x, y). */
  centerCamera(x, y) {
    this.#x.mbw_camera_center(Math.round(x), Math.round(y));
  }

  /** World px -> display px for the given state's camera. */
  worldToDisplay(state, wx, wy) {
    const s = this.#info.scale;
    return [(wx - state.camera[0]) * s, (wy - state.camera[1]) * s];
  }

  /** Display px -> world px. */
  displayToWorld(state, dx, dy) {
    const s = this.#info.scale;
    return [Math.floor(dx / s) + state.camera[0], Math.floor(dy / s) + state.camera[1]];
  }

  #varIndex(name) {
    const idx = this.#info ? this.#info.vars.findIndex((v) => v.name === name) : -1;
    if (idx < 0) throw new MicrobiomeError(-1, `no variable '${name}'`);
    return idx;
  }

  // The host returns 0 when it can't grow memory to hold the document.
  #json(ptr) {
    if (!ptr) throw new MicrobiomeError(101, "out of memory writing engine JSON");
    return JSON.parse(this.#cstr(ptr));
  }

  #cstr(ptr) {
    const mem = new Uint8Array(this.#x.memory.buffer);
    let end = ptr;
    while (mem[end]) end++;
    return decoder.decode(mem.subarray(ptr, end));
  }

  #snapshot() {
    const s = this.state();
    return {
      center: [s.camera[0] + (s.camera[2] >> 1), s.camera[1] + (s.camera[3] >> 1)],
      minute: s.minute,
      date: s.date,
      timeScale: s.timeScale,
      pins: this.#info.vars
        .map((v, i) => ({ name: v.name, pinned: s.vars[i].pinned, label: s.vars[i].label }))
        .filter((p) => p.pinned),
    };
  }
}

export function parseClock(s) {
  const m = /^\s*(\d{1,2}):(\d{2})\s*$/.exec(s);
  if (!m || +m[1] > 23 || +m[2] > 59) throw new MicrobiomeError(-1, `bad time '${s}' (use HH:MM)`);
  return +m[1] * 60 + +m[2];
}

export function formatClock(minute) {
  return `${String(Math.floor(minute / 60)).padStart(2, "0")}:${String(minute % 60).padStart(2, "0")}`;
}
