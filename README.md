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
                                                       ├─ platform/wasm      browser viewer, future studio
                                                       └─ platform/rp2350    Pico 2 + ST7789 firmware
```

## Quick start (desktop)

You need CMake ≥ 3.16, a C compiler, Python 3 with PyYAML and Pillow, and
SDL2 for the simulator. Without SDL2 everything else still builds; CMake
just skips `mbsim`.

### Ubuntu / Debian

```sh
sudo apt install build-essential cmake libsdl2-dev python3-yaml python3-pil
cmake -B build && cmake --build build
./build/mbsim build/forest.mbp                 # 320x240 at 3x zoom
./build/mbsim build/forest.mbp --size 480x480
ctest --test-dir build
```

(On Ubuntu 23.04+ a bare `pip install` is refused, so use the apt packages
above, or a venv: `python3 -m venv .venv && .venv/bin/pip install pyyaml pillow`,
then configure with `-DPython3_EXECUTABLE=$PWD/.venv/bin/python`.)

### macOS

```sh
brew install cmake sdl2 && pip3 install pyyaml pillow
cmake -B build && cmake --build build && ./build/mbsim build/forest.mbp
```

### Windows (Visual Studio 2022)

1. Install **Visual Studio 2022** with "Desktop development with C++" (it includes CMake),
   and **Python 3** from python.org (tick "Add to PATH"). Then `pip install pyyaml pillow`.
2. Download `SDL2-devel-2.30.x-VC.zip` from the
   [SDL releases page](https://github.com/libsdl-org/SDL/releases) (**SDL2**, not SDL3) and unzip it,
   e.g. to `C:\libs\SDL2-2.30.9`.
3. From a *Developer PowerShell for VS 2022* in the repository:

```powershell
cmake -B build -DSDL2_DIR=C:\libs\SDL2-2.30.9\cmake
cmake --build build --config Release
.\build\Release\mbsim.exe build\forest.mbp
ctest --test-dir build -C Release
```

The build copies `SDL2.dll` next to `mbsim.exe` automatically. vcpkg works too:
`vcpkg install sdl2:x64-windows`, then configure with
`-DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake` instead of `SDL2_DIR`.

**Alternatively, use WSL2.** On Windows 11, Linux GUI apps display natively,
so the Ubuntu steps above work unchanged inside WSL, simulator window included.

CI (`.github/workflows/ci.yml`) builds and tests on Ubuntu and Windows, runs a
sanitizer soak test, and builds the firmware.

| Key | Action |
| --- | --- |
| arrows | pan |
| Z / X | A / B buttons (not used by the forest pack yet) |
| T | time speed 1× → 60× → 600× |
| `[` / `]` | hour −/+ |
| W | cycle weather (pinned) → back to automatic |
| D (or F1) | debug overlay: actor boxes, places, lanes, obstacles; states in title bar |
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
./build/mbsnap build/forest.mbp - --info --sim 600            # vars, actors, spawn-rule status
```

`ctest --test-dir build` runs smoke tests. With `pip install jsonschema` it
also checks `pack.yaml` against the [pack schema](docs/pack-format.md#editor-support).

## In a browser (WebAssembly)

The same engine core, compiled to WebAssembly with plain clang (no Emscripten),
with a small viewer: time scrubbing, pinned variables, a debug overlay,
click-to-inspect, and spawn-rule status. Drop a `.mbp` on it to load another pack.

```sh
sudo apt install clang lld nodejs        # macOS: brew install llvm lld node
cmake -S platform/wasm -B build-wasm && cmake --build build-wasm
ctest --test-dir build-wasm              # add -DMB_MBSNAP=build/mbsnap when configuring for the pixel-parity test
python3 -m http.server -d build-wasm/web 8000   # open http://localhost:8000
```

CI uploads a ready-built `microbiome-web` artifact, so you can also just serve
that folder. `build-wasm/web/microbiome.js` is the JS API (typed in
`microbiome.d.ts`) that the planned [studio](docs/studio.md) builds on.

## Firmware (RP2350 / Pico 2)

**No toolchain needed to try it:** every CI run uploads a `microbiome-firmware`
artifact (Actions tab → a run → Artifacts) containing `.uf2` builds with and
without RTC support. Hold BOOTSEL, plug in the Pico 2, and copy the `.uf2` to the drive.

To build it yourself on Ubuntu (or WSL):

```sh
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib
git clone -b 2.1.1 https://github.com/raspberrypi/pico-sdk && (cd pico-sdk && git submodule update --init lib/tinyusb)
export PICO_SDK_PATH=$PWD/pico-sdk
cmake -S platform/rp2350 -B build-rp2350          # add -DMB_RTC_DS3231=ON if you fit an RTC
cmake --build build-rp2350
```

On native Windows, Raspberry Pi's VS Code "Raspberry Pi Pico" extension
installs the SDK and toolchain. Point it at `platform/rp2350` (not tested yet;
WSL and CI are the known-good paths).

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
| `tools/packc/pack.schema.json` | JSON Schema for `pack.yaml` (editor autocomplete, studio forms); `check_schema.py` validates against it. |
| `tools/placeholder_art.py` | Generates the current placeholder forest art. |
| `packs/forest/` | The forest pack. |
| `platform/sdl/` | Desktop simulator with hot reload. |
| `platform/headless/` | `mbsnap` PNG renderer. |
| `platform/wasm/` | WebAssembly host, JS API (`microbiome.js`) and web viewer. |
| `platform/rp2350/` | Pico 2 firmware: ST7789 DMA driver, buttons, optional DS3231, serial console. |
| `docs/architecture.md` | How the engine works and how to port it. |
| `docs/pack-format.md` | Writing packs: the YAML schema, behaviours, and art requirements. |
| `docs/studio.md` | Plan for microbiome Studio, a desktop/browser scene editor. |

## Product vision and roadmaps

Microbiome is planned as three connected products: Engine & Firmware, Studio
(world creation), and a separate Companion desktop app (devices, updates, worlds).

- [Project vision](docs/vision.md)
- [Unified three-track roadmap](docs/roadmap.md)
- [Christmas 2026 gift-first plan](docs/gift-2026.md)
- [Engine & Firmware roadmap](docs/roadmaps/engine-firmware.md)
- [Studio roadmap](docs/roadmaps/studio.md)
- [Companion roadmap](docs/roadmaps/companion.md)
- [Ideas and discovery backlog](docs/ideas.md)

The immediate target is **Friday, December 18, 2026**: one gift-ready custom
PCB/enclosure, one authored world, a minimum usable Studio, and reliable firmware.
The new docs are drafts and should be reviewed before scope is treated as fixed.
