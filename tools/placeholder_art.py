#!/usr/bin/env python3
"""Generate crude placeholder pixel art for packs/forest so the engine has
something to show. Every file it writes is meant to be replaced by real art
with the same dimensions/frame layout (see packs/forest/pack.yaml).

    python3 tools/placeholder_art.py packs/forest/art
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

C = {
    # sky (top -> horizon)
    "sky0": "#2f5f9e", "sky1": "#3b6eaa", "sky2": "#4a7fb7", "sky3": "#5d92c4",
    "sky4": "#74a6d0", "sky5": "#8fbadc", "sky6": "#b0d0e8",
    # mountains / far
    "mtn": "#6f819b", "mtn_dark": "#5b6b84", "snow": "#e8eef5",
    "far": "#46705a", "far_dark": "#365c48", "far_light": "#5a8569",
    # ground
    "grass": "#5f9a3c", "grass_dark": "#4a7f2f", "grass_light": "#7bb04a",
    "dirt": "#a08058", "dirt_dark": "#866845",
    "water": "#3d7fb0", "water_light": "#6aa5cf", "water_dark": "#2f6690",
    "bark": "#6b4a2f", "bark_dark": "#523822",
    "leaf": "#3c7a35", "leaf_dark": "#2e5f29", "leaf_light": "#57993f",
    "rock": "#8a8f94", "rock_dark": "#6d7277", "hole": "#2b2118",
    # animals
    "deer": "#9a6a3e", "deer_light": "#c49a6c", "deer_dark": "#6e4a2a",
    "white": "#f2efe6", "black": "#231e1a",
    "rabbit": "#8c7b6a", "rabbit_light": "#b3a593",
    "fox": "#d2691e", "fox_dark": "#8a3f10",
    "owl": "#7a5c3a", "owl_light": "#b39470", "owl_eye": "#ffd23f",
    "bird": "#6b5a4a", "breast": "#d9583b",
}


def rgb(name):
    h = C[name].lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), 255)


def new(w, h):
    return Image.new("RGBA", (w, h), (0, 0, 0, 0))


# ---------------------------------------------------------------- scenery

def sky():
    im = new(64, 300)
    px = im.load()
    bands = ["sky0", "sky1", "sky2", "sky3", "sky4", "sky5", "sky6"]
    band_h = 26
    for y in range(300):
        i = min(y // band_h, len(bands) - 1)
        for x in range(64):
            c = bands[i]
            # dithered transition row between bands
            if y % band_h >= band_h - 3 and i + 1 < len(bands) and (x + y) % 2 == 0:
                c = bands[i + 1]
            px[x, y] = rgb(c)
    return im


def mountains(rnd):
    w = 800
    im = new(w, 300)
    d = ImageDraw.Draw(im)
    x = -40
    while x < w + 40:
        peak_h = rnd.randint(95, 140)
        half = rnd.randint(60, 110)
        d.polygon([(x - half, 210), (x, peak_h), (x + half, 210)], fill=rgb("mtn"))
        d.polygon([(x, peak_h), (x + half, 210), (x + half // 3, 210)], fill=rgb("mtn_dark"))
        cap = 14
        d.polygon([(x - cap, peak_h + cap + 2), (x, peak_h), (x + cap, peak_h + cap + 2),
                   (x + 4, peak_h + cap - 2), (x - 3, peak_h + cap + 1)], fill=rgb("snow"))
        x += half + rnd.randint(10, 60)
    d.rectangle([0, 205, w, 300], fill=rgb("mtn_dark"))
    return im


def far_trees(rnd):
    w = 1040
    im = new(w, 300)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 195, w, 300], fill=rgb("far_dark"))
    for x in range(-10, w + 10, 9):
        h = rnd.randint(28, 58)
        base = 200 + rnd.randint(-4, 4)
        col = rgb(rnd.choice(["far", "far_dark", "far"]))
        d.polygon([(x - 8, base), (x, base - h), (x + 8, base)], fill=col)
        d.line([(x, base - h + 3), (x + 5, base - 2)], fill=rgb("far_light"))
    return im


def canopy(d, cx, cy, rnd, r=46):
    for _ in range(14):
        ox, oy = rnd.randint(-r, r), rnd.randint(-r // 2, r // 2)
        rr = rnd.randint(18, 30)
        d.ellipse([cx + ox - rr, cy + oy - rr, cx + ox + rr, cy + oy + rr], fill=rgb("leaf_dark"))
    for _ in range(12):
        ox, oy = rnd.randint(-r + 8, r - 8), rnd.randint(-r // 2, r // 3)
        rr = rnd.randint(12, 22)
        d.ellipse([cx + ox - rr, cy + oy - rr - 4, cx + ox + rr, cy + oy + rr - 4], fill=rgb("leaf"))
    for _ in range(10):
        ox, oy = rnd.randint(-r + 12, r - 12), rnd.randint(-r // 2, 0)
        rr = rnd.randint(5, 9)
        d.ellipse([cx + ox - rr, cy + oy - rr - 10, cx + ox + rr, cy + oy + rr - 10], fill=rgb("leaf_light"))


def ground(rnd):
    w, h = 1280, 300
    im = new(w, h)
    d = ImageDraw.Draw(im)
    # meadow with a wavy top edge
    for x in range(w):
        top = 212 + int(4 * math.sin(x / 37.0) + 3 * math.sin(x / 11.0))
        d.line([(x, top), (x, h)], fill=rgb("grass"))
        d.point((x, top), fill=rgb("grass_light"))
    for _ in range(1400):
        x, y = rnd.randrange(w), rnd.randint(222, h - 1)
        d.point((x, y), fill=rgb(rnd.choice(["grass_dark", "grass_light", "grass_dark"])))
    # dirt path
    for x in range(w):
        y = 266 + int(8 * math.sin(x / 90.0))
        d.line([(x, y), (x, y + 9)], fill=rgb("dirt"))
        if x % 3 == 0:
            d.point((x, y + rnd.randint(0, 9)), fill=rgb("dirt_dark"))
    # pond
    d.ellipse([688, 248, 832, 276], fill=rgb("water_dark"))
    d.ellipse([692, 250, 828, 273], fill=rgb("water"))
    for _ in range(18):
        x, y = rnd.randint(705, 815), rnd.randint(254, 268)
        d.line([(x, y), (x + rnd.randint(3, 8), y)], fill=rgb("water_light"))
    # oak (left) with a branch to the right at y~140
    d.rectangle([205, 110, 235, 236], fill=rgb("bark"))
    d.rectangle([205, 110, 211, 236], fill=rgb("bark_dark"))
    d.polygon([(200, 236), (205, 220), (235, 220), (242, 236)], fill=rgb("bark"))
    d.polygon([(235, 136), (282, 132), (282, 137), (235, 145)], fill=rgb("bark"))
    d.line([(236, 145), (282, 137)], fill=rgb("bark_dark"))
    canopy(d, 220, 64, rnd, r=52)
    # snag (right) with a bare branch to the left at y~151
    d.rectangle([1040, 70, 1056, 238], fill=rgb("bark"))
    d.rectangle([1050, 70, 1056, 238], fill=rgb("bark_dark"))
    d.polygon([(1040, 158), (1000, 150), (1000, 154), (1040, 165)], fill=rgb("bark"))
    d.polygon([(1046, 70), (1060, 40), (1052, 72)], fill=rgb("bark_dark"))
    # rocks, log, bush, burrow
    d.ellipse([440, 232, 470, 248], fill=rgb("rock_dark"))
    d.ellipse([443, 231, 466, 244], fill=rgb("rock"))
    d.rectangle([560, 238, 622, 247], fill=rgb("bark"))
    d.ellipse([616, 237, 626, 248], fill=rgb("deer_light"))
    d.line([(560, 247), (622, 247)], fill=rgb("bark_dark"))
    for ox, oy, r in ((-12, 0, 13), (0, -6, 15), (13, 0, 12)):
        d.ellipse([905 + ox - r, 232 + oy - r, 905 + ox + r, 232 + oy + r], fill=rgb("leaf_dark"))
    for ox, oy, r in ((-6, -6, 7), (7, -9, 6)):
        d.ellipse([905 + ox - r, 232 + oy - r, 905 + ox + r, 232 + oy + r], fill=rgb("leaf"))
    d.ellipse([372, 242, 390, 251], fill=rgb("dirt_dark"))
    d.ellipse([375, 243, 387, 250], fill=rgb("hole"))
    return im


def foreground(rnd):
    w = 1600
    im = new(w, 300)
    d = ImageDraw.Draw(im)
    x = 0
    while x < w:
        n = rnd.randint(3, 7)
        for i in range(n):
            bx = x + i * 2
            hgt = rnd.randint(8, 20)
            lean = rnd.randint(-3, 3)
            d.line([(bx, 299), (bx + lean, 299 - hgt)], fill=rgb(rnd.choice(["grass_dark", "leaf_dark", "grass"])))
        if rnd.random() < 0.25:  # fern
            cx = x + 6
            for k in range(6):
                d.line([(cx, 299), (cx + rnd.randint(-12, 12), 299 - rnd.randint(10, 24))], fill=rgb("leaf"))
        x += rnd.randint(14, 60)
    return im


# ---------------------------------------------------------------- sprites

def sheet(fw, fh, cols, rows):
    return new(fw * cols, fh * rows)


def cell(im, idx, fw, fh, cols):
    """Return a drawing context offset to a sheet cell."""
    ox, oy = (idx % cols) * fw, (idx // cols) * fh
    d = ImageDraw.Draw(im)

    class D:
        def ellipse(self, b, c): d.ellipse([b[0] + ox, b[1] + oy, b[2] + ox, b[3] + oy], fill=rgb(c))
        def rect(self, b, c): d.rectangle([b[0] + ox, b[1] + oy, b[2] + ox, b[3] + oy], fill=rgb(c))
        def line(self, pts, c, w=1): d.line([(x + ox, y + oy) for x, y in pts], fill=rgb(c), width=w)
        def poly(self, pts, c): d.polygon([(x + ox, y + oy) for x, y in pts], fill=rgb(c))
        def px(self, x, y, c): d.point((x + ox, y + oy), fill=rgb(c))
    return D()


def deer():
    fw, fh, cols = 32, 32, 5
    im = sheet(fw, fh, cols, 2)

    def body(d, lift=0, head="up", legs=(0, 0, 0, 0), ears_up=False):
        by = 12 - lift
        for (lx, off) in zip((10, 12, 22, 24), legs):
            d.line([(lx, by + 8), (lx + off, 31 - lift // 2)], "deer_dark", 2)
        d.ellipse([7, by, 26, by + 9], "deer")
        d.ellipse([10, by + 5, 23, by + 9], "deer_light")
        d.ellipse([5, by + 1, 8, by + 4], "white")
        if head == "down":
            d.poly([(22, by + 2), (26, by + 2), (29, by + 12), (26, by + 13)], "deer")
            d.ellipse([25, by + 11, 31, by + 16], "deer")
            d.px(30, by + 15, "black")
            d.px(27, by + 12, "black")
        else:
            top = 1 if head == "alert" else 3
            d.poly([(21, by + 3), (25, by + 3), (27, top + 4), (24, top + 4)], "deer")
            d.ellipse([23, top, 30, top + 6], "deer")
            d.px(29, top + 4, "black")
            d.px(26, top + 2, "black")
            d.line([(24, top), (23, top - (3 if ears_up else 1))], "deer_dark")
    walk = [(-2, 2, 2, -2), (0, 0, 0, 0), (2, -2, -2, 2), (0, 0, 0, 0)]
    for i, lg in enumerate(walk):
        body(cell(im, i, fw, fh, cols), legs=lg)
    body(cell(im, 4, fw, fh, cols), head="down")
    body(cell(im, 5, fw, fh, cols), head="down", legs=(0, 1, 0, 1))
    body(cell(im, 6, fw, fh, cols), head="alert", ears_up=True)
    run = [(-5, -5, 5, 5), (0, 0, 0, 0), (5, 5, -5, -5)]
    for i, lg in enumerate(run):
        body(cell(im, 7 + i, fw, fh, cols), lift=2 if i == 1 else 0, legs=lg)
    return im


def rabbit():
    fw, fh, cols = 16, 16, 4
    im = sheet(fw, fh, cols, 2)

    def r(d, lift=0, ears="back", stretch=0):
        y = 8 - lift
        d.ellipse([3 - stretch, y, 11, y + 6], "rabbit")
        d.ellipse([4, y + 3, 9, y + 6], "rabbit_light")
        d.ellipse([9, y - 2, 14, y + 3], "rabbit")
        d.px(13, y, "black")
        d.ellipse([2 - stretch, y + 1, 4 - stretch, y + 3], "white")
        if ears == "up":
            d.line([(10, y - 2), (10, y - 7)], "rabbit", 2)
        else:
            d.line([(10, y - 1), (7, y - 4)], "rabbit", 2)
        d.line([(9, y + 6), (10, 15)], "rabbit")
    r(cell(im, 0, fw, fh, cols))
    r(cell(im, 1, fw, fh, cols), lift=2, stretch=1)
    r(cell(im, 2, fw, fh, cols), lift=3, stretch=2)
    r(cell(im, 3, fw, fh, cols), lift=1)
    r(cell(im, 4, fw, fh, cols))
    d = cell(im, 5, fw, fh, cols)
    r(d)
    d.px(14, 11, "grass_light")
    r(cell(im, 6, fw, fh, cols), ears="up")
    return im


def fox():
    fw, fh, cols = 32, 24, 4
    im = sheet(fw, fh, cols, 2)

    def f(d, legs=(0, 0, 0, 0), sniff=False):
        for lx, off in zip((9, 11, 20, 22), legs):
            d.line([(lx, 15), (lx + off, 23)], "fox_dark", 2)
        d.poly([(1, 9), (8, 11), (8, 15), (2, 14)], "fox")
        d.px(1, 10, "white")
        d.px(1, 11, "white")
        d.ellipse([6, 9, 24, 17], "fox")
        d.ellipse([12, 13, 22, 17], "white")
        hy = 13 if sniff else 6
        d.poly([(21, hy + 2), (28, hy + 4), (31, hy + 6), (24, hy + 9)], "fox")
        d.poly([(22, hy + 2), (23, hy - 2), (25, hy + 2)], "fox_dark")
        d.px(26, hy + 4, "black")
        d.px(31, hy + 6, "black")
        d.line([(25, hy + 7), (29, hy + 7)], "white")
    trot = [(-2, 2, 2, -2), (0, 0, 0, 0), (2, -2, -2, 2), (0, 0, 0, 0)]
    for i, lg in enumerate(trot):
        f(cell(im, i, fw, fh, cols), legs=lg)
    f(cell(im, 4, fw, fh, cols), sniff=True)
    f(cell(im, 5, fw, fh, cols), sniff=True, legs=(0, 1, 0, 0))
    return im


def owl():
    fw, fh, cols = 16, 16, 4
    im = sheet(fw, fh, cols, 1)

    def perch(d, blink=False):
        d.ellipse([4, 3, 12, 15], "owl")
        d.ellipse([5, 3, 11, 9], "owl_light")
        d.poly([(4, 4), (5, 0), (7, 3)], "owl")
        d.poly([(12, 4), (11, 0), (9, 3)], "owl")
        if blink:
            d.line([(6, 6), (7, 6)], "owl")
            d.line([(9, 6), (10, 6)], "owl")
        else:
            d.rect([6, 5, 7, 6], "owl_eye")
            d.rect([9, 5, 10, 6], "owl_eye")
        d.px(8, 8, "black")
        d.line([(6, 15), (7, 15)], "owl_eye")
        d.line([(9, 15), (10, 15)], "owl_eye")

    def fly(d, up):
        d.ellipse([3, 6, 13, 12], "owl")
        d.ellipse([10, 5, 15, 10], "owl_light")
        d.rect([12, 6, 13, 7], "owl_eye")
        if up:
            d.poly([(5, 8), (8, 0), (11, 8)], "owl")
        else:
            d.poly([(5, 9), (8, 15), (11, 9)], "owl")
    perch(cell(im, 0, fw, fh, cols))
    perch(cell(im, 1, fw, fh, cols), blink=True)
    fly(cell(im, 2, fw, fh, cols), True)
    fly(cell(im, 3, fw, fh, cols), False)
    return im


def bird():
    fw, fh, cols = 8, 8, 4
    im = sheet(fw, fh, cols, 1)
    for i, up in enumerate((True, False)):
        d = cell(im, i, fw, fh, cols)
        d.ellipse([1, 3, 6, 6], "bird")
        d.px(5, 5, "breast")
        d.px(7, 4, "black")
        d.px(6, 3, "bird")
        if up:
            d.line([(3, 3), (2, 0)], "bird")
            d.line([(4, 3), (4, 0)], "bird")
        else:
            d.line([(3, 6), (2, 7)], "bird")
    for i, hop in ((2, 0), (3, 1)):
        d = cell(im, i, fw, fh, cols)
        y = 2 - hop
        d.ellipse([1, y + 1, 5, y + 5], "bird")
        d.ellipse([3, y + 3, 5, y + 5], "breast")
        d.ellipse([4, y, 7, y + 3], "bird")
        d.px(7, y + 1, "black")
        d.px(0, y + 2, "bird")
        d.line([(3, y + 6), (3, 7)], "black")
    return im


def main(out):
    os.makedirs(out, exist_ok=True)
    rnd = random.Random(7)
    files = {
        "sky.png": sky(), "mountains.png": mountains(rnd), "far_trees.png": far_trees(rnd),
        "ground.png": ground(rnd), "foreground.png": foreground(rnd),
        "deer.png": deer(), "rabbit.png": rabbit(), "fox.png": fox(), "owl.png": owl(),
        "songbird.png": bird(),
    }
    for name, im in files.items():
        im.save(os.path.join(out, name))
        print(f"wrote {os.path.join(out, name)} ({im.width}x{im.height})")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "packs/forest/art")
