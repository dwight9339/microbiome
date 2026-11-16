#!/usr/bin/env python3
"""Generate the motion test pack's art. Re-run after changing it."""
import os

from PIL import Image

ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "art")
bg = Image.new("RGB", (32, 16))
for x in range(32):
    bg.paste((8 * x + 4, 0x40, 0x80), (x, 0, x + 1, 16))  # one colour per column
bg.save(os.path.join(ART, "bg.png"))
mask = Image.new("L", (32, 16), 0)
mask.paste(255, (8, 4, 16, 12))   # x 8-15, y 4-11: full weight
mask.paste(128, (16, 4, 24, 12))  # x 16-23: half weight
mask.save(os.path.join(ART, "water.png"))
