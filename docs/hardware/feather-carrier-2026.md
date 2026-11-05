# Gift 2026 — Feather RP2350 Carrier Board (draft)

**Status:** proposed hardware plan. Confirm the exact Feather variant, prototype component models and wiring, display module, and battery/power decisions **before layout/fabrication**.  
**Milestone and dates:** [Gift 2026](../gift-2026.md).  
**Track:** [Engine & Firmware](../roadmaps/engine-firmware.md).

## Intended architecture

Make a **custom carrier/FeatherWing-like PCB** that replaces prototype breadboard wires with appropriate footprints, connectors, mounting points and routing. Plug an off-the-shelf **RP2350-based Feather** into the board, rather than populating a bare RP2350 MCU and designing USB, regulator, flash and LiPo charger from scratch.

- **Compute/USB/power:** tentatively the **Adafruit Feather RP2350 with HSTX**. Confirm actual SKU, flash/PSRAM option, pin locations and height.
- **Display:** reuse the known-working SPI TFT module (currently ST7789 in firmware), with correct module footprint or a keyed connector and mounting alignment.
- **Controls:** physical buttons already used on the breadboard, ideally board-mounted where they align with the enclosure. Preserve all six logical inputs if practicable.
- **Time:** optional RTC IC or module on I²C; preserve a footprint/pads for it, but don't let an untested RTC become a gift dependency. DS3231 or existing proven module is an option, not yet selected.
- **Battery:** use the **Feather's existing LiPo connector and charger** for a compatible protected single-cell rechargeable pack. Do not design a second charger or route raw battery charging on the carrier. Support USB operation independently of having a battery installed.
- **Enclosure:** custom case holds the display, switches, Feather/carrier and battery with USB-C, BOOT/RESET and service access. Design physical mockups early.

The carrier is **custom PCB work** even though the RP2350 and power-management circuits live on the replaceable Feather. A working device must still meet the functional and fit tests.

## Important pin and firmware changes from Pico 2

Do not reuse today's `platform/rp2350/board_config.h` GPIO numbers without checking the Feather pinout; it was designed for a **Pico 2**, not a Feather:

| Function | Current Pico 2 wiring | Feather RP2350 HSTX direction — **illustrative, not final pin assignment** |
| --- | --- | --- |
| Display SPI | `spi0` SCK GPIO18, MOSI GPIO19 | Prefer Feather **header SPI0 SCK GPIO22 / MOSI GPIO23**; GPIO18/19 are on the HSTX connector rather than ordinary headers |
| Display CS/DC/RST/backlight | GPIO17/16/20/21 | Allocate accessible spare header GPIOs; **GPIO21 is used by onboard NeoPixel**, so avoid it for backlight by default |
| Buttons | GPIO2–7 | Reassign to available inputs; **GPIO2/3 are Feather's standard SDA/SCL I²C pins** and should remain free for RTC/STEMMA |
| RTC | `i2c0`, GPIO8/9 | Favor `i2c1` **GPIO2/3** if using Feather's default I²C/STEMMA wiring |
| Flash and board build | `PICO_BOARD=pico2` with the Pico's memory assumptions | Pico SDK provides `PICO_BOARD=adafruit_feather_rp2350` for this exact Adafruit board; use a target-specific configuration rather than globally replacing Pico 2 support |

The listed Feather variant includes **8 MB external QSPI flash** (versus 4 MB on the current Pico 2), but **usable world capacity must still be measured** after firmware and reserved sectors; don't automatically assume all flash is available. Some Feather variants have PSRAM; **GPIO8 is reserved as PSRAM chip select** when fitted.

**Pin allocation action:** prepare a signed-off schematic/pin table for the exact display PCB, GPIO/peripheral mux, number of buttons, optional RTC, any sensor, backlight PWM and power switch. Confirm accessible headers, pin conflicts, input pull-ups, cable orientation, routing, socket footprint and board clearance before release.

## Battery and power details to validate

The official Feather documentation states a **3.3 V regulator rated 500 mA peak**, but warns it should not be operated continuously at that peak when fed from USB. Measure actual current on USB and battery with TFT backlight at maximum, and verify thermal behavior and usable battery runtime. TFT breakout boards differ in regulator/backlight wiring; confirm the specific module supports the intended 3.3 V signals and supply. Do **not** power a 5 V-only module directly from an arbitrary pin.

- Battery must have the correct chemistry, protection, size and JST polarity for the Feather's charger; never connect alkaline, NiMH, a multi-cell pack or a reversed JST. Select a cell appropriate for the Feather's charge current and leave safe space/strain relief in the enclosure.
- Use the Feather's USB-C and charger, not an added parallel charger. Test USB-only, battery-only, charging while the display runs, unplug/replug behavior and off-state.
- Consider an accessible **EN-based power switch**; the Feather's EN pin disables the 3.3 V regulator, not the BAT/USB rails. Verify the intended off/charge interaction physically.
- This Feather **does not include battery-voltage ADC monitoring** by default. If wanted, add an appropriate divider/ADC circuit and account for idle power; battery percentage estimation is later.
- If the goal is a portable gift, specify and test minimum desired runtime; don't promise one before measuring the final display/backlight consumption.
- Optional RTC backup needs a separate design decision: a calendar clock should survive shutdown if date continuity is important, but don't complicate the gift power path solely for that.

## Enclosure and assembly checklist

- Match Feather header layout and the real Feather board dimensions, USB-C port position, reset/boot buttons and battery JST orientation; socketing the Feather is preferable to permanent soldering for the first gift.
- Check screen window opening, active-area centering, physical orientation, button height/travel, switch access, strain relief, screw standoffs, PCB thickness and battery clearance.
- Plan a printed fit-check before PCB arrival, and print another prototype once the board arrives; keep USB recovery accessible without opening destructive seals.
- Add clear silkscreen labels and assembly notes. Test all six inputs and a cold-start after enclosure assembly.

## Hardware checkpoints

The dates for these are in the [gift plan](../gift-2026.md#week-by-week).

- **Before ordering the PCB:** Feather variant, TFT model and dimensions, breadboard inventory, battery requirement, button count and placement, RTC decision, pin map and PCB form factor confirmed; Feather-target firmware boots and draws on temporary wiring; enclosure mockup fit-checked.
- **First board back:** carrier assembled and tested with the TFT, all buttons, USB and battery; RTC only if fitted.
- **Freeze:** full enclosure assembled; charging and thermal tests, boot/flash recovery and soak done.
- **Gift-ready:** hardware finished and a known-good UF2/pack archived.

## Sources / specifications to review before manufacture

- Adafruit Feather RP2350 pinouts: https://learn.adafruit.com/adafruit-feather-rp2350/pinouts
- Feather RP2350 power management: https://learn.adafruit.com/adafruit-feather-rp2350/power-management
- Existing Pico SDK Feather board definition: https://github.com/raspberrypi/pico-sdk/blob/master/src/boards/include/boards/adafruit_feather_rp2350.h
- Feather PCB / mechanical reference: https://github.com/adafruit/Adafruit-Feather-RP2350-PCB
- Feather format/mechanical specifications: https://learn.adafruit.com/adafruit-feather/feather-specification

**Open question:** which exact Feather RP2350 and TFT module are already on hand? This draft intentionally does **not** approve a final electrical schematic or footprint.
