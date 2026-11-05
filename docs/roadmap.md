# Microbiome — Unified Roadmap (draft)

**Planning date:** 2026-11-04. **Highest priority:** [Christmas 2026 gift milestone](gift-2026.md), target **Friday, December 18, 2026**; Christmas is December 25. Dates are planning gates, not guarantees. Hardware fabrication and shipping lead times must be confirmed immediately.

## Reading this roadmap

- [Vision](vision.md) — long-term experience and architectural principles.
- [Gift 2026 execution plan](gift-2026.md) — shared deadline, acceptance criteria, weekly gates, cut lines, hardware critical path.
- [Engine & Firmware track](roadmaps/engine-firmware.md) — simulation, portable application/platform services, RP2350, custom board/enclosure integration.
- [Studio track](roadmaps/studio.md) — minimum viable world authoring followed by full visual editing.
- [Companion track](roadmaps/companion.md) — separate device utility, independent world installation, firmware updates, eventual world sharing.
- [Longer-term Studio design](studio.md) — detailed concept from the Phase 0 work, superseded by this roadmap on gift scope and device-management ownership.
- [Ideas / discovery backlog](ideas.md) — promising uncommitted concepts.

**Status vocabulary:** Now = actively needed for the gift; Planned = accepted goal but not yet underway; Exploring = validate before committing; Later = expressly out of gift scope; Done = verified with evidence. Do not treat a proposed concept as implemented.

## Current baseline

- Portable C11 engine, Python pack compiler, SDL simulator, headless renderer and RP2350 firmware exist.
- WebAssembly preview, introspection and pack schema are implemented on **studio-phase0 / PR #2**, but that PR is **open**, not yet in main as of this plan.
- RP2350 firmware currently **embeds** a compiled world into the UF2. Independently installing a pack is a later protocol/storage project.
- A full visual Studio and a standalone Companion application do **not** yet exist.
- The existing firmware/breadboard target is Pico 2 + ST7789. The **gift plan instead targets an RP2350 Feather on a custom carrier PCB** with the existing TFT and buttons, and optional RTC. Its new GPIO mapping, battery/backlight budget, PCB and enclosure have **not** yet been validated. See [Feather hardware plan](hardware/feather-carrier-2026.md).

## Priority order through December 18

1. **Gift hardware and build feasibility:** freeze the RP2350 Feather model, TFT/buttons, LiPo power assumptions and new GPIO map; order a custom carrier PCB quickly, prototype enclosure and physical assembly.
2. **End-to-end golden path:** create one world -> compile -> preview -> build/install UF2 -> run reliably on the actual device.
3. **Minimum viable Studio:** enough graphical authoring to build and save one distinct world, using desktop-first implementation and the existing pack compiler. Advanced YAML fallback is acceptable for nonessential settings.
4. **Small engine vertical slice:** capability-aware reachable affordances, visible bird ground-to-perch-to-ground traversal without respawn, and matching contextual animations.
5. **Polish and delivery:** art, burn-in, documentation, enclosure, spare recovery route.
6. **Everything else** only after the gift is secure.

## Three linked tracks

| Track | Gift deliverable (Now) | After gift |
| --- | --- | --- |
| [Engine & Firmware](roadmaps/engine-firmware.md) | Reliable RP2350 firmware; one embedded custom world; minimal connected-habitat / reachability / locomotion slice; custom PCB/enclosure bring-up | richer animal generation, terrain and weather physics, reusable firmware application, platform abstraction, independent pack storage, second target |
| [Studio](roadmaps/studio.md) | Open/edit/save/compile/live-preview one complete new world with a small set of essential visual tools | comprehensive visual canvas, behavior graph, sprite-sheet editor, lighting timeline, budgets, web build |
| [Companion](roadmaps/companion.md) | **Not a gift blocker.** Document manual UF2/BOOTSEL recovery and a future device protocol | dedicated desktop utility: device discovery, settings, safe firmware updates, separate world installation, gallery/sharing |

## Cross-track dependencies and ownership

