# Microbiome — Unified Roadmap (draft)

**Highest priority:** the [Christmas 2026 gift](gift-2026.md), gift-ready by **Friday, December 18, 2026**. All dates and gates live in that file. One person builds all three tracks, so the tracks are ways of organising the work, not teams.

## Reading this roadmap

- [Vision](vision.md) — long-term experience and architectural principles.
- [Gift 2026 plan](gift-2026.md) — the only place with dates: definition of done, weekly gates, cut lines.
- [Engine & Firmware track](roadmaps/engine-firmware.md) — simulation, portable application/platform services, RP2350, custom board/enclosure integration.
- [Studio track](roadmaps/studio.md) — a basic Studio for laying out the gift world, then full visual editing.
- [Companion track](roadmaps/companion.md) — separate device utility, independent world installation, firmware updates, eventual world sharing.
- [Longer-term Studio design](studio.md) — detailed concept from the Phase 0 work. Where it differs, the Studio track wins on gift scope and Companion owns device management.
- [Ideas / discovery backlog](ideas.md) — promising uncommitted concepts.

**Status vocabulary:** Now = actively needed for the gift; Planned = accepted goal but not yet underway; Exploring = validate before committing; Later = expressly out of gift scope; Done = verified with evidence. Do not treat a proposed concept as implemented.

## Current baseline

This is the one place that records what is merged; update it as things land.

- Portable C11 engine, Python pack compiler, SDL simulator, headless renderer and RP2350 firmware exist.
- WebAssembly host, introspection API, a minimal browser viewer and the pack schema are in main (PR #2).
- RP2350 firmware currently **embeds** a compiled world into the UF2. Independently installing a pack is a later protocol/storage project.
- A full visual Studio and a standalone Companion application do **not** yet exist.
- The existing firmware/breadboard target is Pico 2 + ST7789. The **gift plan instead targets an RP2350 Feather on a custom carrier PCB** with the existing TFT and buttons, and optional RTC. Its new GPIO mapping, battery/backlight budget, PCB and enclosure have **not** yet been validated. See [Feather hardware plan](hardware/feather-carrier-2026.md).

## Priority order through December 18

1. **Gift hardware and build feasibility:** freeze the RP2350 Feather model, TFT/buttons, LiPo power assumptions and new GPIO map; order a custom carrier PCB quickly, prototype enclosure and physical assembly.
2. **End-to-end golden path:** create one world -> compile -> preview -> build/install UF2 -> run reliably on the actual device.
3. **The gift world itself:** one distinct world with finished art.
4. **Basic Studio:** a local dev server plus the Wasm viewer with drag-to-move editing and comment-preserving save, to lay out the gift world faster. Fallback: YAML + `packc --watch` + `mbsim`.
5. **Small engine vertical slice:** capability-aware reachable affordances, visible bird ground-to-perch-to-ground traversal without respawn, and matching contextual animations.
6. **Polish and delivery:** burn-in, documentation, enclosure, spare recovery route.
7. **Everything else** only after the gift is secure.

## Three linked tracks

| Track | Gift deliverable (Now) | After gift |
| --- | --- | --- |
| [Engine & Firmware](roadmaps/engine-firmware.md) | Reliable RP2350 firmware; one embedded custom world; custom PCB/enclosure bring-up; *should:* minimal reachability / locomotion slice | richer animal generation, terrain and weather physics, reusable firmware application, platform abstraction, independent pack storage, second target |
| [Studio](roadmaps/studio.md) | Basic Studio: live rebuild/reload, error banner, drag-to-move places/lanes/obstacles, comment-preserving save | full visual editing, behavior graph, sprite-sheet editor, lighting timeline, budgets, packaging decision |
| [Companion](roadmaps/companion.md) | **Not a gift blocker.** Document manual UF2/BOOTSEL recovery and a future device protocol | dedicated desktop utility: device discovery, settings, safe firmware updates, separate world installation, gallery/sharing |

## Cross-track dependencies and ownership

| ID | Dependency | From -> used by | Gift treatment |
| --- | --- | --- | --- |
| X1 | Valid pack format + Python packc + real engine/Wasm preview | Engine -> Studio | Must work for one custom world; keep format/schema aligned |
| X2 | Basic connected-habitat, reachable-place and locomotion vocabulary | Engine -> pack schema + Studio | Choose minimal contract before building deep behavior editors; avoid overdesign |
| X3 | Build/flash reliable RP2350 UF2 containing a custom world | Firmware + hardware -> Studio gift workflow | Must; manual UF2 install is acceptable |
| X4 | Display geometry, memory/flash budgets, and target configuration | Hardware/Firmware -> Studio validations + eventual Companion | Freeze for gift board; expose known limits |
| X5 | Portable device information, control, storage and update protocol | Firmware -> Companion + Studio hand-off | Define direction; full implementation post-gift |
| X6 | Separate world pack storage, compatibility/version checks and atomic activation | Firmware -> Companion world install | Later; **not required** to give the gift |
| X7 | **Feather carrier PCB**, TFT/button wiring, LiPo/regulator/backlight current budget, USB/BOOT access, enclosure tolerances, optional RTC | Hardware -> Firmware + experience | Critical path; revise Pico GPIO mapping, prototype and order in parallel with software |

## Gift schedule

The week-by-week gates are in [gift-2026.md](gift-2026.md#week-by-week). In short: hardware decided and PCB ordered first, basic Studio while the PCB is made, then the world, bring-up, freeze and burn-in.

## Scope policy

**Must-have** means the gift cannot be called ready without it. **Should-have** is pursued only when the path to must-have is secure. **Stretch** is cut first. **Later** is not worked before delivery except minimal interface notes. See each track for detailed sorting.

The current largest schedule risks are **custom hardware lead time**, **fitting Studio work around the world itself**, and **engine navigation redesign**. Reduce these by ordering hardware early, reusing the existing TFT/buttons but validating the Feather RP2350 pinout and battery power path, keeping Studio to the basic version with a YAML fallback, and implementing at most one observable bird/rabbit vertical slice. Do **not** make Companion, over-the-air updates, marketplace, full actor generation, full terrain physics, or a second MCU prerequisites.

## How decisions evolve

1. Record a user-visible scenario in [Ideas](ideas.md); do not immediately assign a deadline.
2. Ask whether it advances the [Vision](vision.md); prototype uncertain engine boundaries.
3. Promote to a track as Exploring -> Planned -> Now only with acceptance criteria, constraints, and dependency links.
4. Keep one gift schedule in [gift-2026.md](gift-2026.md), not one per track.
5. At each weekly gate, note what's done, what's blocked and what got cut.
6. Log hard-to-reverse architectural decisions in `docs/decisions/` (create it with the first one).
7. Keep the Studio and Companion boundaries explicit; shared code/interfaces are encouraged, duplicate feature ownership is not.
