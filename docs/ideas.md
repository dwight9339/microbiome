# Microbiome — Ideas and Discovery Backlog (draft)

This is a **non-committed idea garden**, not the delivery roadmap. The [vision](vision.md) guides choices; the [unified roadmap](roadmap.md) and [Gift 2026 plan](gift-2026.md) control delivery.

## Promising ideas (after the gift unless promoted)

| Theme | Idea | Question / proof to seek |
| --- | --- | --- |
| Actor individuality | Generated coats, sprite variants, temperaments, preferences and recognizable identities | Can two animals of one species be visibly different without large pack/flash cost? |
| Living world | Persistent actor identities, simple routines, seasonal changes, environmental memory | What minimum saved state feels meaningful without fragile flash writes? |
| Habitat navigation | Connected ground/tree/air/water routes, body-size and ability restrictions, shared affordances | Can bird, raccoon and rabbit behave correctly in one small scene with bounded navigation cost? |
| Terrain | Ground/branch/ice/snow/mud, animation overrides, traction and occasional slips | Can one icy patch yield expressive behavior without full physics? |
| Social interaction | Short coordinated events, perch occupancy, family routines and rare discoveries | What looks like an interaction while staying ambient and cheap? |
| User interaction | Gentle A/B actions: food, light, bell, weather | Does interaction deepen attachment without turning the world into a demanding game? |
| Delivery | Independent MBP install, versioned packs, world gallery and sharing | What compatibility/recovery contract is needed first? |
| Hardware | Reusable firmware runtime, device profiles, second target, low-refresh display mode | Can the same device application run on genuinely different boards? |

## Promotion rule

For each idea, capture: **moment a viewer/creator should experience; small test scene; constraints; affected tracks; dependencies; acceptance criteria; what is cut if it enters a fixed milestone.** Move it into a track roadmap only after the architecture is credible and a delivery slot exists. If it needs more experimentation, keep it here rather than pretending it is scheduled.

## Anti-scope-creep rule for Christmas 2026

No new idea displaces the custom PCB/enclosure, the gift world or reliable firmware. If one seems worth it, it replaces something already planned rather than adding to the list. "Interesting" is not the same as "needed by December 18."

## Decision log convention

For irreversible architectural choices, add a short note in `docs/decisions/` (create it with the first one) with: context, options, decision, reasons, consequences, and date. Avoid encoding speculative plans as permanent decisions.