| ID | Dependency | Owner / consumer | Gift treatment |
| --- | --- | --- | --- |
| X1 | Valid pack format + Python packc + real engine/Wasm preview | Engine -> Studio | Must work for one custom world; keep format/schema aligned |
| X2 | Basic connected-habitat, reachable-place and locomotion vocabulary | Engine -> pack schema + Studio | Choose minimal contract before building deep behavior editors; avoid overdesign |
| X3 | Build/flash reliable RP2350 UF2 containing a custom world | Firmware + hardware -> Studio gift workflow | Must; manual UF2 install is acceptable |
| X4 | Display geometry, memory/flash budgets, and target configuration | Hardware/Firmware -> Studio validations + eventual Companion | Freeze for gift board; expose known limits |
| X5 | Portable device information, control, storage and update protocol | Firmware -> Companion + Studio hand-off | Define direction; full implementation post-gift |
| X6 | Separate world pack storage, compatibility/version checks and atomic activation | Firmware -> Companion world install | Later; **not required** to give the gift |
| X7 | **Feather carrier PCB**, TFT/button wiring, LiPo/regulator/backlight current budget, USB/BOOT access, enclosure tolerances, optional RTC | Hardware -> Firmware + experience | Critical path; revise Pico GPIO mapping, prototype and order in parallel with software |

## Gift execution gates

| By | Evidence required |
| --- | --- |
| **Nov 6** | Exact Feather SKU/TFT/battery/inputs and gift world selected; draft Feather GPIO map reviewed; optional RTC decision and Studio minimum scope agreed |
| **Nov 13** | Feather carrier PCB reviewed and ordered; first enclosure fit draft; Feather-target pack -> preview -> UF2 -> wired hardware path |
| **Nov 20** | Editable Studio prototype opens a pack and previews changes; engine reachability/transitions prototype evaluated; enclosure prototype tried |
| **Nov 27** | Studio saves and rebuilds a distinct world; engine slice merged or cut; PCB fabrication/delivery tracked with contingency |
| **Dec 4** | New world displayed on real hardware; first custom-board bring-up and enclosure fit as parts arrive; remaining defects triaged |
| **Dec 11** | Feature and artwork freeze; device + PCB + enclosure integration tested; recovery plan and burn-in candidate ready |
| **Dec 18** | Assembled, tested, gift-ready device and source/artifacts backed up; **no new feature dependencies** |

[Full criteria, risks and contingency rules](gift-2026.md).

## Scope policy

**Must-have** means the gift cannot be called ready without it. **Should-have** is pursued only when the path to must-have is secure. **Stretch** is cut first. **Later** is not worked before delivery except minimal interface notes. See each track for detailed sorting.

The current largest schedule risks are **custom hardware lead time**, **Studio being at concept/Phase 0 rather than an editor**, and **engine navigation redesign**. Reduce these by ordering hardware early, reusing the existing TFT/buttons but validating the Feather RP2350 pinout and battery power path, and implementing only one observable bird/rabbit vertical slice. Do **not** make Companion, over-the-air updates, marketplace, full actor generation, full terrain physics, or a second MCU prerequisites.

## How decisions evolve

1. Record a user-visible scenario in [Ideas](ideas.md); do not immediately assign a deadline.
2. Ask whether it advances the [Vision](vision.md); prototype uncertain engine boundaries.
3. Promote to a track as Exploring -> Planned -> Now only with acceptance criteria, constraints, and dependency links.
4. Maintain one **integrated gift critical path** in [gift-2026.md](gift-2026.md), not three competing calendars.
5. At weekly gate reviews, record measured results, blocked dependencies, scope cuts and decision owners.
6. Log architectural decisions in docs/decisions when they are difficult to reverse.
7. Keep the Studio and Companion boundaries explicit; shared code/interfaces are encouraged, duplicate feature ownership is not.

**Branch note:** this documentation branch starts at the current studio-phase0 PR #2 head, rather than main. Merge/rebase it after PR #2 to retain the existing Studio plan and Wasm baseline.
