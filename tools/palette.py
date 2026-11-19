#!/usr/bin/env python3
"""Pack palettes: one .gpl file per pack that every image is drawn from.

    python3 tools/palette.py snap PALETTE.gpl IN.png OUT.png [--groups animal,fx]
    python3 tools/palette.py preview PALETTE.gpl OUT.png [--scale 16]
    python3 tools/palette.py check PALETTE.gpl IMAGE.png...

A .gpl is a GIMP palette, which Aseprite opens directly (Palette menu > Load
Palette). Each entry is named "<group>/<ramp>": the group says what the colour
is for (scene, animal, fx), the ramp which run of dark-to-light shades it
belongs to (scene/green, animal/fox). Entries of a ramp sit together, darkest
first, so picking shades in Aseprite is a matter of stepping left or right.

  snap     maps every opaque pixel to the nearest palette colour (judged in
           OKLab, so "nearest" means nearest to the eye), hardens alpha to the
           engine's on/off rule (>= 128 opaque), and writes the result. Use
           --groups to restrict it, e.g. a sprite to the animal and fx ramps.
  preview  draws the palette as swatches, one row per ramp.
  check    lists colours in images that are not in the palette.

Only Pillow is needed. The functions are importable (prepare_art.py and
make_palette.py use them).
"""
import argparse
import math
import os
import sys
from dataclasses import dataclass

from PIL import Image


@dataclass
class Entry:
    rgb: tuple
    name: str = ""

    @property
    def group(self) -> str:
        return self.name.split("/", 1)[0] if "/" in self.name else ""

    @property
    def ramp(self) -> str:
        return self.name


# ---- .gpl files ---------------------------------------------------------------

def read_gpl(path) -> list[Entry]:
    entries = []
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    if not lines or lines[0].strip() != "GIMP Palette":
        raise ValueError(f"{path}: not a GIMP palette (first line must be 'GIMP Palette')")
    for n, line in enumerate(lines[1:], 2):
        s = line.strip()
        if not s or s.startswith("#") or ":" in s.split()[0]:
            continue  # blank, comment, or a "Name:" / "Columns:" header
        parts = s.split(None, 3)
        try:
            rgb = tuple(int(v) for v in parts[:3])
        except ValueError:
            raise ValueError(f"{path}:{n}: expected 'R G B name', got {line!r}") from None
        if len(rgb) != 3 or not all(0 <= v <= 255 for v in rgb):
            raise ValueError(f"{path}:{n}: bad colour {line!r}")
        entries.append(Entry(rgb, parts[3].strip() if len(parts) > 3 else ""))
    return entries


def write_gpl(path, title: str, entries: list[Entry], header: str = "", columns: int = 16):
    out = ["GIMP Palette", f"Name: {title}", f"Columns: {columns}", "#"]
    out += [f"# {line}".rstrip() for line in header.strip().splitlines()]
    if header:
        out.append("#")
    for e in entries:
        r, g, b = e.rgb
        out.append(f"{r:3d} {g:3d} {b:3d}\t{e.name}")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def hexrgb(rgb) -> str:
    return "#%02x%02x%02x" % tuple(rgb[:3])


def parse_hex(s: str) -> tuple:
    s = s.lstrip("#")
    return tuple(int(s[i:i + 2], 16) for i in (0, 2, 4))


def select(entries: list[Entry], groups=None) -> list[Entry]:
    if not groups:
        return list(entries)
    keep = [e for e in entries if e.group in groups]
    if not keep:
        have = sorted({e.group for e in entries})
        raise ValueError(f"no palette entries in group(s) {', '.join(groups)} (have: {', '.join(have)})")
    return keep


# ---- colour maths (OKLab, https://bottosson.github.io/posts/oklab/) ------------

def _lin(c):
    c /= 255
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def oklab(rgb) -> tuple:
    r, g, b = (_lin(v) for v in rgb[:3])
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l, m, s = (math.copysign(abs(v) ** (1 / 3), v) for v in (l, m, s))
    return (0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
            1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
            0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s)


def oklch(rgb) -> tuple:
    L, a, b = oklab(rgb)
    return L, math.hypot(a, b), math.degrees(math.atan2(b, a)) % 360


