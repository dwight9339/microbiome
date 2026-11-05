# microbiome Studio

> **Planning note:** this document keeps the long-term Studio design. For the gift, the [Studio track](roadmaps/studio.md) takes precedence: a basic Studio (local dev server + the Wasm viewer, drag-to-move editing, comment-preserving save) rather than the Tauri app described below, which is deferred until after Christmas. Device flashing, firmware updates, library and device configuration belong to the separate [Companion track](roadmaps/companion.md); Studio can hand off a built world.


A desktop and browser app for building packs visually. You see the scene as the
device would show it, and you edit layers, places, lanes, animals and lighting
by working on that view, with no YAML/compile/reload loop in between.

Status: **Phase 0 done** (the engine runs in the browser; see
`platform/wasm`). Phase 1 (the viewer app) is next. This page records the plan
and the decisions behind it. Update it as they change.

## Decisions so far

| Question | Decision |
| --- | --- |
| App shell | **Tauri 2** (Rust shell, TypeScript web UI) |
| Pack compiler | **Keep `packc` in Python** for now. It stays the only compiler. |
| Browser-hosted | **Yes, a goal.** The same UI should run as a web page. Desktop adds native file access and device I/O on top. |
| Repository | **This repo**, under `studio/`, until the pack format settles (see [Repository](#repository)) |

## Principles

1. **`pack.yaml` + PNGs stay the source of truth.** The studio opens a pack
   directory, edits the YAML in place without losing comments, key order or
   formatting, and saves. There's no separate project format. Hand-editing,
   `packc --watch`, `mbsim` and git diffs all keep working, and someone using
   the studio and someone using a text editor can work on the same pack.
2. **The preview runs the real engine.** `core/` is compiled to WebAssembly
   and runs unmodified, so what the studio shows is pixel-identical to
   `mbsim`, `mbsnap` and the device. There's no JavaScript re-implementation
   that can drift from it.
3. **One compiler.** `packc` is the only thing that turns YAML into a `.mbp`.
   The studio runs it; it never re-implements it.
4. **Studio-only work goes in the engine when it helps everyone.**
   Introspection such as "why isn't this spawn rule firing?" goes in the
   core API, so `mbsim` and `mbsnap --info` can use it too.

## Features, by how much they help

**1. Live preview with the real engine**
- Time scrubber (drag through 24 h), date and season picker, pins for every
  world variable (weather and anything else the pack defines), time-speed control.
- Device frames: 320×240, 240×320, 480×480 and custom sizes, showing the
  integer scale and viewport the engine picks. An optional RGB565 mode shows
  the colours as the LCD will show them.
- The `D` debug overlay from `mbsim`, made clickable: select an actor to see
  its state, timer, target and the reaction that fired.

**2. Editing directly on the canvas**
- Drag **places** and their `spread`, resize **lane** bands, draw **obstacle**
  ellipses over the art they belong to.
- Reorder **layers**, toggle `front`, set parallax and watch it react as you pan.
- **Sprite sheet tool**: frame grid, anchor/feet point, animation frame lists
  with playback.

**3. Behaviour editors**
- **Actor state machine** as a node graph: states as nodes, weighted `next`
  edges, `if` conditions on the edges, reactions as tag-based triggers.
- **Spawn rules** as a table that shows which rules are eligible right now
  and, for the others, why not: condition false (and which variable), at
  `max`, cooling down, or the global cap.
- **Condition builder** that knows each variable's enum labels.
- **Lighting timeline**: keyframes on a 24 h track, a tint colour picker, the
  lit palette shown under each keyframe, and an `emissive` picker that lets you
  click a colour in the art.

**4. Budgets and checks, always visible**
- Palette usage (N/255), with the images that add new colours highlighted.
  Optional snap-to-palette when importing art.
- Pack size against the RP2350 flash budget, broken down by image.
- Engine limits (32 actors, 16 layers, 8 obstacles…), and `packc` errors and
  warnings linked to the field or canvas object they refer to.

**5. Handing a world to the device utility**
- Export a compiled world and show a clear **Send to Companion** action when available.
- The separate Companion app owns device setup, world installation, UF2 flashing,
  firmware updates, serial configuration, and recovery. For Christmas 2026,
  the documented manual BOOTSEL/UF2 path is an acceptable interim workflow.

## Architecture

```
studio/                            (TypeScript UI; same bundle on desktop and web)
 ├─ editors: canvas, node graph, timeline, inspector
 ├─ yaml: comment-preserving read/modify/write (npm `yaml` Document API)
 ├─ preview ──────────> platform/wasm  (core/ → microbiome.wasm + microbiome.js)
 ├─ schema ───────────> tools/packc/pack.schema.json  (forms, autocomplete, validation)
 ├─ compile ──────────> packc   desktop: Python sidecar · web: Pyodide (see below)
 └─ host adapter
      ├─ desktop (Tauri/Rust): files, file watching, packc sidecar, export
      └─ web: File System Access API / drag-and-drop, Pyodide
```

The UI talks to the outside world only through a small **host adapter**
interface: read and write pack files, watch for changes, compile, and export.
Tauri implements it in Rust and the web build implements it with browser APIs.
Everything above the adapter is shared, which is what makes browser hosting
cheap.

### The pieces that live outside `studio/`

These were Phase 0 and are in place. They are useful on their own, without the studio:

| Piece | Where | What |
| --- | --- | --- |
| WebAssembly host | `platform/wasm/` | A fourth host next to `sdl`, `headless` and `rp2350`. Compiled with plain `clang --target=wasm32` (no Emscripten, no libc). A JS wrapper (`microbiome.js`) loads packs, steps the simulation, renders into RGBA, and exposes the introspection API as JSON. Includes a minimal web viewer. |
| Introspection API | `core/include/mb/mb.h` | Spawn-rule eligibility and the reason a rule is blocked, richer actor info, enum labels for any value, pinned state, camera view rect and date getters. |
| Pack schema | `tools/packc/pack.schema.json` | JSON Schema for `pack.yaml`. It drives the studio's forms and validation. Editors such as VS Code with the YAML extension get autocomplete and typo detection from it today. |

### packc in the browser

Desktop runs `packc` as a sidecar: the user's Python, or later a bundled
PyInstaller binary. Without a server, a browser can't run Python. The plan is
**Pyodide**, which runs CPython in WebAssembly and ships PyYAML and Pillow, so
the unmodified `packc.py` runs there and stays the only compiler.

To check in the Phase 1 spike:
- **Compile time.** The forest pack takes about 1 s in native CPython, almost
  all of it the per-pixel palette loop in `Builder.image`. Under Pyodide expect
  a few times slower. Vectorising that loop with Pillow (`Image.getdata`, or
  `tobytes` plus a palette lookup) should make both paths roughly 10× faster.
  It's worth doing either way.
- **Download size.** About 10 MB for Pyodide with the packages, cached after
  the first load. The desktop build doesn't need it.
- If Pyodide proves too heavy, the fallback is porting `packc` to TypeScript
  as the single compiler, with the CLI running under Node. That was the
  alternative considered and set aside for now.

### Hot reload

The preview reloads the way `mbsim` does: keep camera, time of day, date, time
scale and pinned variables (matched by name), then re-init with the new pack.
The JS wrapper does this (`engine.load(bytes, { keepView: true })`).

## Phases

| Phase | Deliverable |
| --- | --- |
| **0** ✓ | `platform/wasm` + introspection API + pack JSON Schema. Also a minimal web viewer (`platform/wasm/web`) as the host's test harness; CI checks pixel parity with `mbsnap`. |
| **1** | Viewer: open a pack (desktop: folder; web: folder picker or drag-and-drop), live preview, time and variable scrubbing, clickable debug overlay, hot reload. Spike packc-on-Pyodide. |
| **2** | Canvas editing: places, lanes, obstacles, layers, saved back to YAML with comments intact |
| **3** | Behaviour: state-machine graph, spawn table with "why not?", lighting timeline |
| **4** | Assets and budgets: sprite sheet tool, palette manager, size meters |
| **5** | Export and hand-off to Companion. The dedicated Companion app owns flashing, device configuration, and firmware updates. |

Phase 1 alone replaces `mbsim` + `packc --watch` for most iteration and is a
good point to decide whether the rest is worth building.

## Repository

The studio lives in this repo under `studio/` for now:

- **The contract is still moving and is shared.** A new movement type or
  condition operator currently means changing `pack_format.h`, `packc.py` and
  the docs in one PR. Its studio editor belongs in the same PR. Across two
  repos that becomes coordinated releases and version pinning while the format
  is still changing often.
- **The studio needs the engine's source** to build the wasm preview and
  bundle packc.
- **CI handles the cost.** Studio jobs are filtered to `studio/**`,
  `platform/wasm/**`, `core/**` and `tools/packc/**`, so engine-only changes
  don't pay for Node/Rust builds, and format changes that break the studio
  fail in the same PR.

**When to split it out:** other people work on the studio but not the engine,
its release cadence differs from the firmware's (signed installers,
auto-update), or the pack format has settled. By then the boundary is already
clean (the wasm host, packc and the JSON Schema). The studio would pin an
engine version and check `MB_PACK_VERSION` and the schema on load.

## Open questions

- UI framework for `studio/` (React + React Flow for the node graph is the
  default assumption; Svelte is the alternative).
- How the web build saves: File System Access API (Chromium) with
  download/upload as the fallback elsewhere.
- Whether `packc` should emit a JSON sidecar (warnings with YAML paths, image
  and palette stats) for the studio to read, rather than the studio parsing
  its text output.
