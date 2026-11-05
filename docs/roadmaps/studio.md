# Studio Roadmap (draft)

**Product promise:** A creator can make, preview and save a living world visually, using the actual engine.  
**Shared deadline:** [Gift 2026](../gift-2026.md) — one distinct gift world by Dec 18, 2026.  
**Links:** [Vision](../vision.md) | [Unified roadmap](../roadmap.md) | [Engine/Firmware](engine-firmware.md) | [Companion](companion.md) | [Detailed Studio concept](../studio.md).

## Baseline

As of Nov 4, Studio's real engine WebAssembly host, introspection API, minimal viewer and schema are implemented on the open studio-phase0 PR. **There is no full authoring application yet.** The existing [longer Studio plan](../studio.md) records architecture and future editor ideas, but the gift slice below takes precedence.

## GIFT 2026 — Desktop-first creator MVP (Now)

**One end-to-end task:** create a distinct world from a starter folder -> import/replace PNG art -> edit visually -> save pack.yaml/assets -> compile with existing Python packc -> preview the actual Wasm engine -> build gift firmware using that pack.

| ID | Deliverable | Acceptance evidence | Needs |
| --- | --- | --- | --- |
| S-G1 | Runnable desktop-first shell with pack-folder open and recent-folder workflow | Opens a valid local pack folder and shows a live engine preview | Wasm host, desktop file adapter |
| S-G2 | Minimal **visual editing**: layer ordering/placement, place positions/affordances, basic lane region, basic actor/spawn settings | User edits at least one item of each essential class in UI; reload visibly updates scene; edits can be saved | pack schema + YAML Document handling; E-G2 vocabulary |
| S-G3 | Manage essential art and preview | Import/replace PNG images or point layer/actor at provided art; view display dimensions/color parity; select/time/weather preview | packc + Wasm, target device profile |
| S-G4 | Reliable save, compile and error surfacing | Save preserves unrelated YAML fields/comments where feasible; failures are shown with actionable location; reopens unchanged; no silent overwrite of externally modified files | single source format + packc |
| S-G5 | Gift acceptance workflow | Creator produces one distinct pack (not just changes to the default scene), validates and previews it; known-good build/flash instructions yield device result | Engine E-G1, E-G2–E-G4, gift hardware |

**Scope choice:** Tauri desktop first; no Pyodide browser compiler prerequisite. The minimum UI need not expose every advanced field. A clearly labeled YAML source view is an acceptable escape hatch; the core visual operations above must be genuinely usable.

**Non-goals for gift:** full node-graph actor editor, polished sprite-sheet composer, full light-keyframe timeline, palette manager, cloud collaboration, online gallery, web parity, arbitrary worlds/hardware, device firmware updater.

## Timing and build strategy

- **By Nov 13:** open pack -> preview real engine -> first visual change; test compiler link from the actual desktop app.
- **By Nov 20:** edited place/layer round-trips through save/compile; visible errors.
- **By Nov 27:** actor/spawn settings editable; generate the selected new world from the app end-to-end.
- **By Dec 4:** gift world on physical device, revised using Studio; freeze authoring scope and fix bugs through Dec 11.
- **Dec 11–18:** no new editor features; use tool to polish and re-verify gift world while hardware is assembled.

## Post-gift sequence (Planned)

1. **Robust viewer and workspace:** live time/date/weather scrubbing, actor picking, debug overlay, safe hot reload, undo/redo, file watching and multi-pack projects.
2. **Scene/canvas editing:** layers, navigable surfaces, transitions, lanes, places, obstacles, affordance access and occupancy; avoid hardwiring UI to today's single-lane engine model.
3. **Behavior editing:** state-machine graph, weighted conditions, actor individuality/variation, spawn rule eligibility and deterministic scenario inspector.
4. **Assets/lighting/budgets:** sprite sheets, animations by surface/locomotion, palette tools, keyframe timeline, flash/actor limits and validated device profiles.
5. **Broader delivery:** optional browser build (Pyodide spike before commitment), starter templates, exporting versioned worlds, **Send to Companion** hand-off.

## Cross-track ownership

- **Engine** owns behavior, physics/navigation contracts, pack versions and real rendering.
- **Studio** owns creation and authoring UX, validation feedback, source asset changes and export.
- **Companion** owns device discovery, configuration, firmware updates, independent pack installation and marketplace/library. Studio should share APIs/handoff, **not** duplicate updater or USB stack.

## Risks and first principles

- The hardest early commitment is lossless/safe YAML editing. Use a comment-preserving parser, verify round-trips with real packs, and add backups or conflict prompts.
- Avoid overcommitting to deeply bespoke editor widgets while movement/terrain schemas may evolve.
- Keep one compiler (Python packc) and one preview implementation (actual Wasm core).
- Evaluate usability against one first-time world-making task, not the number of editor controls implemented.