class Matcher:
    """Nearest palette colour by OKLab distance, cached per input colour."""

    def __init__(self, entries: list[Entry]):
        self.cols = [(e.rgb, oklab(e.rgb)) for e in entries]
        self.cache: dict[tuple, tuple] = {}

    def __call__(self, rgb) -> tuple:
        rgb = tuple(rgb[:3])
        hit = self.cache.get(rgb)
        if hit is None:
            p = oklab(rgb)
            hit = min(self.cols, key=lambda c: math.dist(p, c[1]))[0]
            self.cache[rgb] = hit
        return hit


def snap(im: Image.Image, entries: list[Entry]) -> Image.Image:
    """Every opaque pixel (alpha >= 128) to its nearest palette colour; the rest clear."""
    match = Matcher(entries)
    src = im.convert("RGBA")
    out = Image.new("RGBA", src.size, (0, 0, 0, 0))
    sp, op = src.load(), out.load()
    w, h = src.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = sp[x, y]
            if a >= 128:
                op[x, y] = match((r, g, b)) + (255,)
    return out


def remap_fast(im: Image.Image, entries: list[Entry]) -> Image.Image:
    """Opaque RGB image to the nearest palette colour by RGB distance, in C.

    For large images such as a painted background. It agrees with Pillow's own
    quantizer, so a palette taken from Pillow's quantize() maps back exactly.
    """
    if len(entries) > 256:
        raise ValueError("at most 256 colours")
    flat = [v for e in entries for v in e.rgb]
    flat += list(entries[0].rgb) * (256 - len(entries))  # pad with a real entry, never a stray black
    pal = Image.new("P", (1, 1))
    pal.putpalette(flat)
    return im.convert("RGB").quantize(palette=pal, dither=Image.Dither.NONE).convert("RGB")


def off_palette(im: Image.Image, allowed: set) -> dict:
    """{rgb: (x, y) of its first pixel} for opaque pixels not in `allowed`."""
    px = im.convert("RGBA").load()
    w, h = im.size
    off = {}
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a >= 128 and (r, g, b) not in allowed and (r, g, b) not in off:
                off[(r, g, b)] = (x, y)
    return off


# ---- ramps -----------------------------------------------------------------------

HUE_NAMES = [(40, "red"), (75, "brown"), (100, "gold"), (125, "olive"), (165, "green"),
             (215, "teal"), (290, "blue"), (345, "purple"), (360, "red")]
NEUTRAL_CHROMA = 0.02  # OKLCh chroma below this reads as grey
MAX_HUE_SPAN = 35      # degrees; a family wider than this is split at its biggest hue gap
MIN_FAMILY = 4         # smaller families join the neighbour nearest in hue
MAX_RAMP = 12          # longer families are split into chroma tiers (muted ... vivid)


def _hue_name(h):
    return next(name for top, name in HUE_NAMES if h < top)


def _mean_hue(cols, lch):
    return math.degrees(math.atan2(sum(math.sin(math.radians(lch[c][2])) for c in cols),
                                   sum(math.cos(math.radians(lch[c][2])) for c in cols))) % 360


def _hue_dist(a, b):
    d = abs(a - b) % 360
    return min(d, 360 - d)


def _tiers(name, cols, lch):
    """Split a family into chroma tiers of at most MAX_RAMP colours."""
    k = math.ceil(len(cols) / MAX_RAMP)
    by_c = sorted(cols, key=lambda c: lch[c][1])
    size = math.ceil(len(by_c) / k)
    chunks = [by_c[i:i + size] for i in range(0, len(by_c), size)]
    labels = {1: [name], 2: [f"{name}-muted", name], 3: [f"{name}-muted", name, f"{name}-vivid"]}
    return list(zip(labels.get(len(chunks), [f"{name}-{i + 1}" for i in range(len(chunks))]), chunks))


