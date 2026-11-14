#!/usr/bin/env python3
"""Generate the occlusion test pack's art. Re-run after changing it."""
import os

from PIL import Image

ART = os.path.join(os.path.dirname(os.path.abspath(__file__)), "art")
Image.new("RGB", (64, 40), (0x20, 0x40, 0x20)).save(os.path.join(ART, "bg.png"))
for name, rgb in {"blue": (0, 0, 255), "white": (255, 255, 255), "yellow": (255, 255, 0)}.items():
    Image.new("RGB", (4, 4), rgb).save(os.path.join(ART, f"{name}.png"))
mask = Image.new("RGB", (64, 40), (0, 0, 0))
mask.paste((255, 0, 0), (20, 10, 44, 30))  # x 20-43, y 10-29
mask.save(os.path.join(ART, "occlusion.png"))
