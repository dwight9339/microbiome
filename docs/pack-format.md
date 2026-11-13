# Writing packs

A pack is a directory with a `pack.yaml` and its PNGs:

```
packs/forest/
  pack.yaml
  art/sky.png  art/ground.png  art/deer.png ...
```

Compile it with `python3 tools/packc/packc.py packs/forest -o build/forest.mbp`
(add `--watch` while editing). `packc` reports errors with a path to the
problem, e.g. `actors.deer.states.drink: unknown anim 'walkk' (have: walk, graze, alert, run)`.

`packs/forest/pack.yaml` is the worked example. This page is the reference.

## Editor support

`tools/packc/pack.schema.json` is a JSON Schema for `pack.yaml`. Editors that
understand it give you autocomplete, hover docs, and errors for typos that
`packc` would silently ignore (`duraton: 4s`). In VS Code, install the Red Hat
YAML extension and put this comment at the top of the pack (the forest pack
has it):

```yaml
# yaml-language-server: $schema=../../tools/packc/pack.schema.json
```

`python3 tools/packc/check_schema.py packs/<name>` checks a pack from the
command line (`pip install jsonschema`). `packc` still has the final word:
the schema can't see cross-references such as a `next` naming a missing
state, or the 255-colour limit.

When you add a key to `packc`, add it to the schema too. The `pack_schema`
test fails if a real pack uses a key the schema doesn't know.

## Art requirements

These are the rules to give an artist, human or agent:

- **PNG, RGBA.** Alpha ≥ 128 is opaque; anything lower is transparent. There
  is no partial transparency, so avoid anti-aliased edges against transparency.
- **≤ 255 distinct colours across the whole pack**, shared by every image.
  Working from a fixed palette makes this easy. The forest placeholder uses
  48 colours.
- **Draw for daylight.** Dawn, dusk and night are generated from `lighting`
  keyframes. Colours listed as `emissive` (eyes, fireflies, windows) keep their
  daytime value at night.
- **Sprites face right** (or set `faces: left` on the actor) and are laid out as
  a grid of equal cells, numbered left-to-right, top-to-bottom from 0.
- **Anchor** = the actor's "feet" within a cell; defaults to bottom-centre.
  Lanes and places are expressed in feet positions.
- **Layers** are drawn back to front. The layer the animals walk on should
  be exactly world-sized, so its pixels line up with `places` and `lanes`.
  Further-back layers should be narrower and nearer ones wider (see parallax).
- **Mind the viewport.** At 320×240 the screen shows 320×240 of the world.
  Keep important things within the world's vertical range that a 240-px-tall
  window can reach.
- Pixel budget: images are stored at 1 byte per pixel (uncompressed for now).

## Top level

```yaml
name: Forest
world:     {...}
clock:     {...}
vars:      {...}
lighting:  {...}
layers:    [...]
lanes:     {...}
places:    {...}
obstacles: {...}
actors:    {...}
spawns:    [...]
particles: {...}
```

**Durations** accept `500ms`, `4s`, `2m`, `1.5h` or a bare number of seconds.
**Times of day** are `"HH:MM"`. Quote them, because YAML has opinions about colons.
**Ranges** are `[min, max]` or a single value.

## world

```yaml
world:
  size: [1280, 300]            # required, art pixels
  clear_color: "#000000"       # letterbox / behind everything
  camera:
    start: [640, 200]          # initial camera centre
    pan_speed: 140             # px/s
    idle_drift: 25s            # autopilot after this long without input; 0 = off
    drift_speed: 3             # autopilot glide speed, px/s (0.1-25.5, default 3)
  spawning:
    interval: 4s               # how often to try spawning
    chance: 55                 # percent chance per try
    max_actors: 12             # global cap (engine max: 32)
```

## clock

Used when the device has no real-time clock:

```yaml
clock:
  start: "08:00"
  date: "12-24"                # MM-DD
  day_length: 24h              # e.g. 20m for a fast demo day
```

## vars: world variables

