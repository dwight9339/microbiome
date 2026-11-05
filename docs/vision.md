# Microbiome — Project Vision (draft)

**Status:** draft. This expresses direction, not a promise that every feature will ship.  
**Immediate priority:** gift-ready Microbiome devices by **Friday, December 18, 2026**, one week before Christmas. See [Gift 2026](gift-2026.md) and the [unified roadmap](roadmap.md).

## North star

Microbiome creates small, living, ambient worlds. Individual creatures should feel recognizable; their actions should make sense in relation to their environment; and tiny unscripted moments should reward quiet observation. The experience should feel inviting, calm, and delightful rather than demanding the attention of a traditional game.

The software should make those worlds portable across varied small displays and hardware platforms, approachable to create, and easy to discover and install.

## Audiences and products

| Product track | Primary audience | Promise |
| --- | --- | --- |
| [Engine & Firmware](roadmaps/engine-firmware.md) | World builders, embedded developers, device owners | A predictable, portable, expressive ecosystem that actually runs on a device |
| [Studio](roadmaps/studio.md) | Creators and artists | Make and test a world visually, without needing to understand the engine or manually repeat compile/reload steps |
| [Companion](roadmaps/companion.md) | Device owners | Find, install, configure, update, and enjoy worlds without becoming a firmware developer |

There are **three products**, with shared contracts and a single evolving vision. Studio creates; Companion distributes and manages; Engine/Firmware runs. Device installation and firmware updates belong to Companion, although Studio may offer a "Send to Device" hand-off.

## Experience principles

1. **Alive, not noisy.** Mostly peaceful, believable activity; some social interactions and rare memorable moments. Preserve the ambient feel.
2. **Individuality that can be seen.** Variations in appearance and temperament should produce recognizable differences, not invisible statistics.
3. **The world matters.** Surfaces, terrain, weather, affordances, and reachable spaces affect what an actor can do and how it moves.
4. **Continuous creatures.** A bird landing and returning to a branch is the same bird. Traversal between habitats should not require despawn and respawn.
5. **Emergence over scripts.** Reusable behaviors, capabilities, and affordances should combine into interesting scenes without a bespoke sequence for every animal.
6. **Authors keep control.** Packs remain declarative source assets; preview the real engine; make results reproducible; preserve edits and enable iteration.
7. **Lightweight and portable.** Small predictable memory budgets, no dependency of engine rules on a particular MCU, and graceful capability differences across devices.
8. **Ownership should be simple.** Updating firmware and installing a world should eventually be straightforward and safe, with clear compatibility checks.

## What Microbiome is not trying to be

Not a physics-heavy 3D game, a general artificial intelligence agent framework, or a constantly engaging quest system. Not every world or hardware platform needs every advanced feature. The initial product does not require an account, hosted community service, or wireless connectivity.

## Design boundaries

- **Simulation engine:** portable C11, data-driven rules, deterministic testing where possible; rendering and behavior must stay practical for constrained hardware.
- **Application runtime:** reusable lifecycle, settings, world loading, diagnostics, device protocol; hardware-specific code behind capability-oriented platform services.
- **Pack compiler and format:** one authoritative compiler; explicit version and size constraints; compatibility surfaced to Studio and Companion.
- **Studio:** editor for YAML + art backed by actual engine preview; no second simulation implementation.
- **Companion:** device management, firmware release handling, local/world library and optional sharing; no duplicate authoring engine.

## What success looks like

**Christmas 2026:** finished physical gifts on a custom PCB and enclosure, each showing one distinct world, laid out with a basic Studio if it's ready in time. A small, visible improvement in how actors use their habitat would make it feel more alive, but it's optional. Reliability and gift completion outrank generality.

**After Christmas:** More expressive animal individuality, navigation, terrain, world continuity, and hardware-agnostic firmware; Studio grows into a comfortable creation environment; Companion becomes the friendly interface for devices and community worlds.

This document is the **why**. The [roadmap](roadmap.md) describes **what** and in what order. Architecture and decision records describe **how**.
