#!/usr/bin/env python3
"""Turn the painted glade (src/scene.png) into pack-ready layers.

    python3 packs/glade/prepare_art.py

The source is one flat 480x240 image with ~85k colours. The engine needs
<= 255 colours across the whole pack, and stars must show only through gaps
in the canopy, so this:
  - reduces the scene to SCENE_COLOURS colours (median cut + k-means, no
    dithering, which keeps pixel edges clean), leaving the rest of the
    palette for sprites and particles;
  - moves the open sky into art/sky.png and leaves it transparent in
    art/scene.png, so particles drawn "above: sky" sit behind the trees;
  - copies the pixels marked white in src/front_mask.png (the foliage and
    boulders along the bottom edge) into art/front.png, a layer drawn over
    the animals. They stay in art/scene.png too, so nothing shows through.
    Only rows from FRONT_TOP down are kept: the layer sits at
    `at: [0, FRONT_TOP]` in pack.yaml.
Re-run it whenever src/scene.png or src/front_mask.png changes. The mask is
hand-editable: paint white what should cover animals, black elsewhere.
"""
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SCENE_COLOURS = 200
SKY_MAX_Y = 70  # open sky only appears above this row
FRONT_TOP = 150  # first row of art/front.png; must match its `at:` in pack.yaml


def is_sky(r, g, b):
    # bright, blue-leaning; the misty background trees are darker and greener
    return b >= 200 and r >= 150 and (r + g + b) / 3 > 195


def main():
    src = Image.open(os.path.join(HERE, "src", "scene.png")).convert("RGB")
    w, h = src.size
    pal = src.quantize(colors=SCENE_COLOURS, method=Image.Quantize.MEDIANCUT, kmeans=8,
                       dither=Image.Dither.NONE).convert("RGB")
    sky = Image.new("RGBA", (w, SKY_MAX_Y), (0, 0, 0, 0))
    scene = pal.convert("RGBA")
    sp, qp, cp = src.load(), pal.load(), scene.load()
    kp = sky.load()
    for y in range(SKY_MAX_Y):
        for x in range(w):
            if is_sky(*sp[x, y]):
                kp[x, y] = qp[x, y] + (255,)
                cp[x, y] = (0, 0, 0, 0)
    mask = Image.open(os.path.join(HERE, "src", "front_mask.png")).convert("L")
    if mask.size != (w, h):
        raise SystemExit(f"src/front_mask.png is {mask.size[0]}x{mask.size[1]}, expected {w}x{h}")
    box = mask.point(lambda v: 255 if v >= 128 else 0).getbbox()
    if box and box[1] < FRONT_TOP:
        raise SystemExit(f"src/front_mask.png marks row {box[1]}, above FRONT_TOP ({FRONT_TOP}): "
                         "lower FRONT_TOP here and the front layer's `at:` in pack.yaml")
    front = Image.new("RGBA", (w, h - FRONT_TOP), (0, 0, 0, 0))
    mp, fp = mask.load(), front.load()
    for y in range(FRONT_TOP, h):
        for x in range(w):
            if mp[x, y] >= 128:
                fp[x, y - FRONT_TOP] = qp[x, y] + (255,)

    sky.save(os.path.join(HERE, "art", "sky.png"))
    scene.save(os.path.join(HERE, "art", "scene.png"))
    front.save(os.path.join(HERE, "art", "front.png"))
    print(f"art/scene.png {w}x{h}, art/sky.png {w}x{SKY_MAX_Y}, art/front.png {w}x{h - FRONT_TOP} "
          f"at y={FRONT_TOP}, {SCENE_COLOURS} colours")


if __name__ == "__main__":
    main()
