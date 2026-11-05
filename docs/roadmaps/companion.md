# Companion Roadmap (draft)

**Product promise:** A separate, approachable desktop utility that helps people own and enjoy Microbiome devices and worlds, whether or not they use Studio.  
**Links:** [Vision](../vision.md) | [Unified roadmap](../roadmap.md) | [Engine/Firmware](engine-firmware.md) | [Studio](studio.md).

## Product boundary

**Companion** owns: device recognition, firmware/update/recovery, settings, installed-world management, local world library, world downloads and eventual sharing/discovery.  
**Studio** owns: authoring and debugging world content, and can offer an export / Send to Companion action.  
**Engine/Firmware** owns: versioned device protocol, capabilities, safe world storage/activation, runtime and firmware-level checks.

This is a distinct application, not a panel embedded in Studio. Desktop-first is the default; browser support is an optional later distribution path.

## Gift 2026 (Now): avoid an app dependency

**No standalone Companion executable is required for December 18.** Instead:
- Keep BOOTSEL/UF2 flash-and-recover instructions reliable and understandable.
- Keep release/build artifacts identifiable (target board, firmware version, embedded world, board/config revision).
- Document the intended portable device-info and management protocol for later; avoid coupling it to USB CDC specifically.
- If there is time, a small local script or GUI wrapper around known-good UF2 copying is optional **and must not delay hardware or Studio**.

The current firmware embeds an MBP world in its UF2. Do not describe independent pack installation as already supported.

## After gift — milestones

| ID | Milestone | Acceptance |
| --- | --- | --- |
| C-1 | **My Devices:** identify connected device/board, firmware and supported capabilities; display clock, storage budget and world details; expose existing configuration controls | User can connect the gift device, see accurate status and change an available setting without opening a terminal |
| C-2 | **Reliable firmware update:** versioned releases, correct board selection, checksum/signature verification for official artifacts, guided update/recovery | Incompatible firmware cannot be inadvertently selected; interruption/recovery is documented and tested |
| C-3 | **Local World Library:** browse/import local MBP+metadata, preview thumbnails, inspect attribution/compatibility and locally organize worlds | User can select a compatible downloaded world without modifying source YAML |
| C-4 | **Separate world installation:** transfer, verify and activate MBP without reflashing engine firmware; progress, cancel/failure recovery and safe rollback | Depends on Engine E-P3/E-P4, device pack storage, stable transfer protocol |
| C-5 | **Discover & Share:** browse downloadable worlds, publish/share metadata and screenshots; optionally support online catalog and ratings | Requires packaging/versioning, trust/moderation, attribution and distribution policy decisions |

## Cross-track contracts

| Dependency | Produced by | Used by |
| --- | --- | --- |
| Device identity, hardware revision, display/power capabilities, limits | Engine/Firmware | Companion selection and compatibility UI; Studio targeting |
| Versioned firmware management protocol, safe bootloader mode | Firmware | Companion firmware updates |
| MBP package metadata, engine/pack compatibility requirements, preview assets | packc/Studio + Engine | Companion library and discovery |
| Transactional pack staging/validation/activation | Firmware storage layer | Companion one-click world install |
| Export / Send to Companion | Studio | Companion installation flow |

**Important early design distinction:** Updating **firmware** (bootloader/recovery-sensitive) is not the same operation as installing a **world** (content pack). Keep their UX, authorization and failure handling separate.

## Technical direction (explore, not decide yet)

- Native desktop shell such as Tauri may be reused, but independent application state and releases should remain possible.
- Use a transport-independent protocol with version negotiation; USB CDC first, BLE/Wi-Fi only when hardware supports them.
- Identify boards by a stable device profile rather than only USB identifiers.
- Expect users to be offline. Local files, local firmware artifacts and manual recovery must keep working.
- Require integrity checks and compatibility checks; only enable unattended firmware updates after proving a safe recovery path.
- Clarify community moderation, licensing, content trust and attribution before launching a hosted sharing service.

## Explicit non-goals

No full world editor, no alternative pack compiler, no separate simulation engine, and no forced account/cloud dependency for local management.
