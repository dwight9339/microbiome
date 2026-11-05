# Engine & Firmware Roadmap (draft)

**Owner:** Engine/Firmware track; PCB and mechanical execution are part of the same Christmas integration plan, not a fourth software product.  
**Shared deadline:** [Gift 2026](../gift-2026.md) — Dec 18, 2026.  
**Context:** [Vision](../vision.md) | [Unified roadmap](../roadmap.md) | [Studio](studio.md) | [Companion](companion.md).

## Current implementation

Portable C11 engine; YAML/PNG -> Python pack compiler -> MBP; clock, weather, actor state machines, spawn rules, tags, affordances, single-lane actors, obstacle avoidance, layer/particle rendering, SDL/headless/RP2350 hosts. The WebAssembly host, introspection, and schema are in open studio-phase0 PR #2 (as of Nov 4). Current actors use a fixed lane and render between background and front scenery; affordance lookup does not check actor reachability. Firmware links the pack into its UF2.

## GIFT 2026 — Now

| ID | Delivery | Acceptance evidence | Dependencies |
| --- | --- | --- | --- |
| E-G1 | Tested **Feather RP2350** firmware target and reproducible single-world UF2 build | Existing TFT and buttons work on the Feather with newly mapped SPI/GPIO; known-good custom pack boots; timing, color, flash and power checked | Current core/packc, new Feather board config and H-G1 |
| E-G2 | **Capability-aware place selection** for one small habitat example | Rabbit cannot select an elevated perch; bird can choose accessible ground/perch targets; no unreachable-target loops | Pack compiler/schema + minimal traversal vocabulary; Studio needs resulting fields |
| E-G3 | **Continuous bird traversal** between perch and ground | One actor ID persists as bird flies, lands, hops, takes off and perches; on-screen transition visible; deterministic test scenario | E-G2 + location/link model |
| E-G4 | **Contextual locomotion/animation** for that demo | Flight, landing and hopping/climbing visual changes trigger from surface/mode rather than separate despawned archetypes | E-G3 + sprite/art support |
| H-G1 | Freeze exact **RP2350 Feather** variant, SPI TFT/button wiring and power/battery/optional RTC plan | Tested Feather GPIO/pin map; reviewed carrier and parts list; PCB fab ordered by Nov 13 | Gift target decision by Nov 6; [hardware checklist](../hardware/feather-carrier-2026.md) |
| H-G2 | Custom **Feather carrier** + enclosure bring-up | Assembled carrier, display, all buttons, USB-C/BOOT and battery charge/power tested; enclosure assembled, measured current, final soak and recovery | H-G1 + lead times; Feather-target firmware E-G1 |
| E-G5 | Preserve compatibility and diagnostics | Old forest pack still works; CI smoke/parity pass; regression covers reachability, transitions and format version | E-G2–E-G4 |

**Architecture allowance:** Use a narrow route graph or explicit links between ground and branches. A complete terrain engine, generalized multi-plane parallax and actor genetics **must not block** delivery. Prefer the simplest pack encoding consistent with future expansion; document pack-version changes.

**Gift hardware target:** a custom **carrier PCB for an RP2350-based Feather**, with footprints/connectors for the proven SPI TFT and physical buttons, a custom enclosure, and an **optional** RTC. Reuse the Feather's integrated USB-C and LiPo charging rather than designing a second charger. The existing Pico 2 pin map is not compatible without changes; use the Pico SDK Feather board target and a new device-specific configuration. Validate TFT/backlight power, LiPo wiring and thermal behavior. See [Feather carrier plan](../hardware/feather-carrier-2026.md). Keep the embedded-pack UF2 and BOOTSEL flow for the gift.

## Should / stretch for gift

- **Should:** seeded reproducibility and test harness for bird/perch behavior; read-only inspector indicating current navigation surface and locomotion mode; safe fallback when no reachable affordance exists.
- **Stretch:** simple spawn-time visual variants (e.g., palette or sprite choice), raccoon climbing as a second locomotion case, an icy patch with rare stumble; only if core path and hardware are already green.
- **Deferred:** full procedural genetics, dynamic weather traction/physics, persistence, joint social AI and long-lived ecology; don't commit to these until the small vertical slice is proven.

## After Christmas — Portable Device Platform

| Stage | Objective and definition |
| --- | --- |
| E-P1 | **Reusable application runtime:** extract startup, world lifetime, settings, serial command handling and frame orchestration from RP2350 main into reusable application code; MCU SDK calls stay in board/platform layers. |
| E-P2 | **Capability-oriented platform interface:** display-present strips/framebuffer, logical inputs, clocks, sensors, storage, communication, optional power/brightness and update services; explicitly handle unsupported features and different refresh characteristics. |
| E-P3 | **Device profile and compatibility contract:** device ID, hardware revision, geometry, color/refresh capabilities, memory/flash/world capacity, engine/pack version, available transports; advertise over a versioned device protocol and consume in Studio/Companion. |
| E-P4 | **Independent pack lifecycle:** reserved storage/slots for MBP, integrity and format validation, safe install/activate/rollback/recovery, clearly separated from firmware update; proof includes interrupted transfer. |
| E-P5 | **Second genuinely different hardware target:** implement platform services and board configuration without modifying simulation or portable app logic; measure RAM/flash, FPS/latency, power and user-facing feature differences. |

Avoid declaring a fully hardware-agnostic HAL before it has been tested on a second real target. The core engine is already portable; the **device application** is the missing reusable layer.

## After Christmas — Living World Systems

- **Connected habitats and traversal:** explicit reachable places/surfaces and links; actor abilities and route costs; continuous movement among ground, branches, air, water and shelters.
- **Surface/material effects:** traction, climbability, support/width, water, snow, ice, mud and weather modifiers; drive contextual animations, speeds and occasional slips without full physics.
- **Individual actors:** stable identity plus observable traits, visual variants, preferences and locomotion proficiency; add persistence later only when backed by a reliable device storage plan.
- **Interactions and world history:** multi-actor events, occupancy, rare occurrences, seasonal changes and optional state persistence.
- **Rendering/content budgets:** compression, palette banks, display-specific optimizations and profiling as data warrants.

## Contracts and risks

- New engine fields require pack_format.h, packc, schema, versioning, tests and corresponding Studio support together; maintain a CLI/manual escape hatch where necessary.
- The actor's **goal**, **route**, **locomotion mode**, and **animation** should become separable. A scenery image layer is not automatically a navigable habitat.
- Runtime budgets on actual target hardware determine the generality of pathfinding, actor genetics and weather simulation.
- Firmware/version/device protocol are shared with [Companion](companion.md); **Companion owns the user-facing flashing and device-manager experience**, not the engine.