`minute` (0–1439), `hour`, `month` (1–12) and `day` are built in. Define more:

```yaml
vars:
  phase:                       # step function of another variable
    step: minute
    table: {"00:00": night, "05:30": dawn, "07:30": day, "18:00": dusk, "19:45": night}
  season:
    step: month
    table: {1: winter, 3: spring, 6: summer, 9: autumn, 12: winter}
  weather:                     # Markov chain, weights per row
    markov:
      step: 20m                # game time between rolls
      states:
        clear:  {clear: 8, cloudy: 2}
        cloudy: {clear: 3, cloudy: 5, precip: 2}
        precip: {cloudy: 3, precip: 6}
    sensor: weather            # optional: a host sensor of this name overrides it
  magic: {const: 3}
```

String values become an enum: the labels are what conditions and the
debug tools use. Each label is at most 255 bytes. A `step` source must be
defined earlier.

## Conditions

Used by `when`, `if`, and `despawn_when`. All entries must hold (AND):

```yaml
when: {phase: [dawn, dusk], weather: clear}     # list = in, scalar = equals
when: {phase: {not: night}}
when: {minute: {between: ["22:00", "04:00"]}}   # wraps around midnight
when: {month: {in: [11, 12, 1]}, hour: {ge: 6}}
```

Operators: `in`, `not`, `eq`, `ne`, `lt`, `le`, `gt`, `ge`, `between`.
`in`/`not` (and list shorthand) only work for values 0–31. For OR, write two
spawn rules or two `next` entries.

## lighting

```yaml
lighting:
  emissive: ["#ffe98a"]                 # colours immune to darkness
  keyframes:                            # blended by time of day, wraps at midnight
    - {at: "00:00", brightness: 0.32, saturation: 0.45, tint: "#5866b8", tint_strength: 0.55}
    - {at: "07:45", brightness: 1.0}
    - {at: "18:30", brightness: 0.72, tint: "#ff9a6a", tint_strength: 0.5}
```

Each keyframe turns the pack palette into a lit palette: desaturate, scale
brightness, then multiply toward `tint`.

## layers

```yaml
layers:                                 # back to front
  - {name: sky, image: art/sky.png, repeat_x: true, parallax: 0}
  - {name: mountains, image: art/mountains.png}          # parallax: auto (default)
  - {name: ground, image: art/ground.png}                # world-sized → moves 1:1
  - {name: grass, image: art/foreground.png, front: true} # wider than world → faster, over actors
```

- `parallax`: `auto` (default), a number (`0` = fixed to the screen, `1` =
  moves with the world), or `[x, y]`.
- `at: [x, y]`: offset in pixels.
- `repeat_x`: tile horizontally (use with an explicit parallax).
- `front`: draw over actors.

## lanes and places

```yaml
lanes:                                  # bands of feet-y that actors move within
  ground: [232, 290]
  sky:    [30, 120]
places:                                 # points that offer affordances
  pond_west:  {at: [700, 254], offers: [drink], spread: 4}
  oak_branch: {at: [262, 137], offers: [perch], spread: 16}
  burrow:     {at: [381, 248], offers: [burrow]}
```

`spread` lets visitors pick a spot within ±N px horizontally, so birds don't
stack on one pixel. Affordance names are free-form (≤ 32 per pack).

## obstacles

```yaml
obstacles:                              # areas walkers go around, not through
  pond: {lane: ground, ellipse: [688, 248, 832, 276]}   # box [x0, y0, x1, y1] the ellipse fills
```

Actors in that lane never pick a wander target or spawn point inside an
obstacle, and they detour around it (behind or in front) when the direct
route crosses it. An obstacle may run up against a lane edge, like a lake
whose far shore is out of reach; walkers then go round the other side. Keep
at least 2 px of lane free on one side, or nothing can get past it.
A place that sits on the edge, like a drinking spot on the shore, is fine.
Draw the obstacle and declare it with the same box. Up to 8 per pack, each
at most 1024 px per side.

