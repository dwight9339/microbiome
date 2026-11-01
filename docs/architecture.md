# Architecture

## Goals

1. **Content is data.** Scenes, creatures and behaviour live in packs, not code.
2. **Hardware-agnostic core.** Display size, pixel format, MCU, RTC and sensors
   are the platform's business. The core is plain C11 with no heap, no OS,
   and no SDK includes.
3. **Small and predictable.** Fixed-capacity pools, a pack read in place from
   flash, strip rendering instead of a framebuffer.

## The engine/host split

The host owns the main loop and hardware; the engine is a library it calls:

```c
mb_engine_init(&eng, pack, size, &cfg);   // cfg: display w/h, pixel format, seed, host callbacks
for (;;) {
    mb_engine_update(&eng, dt_ms, input_bits);              // logical inputs: MB_IN_LEFT...
    for (y = 0; y < H; y += STRIP)
        mb_engine_render_strip(&eng, y, STRIP, buf, stride); // native pixels, then DMA/blit
}
```

Host services are optional callbacks in `mb_host_t`:

| Callback | Used for | If absent |
| --- | --- | --- |
| `wall_time` | real time of day/date (RTC, NTP, OS) | simulated clock from the pack's `clock:` section |
| `read_sensor(name)` | world variables declared with `sensor: <name>` | the variable's own definition (markov, step...) |

Porting to a new device means writing those few functions plus a display
driver that can accept strips. Nothing in `core/` changes.

## Coordinates, scale, and resolution independence

- The **world** is a rectangle in *art pixels* (forest: 1280×300).
- The display shows a **viewport** of it: `view = display / scale`.
- `scale` is an integer (pixel art only looks right at integer scales). Auto
  picks the smallest scale whose viewport doesn't exceed the world:
  320×240 → 1× (320×240 view); 480×480 → 2× (240×240 view); 1920×1080 → 4×.
- The **camera** (Q8 fixed point) pans within the world and is clamped to it.
  If the world is smaller than the viewport, it's centred and letterboxed.

A larger or differently-shaped display therefore shows *more* or *less* of the
same world, never a stretched one, and packs don't need per-device art.

**Parallax.** Each layer has a horizontal/vertical scroll factor. The default
`auto` makes a layer's edges meet the world's edges at the extremes of camera
travel: `(layer_w - view_w) / (world_w - view_w)`. A narrow mountains layer
scrolls slowly, a world-sized layer moves 1:1, and a foreground wider than the
world moves faster than 1:1. Because it is computed at init from the actual
viewport, it adapts to any display.

## Rendering

```
for each display row:
    vy = row / scale
    if vy is new:  compose line[view_w] (8-bit palette indices):
                     clear → back layers (+ particles placed "above" them)
                     → actors sorted by feet-y → front layers → top particles
    expand line through the 256-entry LUT into native pixels, × scale horizontally
```

- Only a **strip** buffer is needed (RP2350: 2 × 320×16×2 B = 20 KiB,
  double-buffered so the CPU renders strip N+1 while DMA sends strip N).
- Index 0 is transparent in every image.
- The **LUT** maps palette index → native pixel (RGB565 BE/LE, XRGB8888). It's
  rebuilt about once per game-second by blending the two lighting keyframes
  around the current time, so day→dusk→night costs 256 lerps, not per-pixel work.
- On RP2350 the per-pixel functions are placed in SRAM via the `MB_HOT` hook
  (XIP flash execution would stall on cache misses).

## Simulation

All of this is driven by pack data:

- **Clock**: the host wall clock if available, else simulated (configurable day
  length and start). `mb_set_time_scale` fast-forwards for testing.
- **World variables** (`int32` blackboard, ≤ 32): built-ins `minute`, `hour`,
  `month`, `day`, plus pack-defined:
  - `step`: a step function of an earlier variable (e.g. `phase` from
    `minute`, `season` from `month`)
  - `markov`: a weighted Markov chain stepped every N game-minutes (weather)
  - `const`
  - any of them can be overridden by a host sensor.
- **Conditions**: AND of clauses over variables (`in`, `not`, `eq`, `lt`,
  `between` with wrap-around...), compiled to a table and deduplicated.
- **Spawning**: every interval, with some chance, one eligible spawn rule is
  picked by weight (condition true, under its `max`, cooldown expired). It
  spawns a group of 1..n at a world edge, a place, or off-camera.
- **Actors** run a weighted random **state machine**. A state has an animation,
  a movement (`idle`, `wander`, `seek <affordance>`, `exit`, `flee`), a
  duration, and weighted `next` states (optionally conditional).
  - **Reactions** are tag-based ("enter `alert` if any actor tagged
    `predator` is within 90 px"), so adding a species never requires editing
    the others.
  - **Despawning**: a `despawn_when` condition or a lifetime switches the
    actor to its `leave` state (walk off-screen, fly away, go into a burrow).
- **Places** are points in the world that offer **affordances** (`drink`,
  `perch`, `burrow`...). Actors seek affordances, not specific places, so new
  scenery works with existing animals.
- **Particles**: snow, rain, fireflies and stars. They are world-space but
  wrapped to the viewport width, fade in and out with their condition, and can
  blink, wander and sway.
- **Camera autopilot**: after `idle_drift` seconds without input, the camera
  eases toward a randomly chosen animal and re-picks every 20–45 s.

## Pack binary format

`packc` emits one little-endian blob: a header with a section table
(offset+count per section), then fixed-size record arrays, a BLOB section
(pixels, variable tables) and a STRINGS section. All sections are 4-byte
aligned so the engine can cast pointers directly into memory-mapped flash.
`mb_pack_bind` range-checks every cross-reference once at load time, so the
hot paths index without checks. A malformed pack is rejected with an error,
never a crash.

`core/include/mb/pack_format.h` is the contract. Any struct change must be
mirrored in `tools/packc/packc.py` (struct formats and size asserts) and
`MB_PACK_VERSION` bumped.

## Budgets (RP2350, forest pack)

| | |
| --- | --- |
| Flash | ~1.5 MB of 4 MB (the pack is ~1.4 MB, almost all uncompressed 8bpp pixels) |
| SRAM | ~34 KB static: engine state 8.5 KB, strip buffers 20 KB |
| SPI | 320×240×2 B = 150 KB/frame. At 37.5 MHz that's ~33 ms (≈30 fps max); at 75 MHz ≈60 fps |

**Not yet measured on real hardware:** render time per frame, and the cost of
reading layer pixels through the XIP cache. The serial `stats` command prints
both once it's running.

## Roadmap / known gaps

- **Image compression.** Run-length encoding of transparent runs would shrink
  the mostly-empty foreground layer by about 90% and speed up rendering.
  Optionally 4bpp images.
- **Palette banks.** There is one global 255-colour palette today. Per-layer
  banks would allow richer art.
- **Lighting modifiers** by weather (overcast = desaturate), and backlight
  dimming at night (it would need a host `set_brightness` callback).
- **User events.** A/B buttons are plumbed through but unused. Dropping food
  would be a pack-defined "player-spawned actor" plus a `food` tag/affordance
  that animals already know how to seek, so it needs no special case.
- **Place occupancy** (one owl per branch), joint interactions (chase), and
  world wrap-around.
- Second RP2350 core: rendering could be split across both cores if profiling
  shows it's needed.
