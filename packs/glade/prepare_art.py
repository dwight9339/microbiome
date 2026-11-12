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
    art/scene.png, so particles drawn "above: sky" sit behind the trees.
Re-run it whenever src/scene.png changes.
"""
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SCENE_COLOURS = 200
SKY_MAX_Y = 70  # open sky only appears above this row


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
    sky.save(os.path.join(HERE, "art", "sky.png"))
    scene.save(os.path.join(HERE, "art", "scene.png"))
    print(f"art/scene.png {w}x{h}, art/sky.png {w}x{SKY_MAX_Y}, {SCENE_COLOURS} colours")


if __name__ == "__main__":
    main()
