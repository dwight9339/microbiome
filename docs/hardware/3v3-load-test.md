# 3V3 rail load test (prototype 1, breadboard)

**Goal:** before the carrier PCB is finalized, find out whether the Feather's 3.3 V regulator can carry everything prototype 1 puts on it, and how much the rail moves under audio load.
**Hardware under test:** Feather RP2350, MSP3223 3.2" ILI9341 module and a PAM8302 breakout on a breadboard. Pins match the `hardware/prototype_1` schematic.
**Context:** [Feather carrier plan](feather-carrier-2026.md#battery-and-power-details-to-validate).

> **Outcome (2026-11-19): prototype 1 PCB ordered with the amp on 3V3, unchanged.** Tables A–C were measured with long breadboard power wiring. After the display and amp power connections were shortened, most of the sag and the stage 8 backlight flicker disappeared. Those later readings weren't recorded, so the tables are pessimistic. The 10-minute stage 7 soak on USB passed: no reset, die temperature levelled off at about +15–17.5 °C. The stage 8 scope measurements weren't taken. **When the boards arrive,** repeat stages 7–8 and the soak on the real PCB, measuring at the test points (MCU_3V3, DISP_3V3, AMP_3V3 and their GND pads). If any flicker remains, cap the volume in firmware (fix 1 below).
>
> **Pins changed after this test.** The breadboard used audio PWM on GP7 and amp SD on GP11. On the ordered board they are GP11 and GP10. The firmware (`boards/feather_proto1.h`) now follows the ordered board, so rewire the breadboard before reusing it.

## What is on 3V3

From the prototype 1 schematic netlist, the Feather's single `3V3` pin feeds:

| Load | Notes |
| --- | --- |
| RP2350, QSPI flash, USB PHY, onboard LED/NeoPixel | inside the Feather |
| MSP3223 display module VCC (J2.1) | module has its own LDO and backlight transistor; backlight switched by `TFT_BL` (GP4) |
| **PAM8302 VDD (U2.6)** | class-D, bridge-tied output; the largest and most variable load |
| DS3231M VCC, 2× 4.7k I²C pull-ups | negligible |
| C5 10 µF, C6 1 µF, C7 100 nF | the only bulk capacitance on the carrier |

The Feather's regulator is rated 500 mA **peak**, not continuous. Rough amp numbers, assuming an 8 Ω speaker on 3.3 V:

- clipped square wave: about VDD / R_L, so up to **~0.4 A** drawn by the amp alone;
- full-scale sine: about 0.7 W ideal into the speaker, so ~0.25 A average with peaks near 0.4 A;
- quiescent (enabled, silent): a few mA.

Add the backlight and the RP2350, and the worst case can exceed the regulator's peak rating. The test checks that number.

## Load-test firmware

`microbiome_loadtest.uf2` steps through fixed load stages and holds each one. It also shows the stage number in large digits at the top-left of the LCD, so it works on battery without USB.

```sh
cmake -S platform/rp2350 -B build-feather -G Ninja \
      -DPICO_BOARD=adafruit_feather_rp2350 -DMB_BOARD_CONFIG=boards/feather_proto1.h
cmake --build build-feather        # -> build-feather/microbiome_loadtest.uf2
```

CI uploads the same file in the `microbiome-firmware` artifact.

| # | Stage | Isolates |
| --- | --- | --- |
| 0 | baseline: backlight off, LCD idle, amp shutdown | MCU + display logic |
| 1 | backlight 100% | backlight current (1 − 0) |
| 2 | + LCD streaming full frames | SPI/DMA + controller (2 − 1) |
| 3 | + core1 busy | second core (3 − 2) |
| 4 | + amp enabled, silent | amp quiescent (4 − 3) |
| 5 | + 1 kHz sine 25% | moderate audio |
| 6 | + 1 kHz sine 100% | loud audio |
| 7 | + 200 Hz square 100% | **steady-state worst case** |
| 8 | + 200 Hz square 100%, 100 ms on / 400 ms off | **load-step transients** |
| 9 | amp only: 200 Hz square 100% | amp current with everything else off |

- **Buttons** (only these four are used): right (GP20) goes to the next stage and center (GP22) to the previous one, both wrapping around. Up (GP1) and down (GP23) change the audio level by 10%. The automatic sequence is started from serial with `auto`.
- **USB serial** (any terminal): `help`, `stage N`, `auto [s]`, `hold`, `status`. You can also set each load directly: `bl`, `lcd`, `cpu`, `amp`, `tone sine|square|none FREQ PCT`, `burst ON OFF`. Every stage change prints a timestamped line, so the log lines up with meter readings.
- **Scope trigger, GP26 (silkscreen A0):** high during each burst in stage 8; in continuous tones it toggles once per wave period. Edges line up with the load step to within a few µs.
- **Lockups:** a 3 s watchdog runs. After a lockup, the next boot prints `WATCHDOG TIMEOUT … in stage N`. Every boot prints the last reset cause, including brown-out.

The RP2350 brown-out detector watches its 1.1 V core supply, not 3V3, so a 3V3 sag usually shows up as a hang or garbage first. Typical causes are flash reads failing below ~2.7 V or the display controller glitching. Check the display and the serial log in every stage, not just the reset cause.

## Before you start

- **Match the PCB, not the breakout.** The Adafruit PAM8302 breakout pulls SD high and has a volume trim pot. The firmware drives SD either way. Turn the pot fully up for the worst case. Fit 10 µF + 100 nF across 3V3/GND at the amp VDD to stand in for C5–C7, unless the breakout already has equivalent capacitance.
- **Protect the speaker.** Stages 7–9 put roughly 1 W or more of square wave into it. For long or soak runs, use an 8 Ω resistor rated ≥ 2 W in place of the speaker, or keep dwell times short. Stage 7 is also very loud.
- **Watch the meter's burden voltage.** A DMM on its mA range has several ohms of shunt resistance. At 400 mA that alone drops more than a volt and invalidates the test. Use the A/10 A range, or a 0.1 Ω shunt read with a second meter or the scope.
- Note the speaker's actual impedance. A 4 Ω speaker roughly doubles the amp current.

## Measurement points

| Quantity | How |
| --- | --- |
| Total input current | USB-C inline power meter (5 V side), or a meter in series with the battery lead (JST pigtail) |
| External 3V3 load current | shunt or A-range meter between the Feather `3V3` pin and the breadboard 3V3 rail |
| V(3V3) at source | DMM at the Feather `3V3` pin to GND pin |
| V(3V3) at load | DMM at PAM8302 VDD and at display VCC (the breadboard wiring drop is pessimistic compared with the PCB) |
| Ripple / droop | scope at PAM8302 VDD, short ground spring, 20 MHz bandwidth limit; ch2 on GP26 (A0) as trigger |
| Regulator temperature | thermocouple or IR on the Feather regulator after 10 min in stage 7 |

**Which reading to record.** Record the **average (DC)** reading for every current and voltage in the tables: meters set to DC current, not AC or true RMS. Average current is what heats the linear regulator and drains the battery. Peaks show up as voltage droop, so take them from scope captures. Per stage:

- Stages 0–4 draw nearly steady current, so the average is the whole story.
- Sine stages (5–6): the supply current follows the tone and averages about 0.64 × peak, so peak ≈ average × π/2.
- Stage 7 (clipped square): the speaker current only reverses direction, so supply current is almost constant and its average ≈ peak. Use it as the worst-case current.
- Stage 8 (bursts): an averaging meter shows about 20% of the on-current, which is misleading. Read the on-current from stage 7 and the droop from the scope.
- With no battery fitted, the USB meter's current is close to the total 3V3 current, since a linear regulator draws about the same current in as out. USB current minus the external 3V3 current approximates the Feather's own draw.

**Peak current (optional).** Put a 0.1 Ω shunt in the ground return of the external loads, between the breadboard GND rail and the Feather GND pin. Scope across it with the probe ground on the Feather GND side: 0.1 V per amp. Don't scope a shunt in the 3V3 line on a scope whose channels share a ground, such as the FNIRSI 2C53T. A second probe clipped to GND would short 3V3 through the scope.

## Procedure

1. **USB only, no battery.** Run `auto 15`. Record every stage in table A.
2. **Battery only, charged (~4.0 V+).** Unplug USB and use the buttons. Record table B.
3. **Battery low (~3.5 V).** Replace the battery with a bench supply on the JST at 3.5 V, current limit about 1 A. **USB must stay unplugged**, because the charger would try to charge the supply. Record stages 0, 4, 7 and 8 in table C. Here the regulator runs close to dropout. This is the likeliest real failure: a gift running on a half-flat cell.
4. **Transient (stage 8).** Scope the 3V3 at the amp VDD and the display VCC, triggered on GP26 (A0). Record the minimum voltage and how long it takes to recover, on USB and at 3.5 V.
5. **Soak.** Hold stage 7 for 10 minutes on USB and on battery, using a dummy load in place of the speaker. Record any resets or display glitches, and whether the 3V3 reading drifts down over the 10 minutes. A regulator nearing thermal shutdown folds back or drops out, so 10 minutes with a flat rail and no reset is a pass on its own. For a temperature trend, type `vref <your stage-7 3V3 reading>` then `log 30` over serial right after entering stage 7. Each line prints the RP2350 die temperature and its rise since the stage started. The die sits a few mm from the regulator and reads cooler than it, so treat the rise as a lower bound. If you have an IR or thermocouple thermometer, use it on the regulator as well, with a bit of tape over the part for IR.
6. **Noise (listening).** In stage 4 (amp on and silent, LCD streaming), listen for SPI or frame-rate whine from the speaker. Rail coupling here is a layout problem the PCB can still fix.

### A. USB, no battery

| # | I USB (5 V) | I 3V3 ext | V 3V3 @ Feather | V 3V3 @ amp | V 3V3 @ display | display OK / notes |
| --- | --- | --- | --- | --- | --- | --- |
| 0 |<1 mA|<1 mA|3.285|3.281|3.281|off|
| 1 |72 mA|64 mA|3.278|3.258|3.252|ok|
| 2 |88 mA|69.5 mA|3.277|3.255|3.249|ok|
| 3 |88 mA|69.5 mA|"|"|"|ok|
| 4 |104 mA|75.1 mA|3.276|"|"|ok|
| 5 |250 mA|220 mA|3.248|3.166|3.232|ok|
| 6 |322 mA|295 mA|3.23|3.12|3.151|ok|
| 7 |349 mA|321 mA|3.224|3.102|3.129|ok|
| 8 |100 mA|75 mA|3.26-3.28|3.19-3.2|3.16-3.25|backlight flickers|
| 9 |303 mA|287 mA|3.235|3.126|3.158|off|

### B. Battery, charged (V_bat = ____ V)

| # | I bat | I 3V3 ext | V 3V3 @ Feather | V 3V3 @ amp | V 3V3 @ display | display OK / notes |
| --- | --- | --- | --- | --- | --- | --- |
| 0 |24 mA|7 mA|3.282|3.28|3.279|off|
| 1 |81 mA|62 mA|3.264|3.245|3.238|ok|
| 2 |97 mA|67.7 mA|3.262|3.242|3.233|ok|
| 3 |99.8 mA|67.7 mA|"|"|"|ok|
| 4 |105.7 mA|73.3 mA|3.26|3.237|3.229|ok|
| 5 |262 mA|217 mA|3.2|3.116|3.129|ok|
| 6 |340 mA|291 mA|3.168|3.049|3.075|ok|
| 7 |364 mA|316.5 mA|3.158|3.029|3.054|ok|
| 8 |106-241 mA|73-200 mA|3.23-3.25|3.12-3.24|3.13-3.23|backlight flickers|
| 9 |312 mA|286 mA|3.173|3.052|3.092|off|

### C. Bench supply at 3.5 V on BAT

| # | I in | V 3V3 @ Feather | V 3V3 @ amp | notes |
| --- | --- | --- | --- | --- |
| 0 |21 mA|3.279|3.28|off|
| 4 |99 mA|3.27|3.252|ok|
| 7 |340 mA|3.11|3.039|ok|
| 8 |99-219 mA|3.187-3.271|3.143-3.257|backlight flickers|

### Transient / soak

| Condition | V min (stage 8) | recovery | ripple p-p (stage 7) | 3V3 drift / die rise over 10 min | resets / glitches |
| --- | --- | --- | --- | --- | --- |
| USB | | | | 3V3: ____ / die +17.5 °C, plateau after ~8 min (first reading taken before `vref` was set: true rise ≈ +15 °C) | no reset or watchdog in 666 s (uptime continuous) |
| battery charged | | | | | |
| 3.5 V bench | | | | — | |

## Proposed pass criteria

These are proposals; adjust them before testing.

- V(3V3) ≥ 3.20 V DC at the display in every stage.
- Transient minimum ≥ 3.0 V at both the amp and the display. Ripple under 100 mV p-p.
- No resets, watchdog timeouts, display corruption or USB drop-outs, including at 3.5 V input.
- Sustained 3V3 load comfortably below the regulator's 500 mA peak rating, with the regulator not uncomfortably hot after 10 minutes.

## If it fails: PCB options, cheapest first

1. **Cap the volume in firmware.** Find the highest level that passes with the up/down buttons and make it the firmware maximum. No hardware change.
2. **Limit it in hardware.** Increase the R6/R7 input attenuation so full-scale PWM can't drive the amp into clipping, which also limits the worst-case current.
3. **Add bulk capacitance at the amp VDD** (e.g. 100–220 µF low-ESR). This helps burst transients (stage 8) but not steady load (stage 7).
4. **Take the amp off 3V3.** The PAM8302 runs from 2.0–5.5 V. Feeding it from VBAT/VBUS (e.g. through a Schottky OR) removes it from the regulator entirely. This is a bigger schematic change. First check how the Feather's BAT pin behaves on USB with no battery.
5. **Add a VBAT divider to a spare ADC pin.** GP26 and GP27 are unconnected. The divider lets firmware lower the volume as the battery runs down, if the 3.5 V results are the problem.