## actors

```yaml
actors:
  deer:
    sheet: art/deer.png
    frame: [32, 32]             # cell size
    anchor: [16, 31]            # feet; default bottom-centre
    faces: right                # direction the art faces
    tags: [prey, large]         # free-form, ≤ 32 per pack
    lane: ground
    speed: 14                   # px/s
    anims:
      walk:  {frames: 0-3, fps: 6}            # or frame: 160ms
      graze: {frames: [4, 5], frame: 600ms}
      blink: {frames: [1, 0], frame: 150ms, loop: false}
    states:                     # first state = initial state
      wander: {anim: walk, move: wander, duration: [6s, 14s], next: {graze: 3, drink: 1}}
      graze:  {anim: graze, duration: [5s, 12s], next: {wander: 2, graze: 1}}
      drink:  {anim: walk, move: seek drink, next: sip}
      sip:    {anim: graze, duration: [4s, 8s], next: wander}
      alert:  {anim: alert, duration: [1s, 2s], next: [{to: flee, weight: 3}, {to: graze, if: {phase: day}}]}
      flee:   {anim: run, move: flee, speed: 3.5}
      leave:  {anim: walk, move: exit}
    react:
      - {to: [predator], radius: 90, goto: alert}
    leave: leave                # state used when despawning
    despawn_when: {phase: night}
    lifetime: 6m
```

### State fields

| Field | Meaning |
| --- | --- |
| `anim` | animation name (defaults to the state's own name if an anim has it) |
| `move` | `idle` · `wander` · `seek <affordance>` · `exit` · `flee` |
| `duration` | seconds or `[min, max]`. Required for `idle`. A `wander` with a duration keeps strolling until it expires; without one, it ends on arrival. `seek` always ends on arrival. |
| `next` | `{state: weight}`, a single name, or a list of `{to, weight, if}`. Picked by weight among entries whose `if` holds. If nothing is eligible, the state repeats. |
| `speed` | multiplier on the actor's speed (< 16) |
| `until: arrive` | a `wander` with a duration also ends when it reaches its target |
| `then: despawn` | the actor disappears when the state ends (into a burrow, a hollow...) |

`exit` walks off the nearest world edge, and `flee` runs off the edge away
from whatever triggered the reaction. Both despawn the actor on arrival.

### Reactions

Checked about 5 times a second. If any other actor carrying one of the `to`
tags is within `radius` pixels, the actor switches to `goto`. Reactions are
then suppressed for 8 s, and while the actor is fleeing.

## spawns

```yaml
spawns:
  - actor: deer
    when: {phase: [dawn, day, dusk]}
    weight: 3                   # relative odds among eligible rules
    max: 2                      # alive at once from this rule
    count: [1, 2]               # group size
    cooldown: 90s
    enter: edge                 # edge | place <affordance> | anywhere (prefers off-camera)
```

## particles

```yaml
particles:
  snow:      {when: {weather: precip, season: winter}, count: 90, color: "#f4f8ff",
              vx: [-4, 4], vy: [12, 22], wobble: 2}
  fireflies: {when: {phase: night}, count: 24, color: "#ffe98a", band: [200, 290],
              vx: [-6, 6], vy: [-4, 4], blink: true, wander: true}
  stars:     {when: {phase: night}, count: 40, color: "#fffbe0", band: [0, 150], blink: true, above: sky}
```

`vx`/`vy` are in px/s. `band` is a world y range (default: the whole world).
`size` is in px. `wobble` is the horizontal sway amplitude. `above: <layer>`
draws the particles right after that layer instead of on top of everything
(stars behind the mountains). Systems fade in and out when their condition
changes. The engine holds 256 particles in total.

## Limits (engine defaults, see `MB_MAX_*` in `core/include/mb/mb.h`)

32 live actors, 256 particles, 8 particle systems, 16 layers, 64 spawn rules,
32 variables, 32 tags, 32 affordances, 254 states per actor, 255 colours,
8 obstacles.
