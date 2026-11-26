#!/usr/bin/env python3
"""Build packs/glade/palette.gpl, the palette every Glade image is drawn from.

    python3 packs/glade/make_palette.py

The palette has three groups (see tools/palette.py for the file format):
  scene   SCENE_COLOURS colours reduced from the painting (src/scene.png),
          sorted into dark-to-light ramps by hue. Regenerated on every run.
  fx      the colours pack.yaml names directly: clear_color, lighting.emissive
          and particle colours. Regenerated on every run.
  animal  ramps for the actors. Seeded from ANIMAL_RAMPS the first time; after
          that, the animal entries already in palette.gpl are kept as they are,
          so edit them there (keep the "animal/<ramp>" names).
Any other group in an existing palette.gpl is kept too.

Re-run after changing src/scene.png or the colours in pack.yaml, then run
prepare_art.py. packc refuses images with colours outside palette.gpl.
"""
import os
import sys

import yaml
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
import palette as pal  # noqa: E402

SCENE_COLOURS = 176  # mean error barely moves between 128 and 200; the rest goes to animals
PALETTE = os.path.join(HERE, "palette.gpl")

# Starting ramps for the animals, darkest first. Kept apart from the scene's
# own browns and greys so animals stay readable against the painting.
ANIMAL_RAMPS = {
    "ink":      ["#120e0c", "#1a1e22", "#231e1a"],          # eyes, noses, shadow-side outline
    "cream":    ["#b8b0a2", "#d9d3c6", "#f2efe6", "#ffffff"],  # tails, chests, eye glints
    "fox":      ["#2e140a", "#5c2410", "#8a3f10", "#b4541a", "#d2691e", "#e88a3c", "#f2ad6a"],
    "deer":     ["#2b1c12", "#4a3020", "#6e4a2a", "#9a6a3e", "#ad7643", "#c49a6c", "#e0c49c"],
    "rabbit":   ["#3a322c", "#5e5348", "#8c7b6a", "#b3a593", "#d4c9b8"],
    "owl":      ["#2c241c", "#4f3e2c", "#7a5c3a", "#9a7a54", "#b39470", "#d6bf98"],
    "bird-red": ["#5a1a12", "#9a2c1e", "#d9583b", "#f08a62"],
}


def scene_entries():
    src = Image.open(os.path.join(HERE, "src", "scene.png")).convert("RGB")
    # Same settings prepare_art.py has always used: median cut refined by
    # k-means, so remap_fast() maps the painting straight back onto these.
    q = src.quantize(colors=SCENE_COLOURS, method=Image.Quantize.MEDIANCUT, kmeans=8,
                     dither=Image.Dither.NONE).convert("RGB")
    colours = [c for _, c in q.getcolors(1 << 24)]
    return pal.ramps(colours, "scene")


def fx_entries():
    with open(os.path.join(HERE, "pack.yaml"), encoding="utf-8") as f:
        spec = yaml.safe_load(f)
    named = [("clear", (spec.get("world") or {}).get("clear_color", "#000000"))]
    named += [("emissive", c) for c in (spec.get("lighting") or {}).get("emissive", [])]
    named += [(f"particle-{n}", p.get("color", "#ffffff")) for n, p in (spec.get("particles") or {}).items()]
    out, seen = [], set()
    for name, c in named:
        rgb = pal.parse_hex(c)
        if rgb not in seen:
            seen.add(rgb)
            out.append(pal.Entry(rgb, f"fx/{name}"))
    return out


def main():
    kept = []
    if os.path.exists(PALETTE):
        kept = [e for e in pal.read_gpl(PALETTE) if e.group not in ("scene", "fx")]
    if not any(e.group == "animal" for e in kept):
        kept = [pal.Entry(pal.parse_hex(c), f"animal/{name}")
                for name, cols in ANIMAL_RAMPS.items() for c in cols] + kept

    entries = scene_entries() + kept + fx_entries()
    distinct = len({e.rgb for e in entries})
    if distinct > 255:
        raise SystemExit(f"{distinct} colours: over the engine's 255. Lower SCENE_COLOURS or trim animal ramps.")
    groups = {}
    for e in entries:
        groups[e.group] = groups.get(e.group, 0) + 1
    header = (f"Glade pack palette: {distinct} of 255 colours, {255 - distinct} free.\n"
              "Groups: scene (from src/scene.png) and fx (from pack.yaml) are rewritten by\n"
              "make_palette.py; animal and any other groups are kept. Names are <group>/<ramp>,\n"
              "and each ramp runs dark to light.")
    pal.write_gpl(PALETTE, "Glade", entries, header)
    print(f"palette.gpl: {distinct} colours ({', '.join(f'{g} {n}' for g, n in groups.items())}), "
          f"{255 - distinct} free")


if __name__ == "__main__":
    main()
