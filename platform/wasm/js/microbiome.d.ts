// Types for microbiome.js. Kept by hand; mirror any change to the JSON written
// by platform/wasm/mbw.c (mbw_pack_json / mbw_state_json).

export declare const Input: Readonly<{ LEFT: 1; RIGHT: 2; UP: 4; DOWN: 8; A: 16; B: 32 }>;

export declare class MicrobiomeError extends Error {
  /** mb_err_t value, 100+ for host errors, -1 for wrapper errors. */
  readonly code: number;
}

export interface LoadOptions {
  /** Display size in physical pixels. Default 320x240. */
  width?: number;
  height?: number;
  /** Integer pixel scale; 0 (default) picks the smallest that fits the world. */
  scale?: number;
  seed?: number;
  /** Seconds simulated after loading so the scene isn't empty. Default 90. */
  warmup?: number;
  /** Render through RGB565, as the LCD shows it. */
  lcd565?: boolean;
  /** Applied before the warm-up. Minute of day or "HH:MM". */
  time?: number | string;
  /** [month, day] or [month, day, year]. */
  date?: [number, number] | [number, number, number];
  /** Variable name -> enum label or number. */
  pins?: Record<string, string | number>;
  /** Carry camera, clock, time scale and pins over from the previous pack. */
  keepView?: boolean;
}

export interface PackInfo {
  name: string;
  packVersion: number;
  packSize: number;
  world: [number, number];
  display: [number, number];
  /** Viewport in world px: display / scale. */
  view: [number, number];
  scale: number;
  lcd565: boolean;
  /** Built-ins (minute, hour, month, day) first, then the pack's, in order. */
  vars: { name: string; labels: string[] }[];
  actors: { name: string; states: string[] }[];
  /** In pack.yaml order. */
  spawns: { actor: string }[];
  layers: { name: string; size: [number, number]; front: boolean }[];
  lanes: { name: string; y: [number, number] }[];
  places: { name: string; at: [number, number]; spread: number }[];
  /** lane indexes `lanes`; box is [x0, y0, x1, y1] in world px. */
  obstacles: { name: string; lane: number; box: [number, number, number, number] }[];
  /** Scenery actors pass behind. Actors with feet y < base are hidden under
   *  the mask, whose bounds are box [x0, y0, x1, y1]. laneMask has a bit per
   *  index into `lanes` (0 = every lane). */
  occluders: { name: string; base: number; laneMask: number; box: [number, number, number, number] }[];
}

export interface ActorState {
  id: number;
  /** Index into PackInfo.actors. */
  type: number;
  name: string;
  state: string;
  stateIndex: number;
  /** Index into PackInfo.spawns. */
  spawnRule: number;
  /** Feet, world px. */
  x: number;
  y: number;
  target: [number, number];
  /** Sprite rectangle in display px: [x, y, w, h]. */
  rect: [number, number, number, number];
  stateMs: number;
  /** 0 = until the movement completes. */
  stateDurMs: number;
  ageMs: number;
  leaving: boolean;
  faceLeft: boolean;
}

export type SpawnBlock = "when" | "max" | "cooldown" | "max_actors";

export interface SpawnState {
  /** Empty when the rule is eligible on the next spawn roll. */
  blocked: SpawnBlock[];
  /** With "when": the first variable whose clause fails. */
  failVar: string | null;
  alive: number;
  max: number;
  cooldownMs: number;
  weight: number;
}

export interface EngineState {
  minute: number;
  date: [number, number, number];
  timeScale: number;
  /** Viewport in world px: [x, y, w, h]. */
  camera: [number, number, number, number];
  /** Indexed like PackInfo.vars. */
  vars: { value: number; label: string; pinned: boolean }[];
  /** Back to front (draw order). */
  actors: ActorState[];
  nextSpawnMs: number;
  /** Indexed like PackInfo.spawns; null if the engine couldn't report a rule. */
  spawns: (SpawnState | null)[];
}

export declare class Engine {
  readonly info: PackInfo | null;
  readonly loaded: boolean;
  load(pack: ArrayBuffer | ArrayBufferView, options?: LoadOptions): PackInfo;
  update(dtMs: number, input?: number): void;
  /** Simulate without moving the camera, as the load-time warm-up does. */
  warmup(seconds: number): void;
  /** RGBA view into wasm memory; valid until the next engine call. */
  pixels(): Uint8ClampedArray;
  draw(ctx: CanvasRenderingContext2D | OffscreenCanvasRenderingContext2D, x?: number, y?: number): void;
  state(): EngineState;
  setTime(t: number | string): void;
  setDate(month: number, day: number, year?: number): void;
  setTimeScale(scale: number): void;
  pin(name: string, value: string | number): void;
  unpin(name: string): void;
  centerCamera(x: number, y: number): void;
  worldToDisplay(state: EngineState, wx: number, wy: number): [number, number];
  displayToWorld(state: EngineState, dx: number, dy: number): [number, number];
}

export declare function loadEngine(
  source: string | URL | Response | ArrayBuffer | ArrayBufferView | WebAssembly.Module,
): Promise<Engine>;
export declare function parseClock(s: string): number;
export declare function formatClock(minute: number): string;
