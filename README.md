# microbiome

An embedded ecosystem: a small, mostly-ambient world drawn on a little screen,
where animals wander in and out according to time of day, season, weather and
chance, and react to each other. You pan around it with arrow buttons; when you
stop, the camera drifts after whatever is interesting.

The engine knows nothing about forests or deer. Everything in a scene (art,
layers, animals, their behaviour, spawn rules, lighting, weather) is
declarative data in a **pack** (`packs/<name>/pack.yaml` + PNGs), compiled
to a compact binary blob that the engine reads in place from flash.

```
 packs/forest/pack.yaml ─┐
 packs/forest/art/*.png ─┴─ packc ──> forest.mbp ──> engine core (C11, no heap)
                                                       ├─ platform/sdl       desktop simulator
                                                       ├─ platform/headless  PNG snapshots / tests
                                                       └─ platform/rp2350    Pico 2 + ST7789 firmware
```

## Quick start (desktop)

Needs CMake ≥ 3.16, a C compiler, Python 3 with `pyyaml` and `pillow`, and
SDL2 for the simulator (`apt install libsdl2-dev` / `brew install sdl2`).

```sh
pip install pyyaml pillow
cmake -B build && cmake --build build
./build/mbsim build/forest.mbp            # 320x240 at 3x zoom
./build/mbsim build/forest.mbp --size 480x480
```

| Key | Action |
| --- | --- |
| arrows | pan |
| Z / X | A / B buttons (not used by the forest pack yet) |
| T | time speed 1× → 60× → 600× |
| `[` / `]` | hour −/+ |
| W | cycle weather (pinned) → back to automatic |
| F1 | debug overlay: actor boxes, places, lanes; states in title bar |
| R | reload pack |

**Iterating on content.** Run the compiler in watch mode next to the
simulator. Every save of `pack.yaml` or a PNG rebuilds the pack, and `mbsim`
hot-reloads it, keeping camera and time:

```sh
python3 tools/packc/packc.py packs/forest -o build/forest.mbp --watch
```

**Snapshots without a display** (art review, CI, agents):

```sh
./build/mbsnap build/forest.mbp night.png --time 22:30 --set weather=clear --sim 120
./build/mbsnap build/forest.mbp pano.png --panorama          # whole world, unscrolled
./build/mbsnap build/forest.mbp shot.png --frames 20 --every 100   # animation strip
./build/mbsnap build/forest.mbp - --info --sim 600            # print vars + actors
```

`ctest --test-dir build` runs smoke tests.

## Firmware (RP2350 / Pico 2)

```sh
git clone -b 2.1.1 https://github.com/raspberrypi/pico-sdk && (cd pico-sdk && git submodule update --init lib/tinyusb)
export PICO_SDK_PATH=$PWD/pico-sdk
cmake -S platform/rp2350 -B build-rp2350          # add -DMB_RTC_DS3231=ON if you fit an RTC
cmake --build build-rp2350
# hold BOOTSEL, plug in the Pico 2, copy build-rp2350/microbiome.uf2 to the drive
```

Default wiring for a 2.0" 240×320 ST7789V module (all pins in
`platform/rp2350/board_config.h`):

| LCD pin | Pico 2 | | Button | Pico 2 |
| --- | --- | --- | --- | --- |
| VCC | 3V3 (36) | | left | GP2 |
| GND | GND | | right | GP3 |
| SCL | GP18 | | up | GP4 |
| SDA | GP19 | | down | GP5 |
| RES | GP20 | | A | GP6 |
| DC | GP16 | | B | GP7 |
| CS | GP17 | | | (buttons to GND) |
| BLK | GP21 | | | |

If the picture is mirrored, rotated or has inverted colours, change
`LCD_MADCTL` / `LCD_INVERT` in `board_config.h`. The USB serial console takes
`time 07:30`, `date 2026-12-25`, `speed 60`, `set weather precip`, `stats`.

## Repository layout

| Path | What |
| --- | --- |
| `core/` | The engine: pack loader/validator, clock & world variables, actors, particles, strip renderer. Portable C11, no allocation, no platform code. |
| `core/include/mb/pack_format.h` | The binary pack format (the contract between `packc` and the engine). |
| `tools/packc/packc.py` | Pack compiler: YAML + PNG → `.mbp`. |
| `tools/placeholder_art.py` | Generates the current placeholder forest art. |
| `packs/forest/` | The forest pack. |
| `platform/sdl/` | Desktop simulator with hot reload. |
| `platform/headless/` | `mbsnap` PNG renderer. |
| `platform/rp2350/` | Pico 2 firmware: ST7789 DMA driver, buttons, optional DS3231, serial console. |
| `docs/architecture.md` | How the engine works and how to port it. |
| `docs/pack-format.md` | Writing packs: the YAML schema, behaviours, and art requirements. |
