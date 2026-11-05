# Studio Roadmap (draft)

**Product promise:** a creator can make, preview and save a living world visually, using the actual engine.  
**Links:** [Vision](../vision.md) | [Unified roadmap](../roadmap.md) | [Gift 2026 dates](../gift-2026.md) | [Engine/Firmware](engine-firmware.md) | [Companion](companion.md) | [Detailed Studio concept](../studio.md).

## Baseline

The WebAssembly engine host, introspection API, a minimal browser viewer (time scrubber, variable pins, debug overlay, click-to-inspect) and the pack schema are in main. For hand-editing, `packc --watch` plus `mbsim`'s hot reload already gives edit → save → see the result in about half a second, and the schema gives editor autocomplete for `pack.yaml`. There is no authoring application yet.

## For the gift: a basic Studio

**Why:** a tool for building the gift world faster than editing YAML by hand. The gift doesn't contain Studio; it's judged only by whether it speeds up that world.

**What's slow today is positions.** Numbers like spawn chances, state machines, lighting keyframes and weather weights are as quick to edit in YAML as in any UI. Places, lanes, obstacles, perches and layer offsets are coordinates you guess, rebuild and check. The basic Studio fixes that and little else.

| ID | Deliverable | Done when |
| --- | --- | --- |
| S-G1 | **Done.** **Local dev server** (`studio/server.py`): serves the Wasm viewer for a pack folder, reruns packc when a source file changes, pushes the new pack so the viewer reloads in place (camera, clock and pins kept) | Saving `pack.yaml` or a PNG in any editor updates the browser within a second or so |
| S-G2 | **Done.** **Error banner**: packc errors and schema problems shown in the page, with whatever location packc reports | A broken `pack.yaml` shows a readable error instead of a stale or blank scene |
| S-G3 | **Edit overlay** in the viewer: draws places, lanes and obstacles over the scene, shows world coordinates under the cursor, lets you drag them | Places, lanes and obstacles can be moved by dragging |
| S-G4 | **Save back to YAML** with ruamel.yaml round-tripping: the server rewrites only the coordinate values that changed | A drag saves into `pack.yaml` with comments, ordering and other formatting intact; `git diff` shows only the moved numbers |
| S-G5 | **Gift use**: the gift world is laid out with it | The world's layout was built in Studio; everything else edited in YAML |

The viewer's existing time and weather controls cover checking the scene at dusk or in rain. Art needs no import feature: put PNGs in `art/` and the server rebuilds.

**Fallback:** if S-G1 to S-G4 aren't working by the [gift plan's Studio gate](../gift-2026.md#week-by-week), build the world with YAML + `packc --watch` + `mbsim` and finish Studio after Christmas.

**Not for the gift:** a desktop shell (Tauri), layer/actor/spawn editing forms, PNG import, a recent-folders workflow, undo/redo, a behaviour graph, a sprite-sheet editor, a lighting timeline, browser-side compiling (Pyodide), device flashing.

## After the gift (Planned)

1. **Workspace:** undo/redo, a pack-folder picker, multi-pack projects, safe handling of files changed outside Studio.
2. **Scene editing:** layers, navigable surfaces, transitions, affordance access and occupancy; avoid hard-wiring the UI to today's single-lane model.
3. **Behaviour editing:** state-machine graph, weighted conditions, actor variation, spawn eligibility and a deterministic scenario inspector.
4. **Assets, lighting and budgets:** sprite sheets, animations by surface/locomotion, palettes, a keyframe timeline, flash/actor limits per device profile.
5. **Packaging:** decide between a desktop shell (Tauri) and staying browser + local server; optional hosted browser build (Pyodide spike first); starter templates; versioned export; **Send to Companion**.

## Cross-track ownership

- **Engine** owns behaviour, navigation contracts, pack versions and real rendering.
- **Studio** owns authoring UX, validation feedback, source-file changes and export.
- **Companion** owns device discovery, configuration, firmware updates, pack installation and the world library. Studio hands off to it and does **not** duplicate the updater or USB stack.

## Principles

- Safe YAML editing comes first: round-trip with ruamel.yaml, verify on real packs, and never silently overwrite a file changed outside Studio.
- One compiler (Python packc) and one preview implementation (the real Wasm core).
- Don't build elaborate widgets for schemas that are about to change (movement, terrain).
- Judge Studio by how fast a world gets built, not by how many controls it has.