def ramps(colours, prefix: str) -> list[Entry]:
    """Sort colours into named dark-to-light ramps grouped by hue, then by chroma."""
    lch = {c: oklch(c) for c in colours}
    greys = [c for c in colours if lch[c][1] < NEUTRAL_CHROMA]
    chroma = sorted((c for c in colours if lch[c][1] >= NEUTRAL_CHROMA), key=lambda c: lch[c][2])

    families = [chroma] if chroma else []
    while True:  # split the widest family at its largest hue gap until all are narrow
        wide = [f for f in families if len(f) > 1 and lch[f[-1]][2] - lch[f[0]][2] > MAX_HUE_SPAN]
        if not wide:
            break
        f = wide[0]
        gaps = [lch[f[i + 1]][2] - lch[f[i]][2] for i in range(len(f) - 1)]
        i = gaps.index(max(gaps)) + 1
        k = families.index(f)
        families[k:k + 1] = [f[:i], f[i:]]
    while len(families) > 1:  # fold tiny families into their nearest neighbour by hue
        small = min(families, key=len)
        if len(small) >= MIN_FAMILY:
            break
        families.remove(small)
        h = _mean_hue(small, lch)
        near = min(families, key=lambda f: _hue_dist(_mean_hue(f, lch), h))
        near.extend(small)

    out: list[tuple[str, list]] = []
    for f in sorted(families, key=lambda f: _mean_hue(f, lch)):
        out += _tiers(_hue_name(_mean_hue(f, lch)), f, lch)
    if greys:  # warm greys (rock in sun, bark) apart from cool ones (rock in shade, mist)
        warm = [c for c in greys if _hue_dist(lch[c][2], 70) <= 90]
        cool = [c for c in greys if c not in warm]
        for name, cols in (("grey-warm", warm), ("grey-cool", cool)):
            if cols:
                out += _tiers(name, cols, lch)

    seen: dict[str, int] = {}
    entries = []
    for name, cols in out:
        seen[name] = seen.get(name, 0) + 1
        label = name if seen[name] == 1 else f"{name}{seen[name]}"
        for c in sorted(cols, key=lambda c: lch[c][0]):
            entries.append(Entry(c, f"{prefix}/{label}"))
    for name, n in seen.items():  # number the first of a repeated name too
        if n > 1:
            for e in entries:
                if e.name == f"{prefix}/{name}":
                    e.name = f"{prefix}/{name}1"
    return entries


def preview(entries: list[Entry], scale: int = 16, per_row: int = 16) -> Image.Image:
    """Swatches, one row (or more, wrapping) per ramp; ramp names are not drawn."""
    rows: list[list[Entry]] = []
    last = None
    for e in entries:
        if e.ramp != last or len(rows[-1]) >= per_row:
            rows.append([])
            last = e.ramp
        rows[-1].append(e)
    gap = max(1, scale // 4)
    img = Image.new("RGB", (per_row * scale, len(rows) * (scale + gap)), (40, 40, 40))
    for r, row in enumerate(rows):
        for i, e in enumerate(row):
            x, y = i * scale, r * (scale + gap)
            img.paste(e.rgb, (x, y, x + scale, y + scale))
    return img


# ---- command line ------------------------------------------------------------------

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("snap", help="map an image onto the palette")
    s.add_argument("palette"), s.add_argument("input"), s.add_argument("output")
    s.add_argument("--groups", help="comma-separated groups to use (default: all)")
    p = sub.add_parser("preview", help="draw the palette as swatches")
    p.add_argument("palette"), p.add_argument("output")
    p.add_argument("--scale", type=int, default=16)
    c = sub.add_parser("check", help="list off-palette colours in images")
    c.add_argument("palette"), c.add_argument("images", nargs="+")
    a = ap.parse_args(argv)

    entries = read_gpl(a.palette)
    if a.cmd == "snap":
        groups = [g.strip() for g in a.groups.split(",")] if a.groups else None
        out = snap(Image.open(a.input), select(entries, groups))
        out.save(a.output)
        n = sum(1 for _, px in out.getcolors(1 << 24) if px[3])
        print(f"{a.output}: {out.size[0]}x{out.size[1]}, {n} colours")
    elif a.cmd == "preview":
        preview(entries, a.scale).save(a.output)
        print(f"{a.output}: {len(entries)} colours")
    else:
        allowed = {e.rgb for e in entries}
        bad = 0
        for path in a.images:
            off = off_palette(Image.open(path), allowed)
            if off:
                bad += 1
                shown = ", ".join(f"{hexrgb(c)} at {xy}" for c, xy in list(off.items())[:5])
                more = f" (+{len(off) - 5} more)" if len(off) > 5 else ""
                print(f"{path}: {len(off)} off-palette colours: {shown}{more}")
        if not bad:
            print(f"all {len(a.images)} images use only {os.path.basename(a.palette)}")
        return 1 if bad else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
