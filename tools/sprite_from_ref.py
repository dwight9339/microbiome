#!/usr/bin/env python3
"""Turn reference art into a palette-locked sprite frame or sheet.

    python3 tools/sprite_from_ref.py PALETTE.gpl REF.png [REF.png ...] -o OUT.png --cell 32x32
            [--groups animal,fx] [--fit WxH] [--split CxR] [--flip]
            [--bg auto|none|#rrggbb] [--tolerance T] [--pockets auto|remove|keep]
            [--method mode|average] [--features 0.15] [--coverage 0.45] [--outline sel|#rrggbb]
            [--columns N] [--preview PREVIEW.png [--scene SCENE.png --at X,Y]]

A reference is a large image of one animal: a generated picture, a painting, a
photo you traced. The result is a starting point for cleanup in Aseprite, not
finished art. Each reference becomes one frame:

  1. Background. Real transparency is kept. Otherwise the colour (or the two
     colours of a fake checkerboard) along the image border is the backdrop, and
     everything of that colour connected to the border is removed. The
     tolerance is set just above the backdrop's own noise unless --tolerance
     gives one (OKLab distance). Areas of the same colour inside the animal,
     such as a white chest, are kept. --bg names the backdrop colour instead, or
     turns removal off. A saturated backdrop that the animal doesn't use (flat
     magenta, say) separates far more cleanly than white or a checkerboard.
     Backdrop seen through a gap (between antlers, say) is a pocket the border
     doesn't reach. With a saturated backdrop pockets are removed too; with a pale
     one they can't be told from white fur, so they are reported and kept unless
     --pockets remove says otherwise.
  2. Scale. Each frame is cropped to the animal, and all frames share one scale,
     set so the largest fits --fit (default: the cell, less 1 px on each side
     with --outline). A walk cycle keeps its size from frame to frame.
  3. Downsample. Every sprite pixel covers a block of reference pixels. With
     --method mode it takes the most common palette colour in the block, which
     keeps flat areas clean. With --method average it takes the block's average
     colour, snapped to the palette, which keeps more shading but softens edges.
     A pixel is opaque when at least --coverage of its block is. Then any compact
     region of one colour that won no pixel but covers at least --features of a
     sprite pixel (an eye, a nose) recolours the pixel it overlaps most.
  4. Clean up. Lone pixels are dropped and pinholes filled. --outline sel draws a
     1 px outline in the darkest shade of each neighbouring pixel's ramp;
     --outline #rrggbb uses one palette colour.
  5. Place. Frames sit bottom-centre in their cell (the engine's default anchor,
     the feet), left to right, wrapping after --columns.

--split CxR cuts each reference into a grid of C x R poses first, for a
generator that draws a whole sheet at once. --flip mirrors the references, for
animals drawn facing left (sprites face right).

--preview writes a picture to judge the result by: the references, the frames
enlarged, and with --scene and --at, each frame standing in the scene with its
feet at world X,Y (try packs/glade/art/scene.png at 300,200).
"""
import argparse
import math
import os
import sys
from collections import Counter, deque

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import palette as pal  # noqa: E402

MAX_REF = 1024  # larger references are shrunk first; past this, more pixels only cost time
FEATURES = 0.15  # see --features
FEATURE_CONTRAST = 0.15  # OKLab distance a kept feature needs from the pixel it replaces
FAMILY_RADIUS = 0.05     # palette colours closer than this vote together


def parse_size(s, what):
    try:
        w, h = (int(v) for v in s.lower().split("x"))
    except ValueError:
        raise SystemExit(f"{what}: expected WxH, e.g. 32x32, got {s!r}") from None
    if w < 1 or h < 1:
        raise SystemExit(f"{what}: must be at least 1x1")
    return w, h


class FastMatcher:
    """pal.Matcher keyed on 6 bits per channel: generated art has a colour per pixel,
    and the rounding (at most 2 levels per channel) never changes which palette
    colour is nearest by more than a hair."""

    def __init__(self, entries):
        self.exact = pal.Matcher(entries)
        self.cache = {}

    def __call__(self, rgb):
        k = (rgb[0] >> 2, rgb[1] >> 2, rgb[2] >> 2)
        hit = self.cache.get(k)
        if hit is None:
            hit = self.cache[k] = self.exact((k[0] * 4 + 2, k[1] * 4 + 2, k[2] * 4 + 2))
        return hit


# ---- 1. background --------------------------------------------------------------

def _border(w, h):
    for x in range(w):
        yield x, 0
        yield x, h - 1
    for y in range(1, h - 1):
        yield 0, y
        yield w - 1, y


def find_backdrop(im):
    """The colour(s) along the border: one for a flat backdrop, two for a fake checkerboard."""
    px = im.load()
    pts = list(_border(*im.size))
    buckets: dict[tuple, list] = {}
    for p in pts:
        r, g, b, _ = px[p]
        buckets.setdefault((r >> 4, g >> 4, b >> 4), []).append((r, g, b))
    ranked = sorted(buckets.values(), key=len, reverse=True)
    picks = [ranked[0]] + [c for c in ranked[1:3] if len(c) >= 0.2 * len(pts)]
    if sum(len(c) for c in picks) < 0.6 * len(pts):
        return None
    return [tuple(round(sum(ch) / len(c)) for ch in zip(*c)) for c in picks]


POCKET_MIN = 24        # px; smaller enclosed specks of backdrop colour are left to the vote
POCKET_REACH = 2.5     # pockets are small and mostly blur, so they match at this many tolerances
SATURATED = 0.1        # OKLab chroma from which a backdrop counts as saturated


def find_pockets(w, h, gone, is_backdrop):
    """Enclosed regions of backdrop colour the border fill didn't reach, as lists of (x, y)."""
    seen = bytearray(w * h)
    pockets = []
    for start in range(w * h):
        if seen[start] or gone[start] or not is_backdrop(start % w, start // w):
            continue
        seen[start] = 1
        stack, region, enclosed = [start], [], True
        while stack:
            y, x = divmod(stack.pop(), w)
            region.append((x, y))
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if not (0 <= nx < w and 0 <= ny < h):
                    continue
                k = ny * w + nx
                if gone[k]:
                    enclosed = False  # the soft edge of the backdrop already removed
                elif not seen[k] and is_backdrop(nx, ny):
                    seen[k] = 1
                    stack.append(k)
        if enclosed and len(region) >= POCKET_MIN:
            pockets.append(region)
    return pockets


def remove_background(im, bg, tol, pockets="auto"):
    """Returns (RGBA image, description of what was done)."""
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    if bg == "none":
        return im, "background kept"
    if bg == "auto":
        clear = sum(1 for p in _border(w, h) if px[p][3] < 128)
        if clear > (2 * (w + h) - 4) // 2:
            return im, "own transparency"
        colours = find_backdrop(im)
        if colours is None:
            raise SystemExit("no single backdrop colour along the edges; pass --bg #rrggbb, or --bg none "
                             "if the image has no backdrop")
    else:
        colours = [pal.parse_hex(bg)]
    labs = [pal.oklab(c) for c in colours]
    # With two or more backdrop colours (a checkerboard), the blend between them
    # where squares meet is backdrop too: measure to the segments joining them.
    segs = [(p, q) for i, p in enumerate(labs) for q in labs[i:]]
    cache: dict[tuple, float] = {}

    def seg_dist(p, a, b):
        ab = [y - x for x, y in zip(a, b)]
        n = sum(v * v for v in ab)
        t = 0.0 if n == 0 else max(0.0, min(1.0, sum((pv - av) * v for pv, av, v in zip(p, a, ab)) / n))
        return math.dist(p, [av + t * v for av, v in zip(a, ab)])

    def dist(rgb):
        k = (rgb[0] >> 2, rgb[1] >> 2, rgb[2] >> 2)
        d = cache.get(k)
        if d is None:
            p = pal.oklab((k[0] * 4 + 2, k[1] * 4 + 2, k[2] * 4 + 2))
            d = cache[k] = min(seg_dist(p, a, b) for a, b in segs)
        return d

    if tol is None:
        # Just above the backdrop's own noise (grain, JPEG), measured along the
        # border, so pale fur next to a white backdrop survives.
        ds = sorted(dist(px[p]) for p in _border(w, h) if px[p][3] >= 128)
        noise = ds[int(0.9 * (len(ds) - 1))] if ds else 0.0
        tol = min(0.08, max(0.015, 1.5 * noise))

    gone = bytearray(w * h)
    queue = deque()
    for x, y in _border(w, h):
        if not gone[y * w + x] and (px[x, y][3] < 128 or dist(px[x, y]) <= tol):
            gone[y * w + x] = 1
            queue.append((x, y))
    while queue:
        x, y = queue.popleft()
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < w and 0 <= ny < h and not gone[ny * w + nx]:
                p = px[nx, ny]
                if p[3] < 128 or dist(p) <= tol:
                    gone[ny * w + nx] = 1
                    queue.append((nx, ny))
    found = find_pockets(w, h, gone, lambda x, y: px[x, y][3] >= 128 and dist(px[x, y]) <= POCKET_REACH * tol)
    saturated = len(labs) == 1 and math.hypot(labs[0][1], labs[0][2]) >= SATURATED
    cut_pockets = pockets == "remove" or (pockets == "auto" and saturated)
    if cut_pockets:
        for x, y in (p for region in found for p in region):
            gone[y * w + x] = 1
    # one more ring: antialiased edge pixels half-blended with the backdrop
    fringe = [(x, y) for y in range(h) for x in range(w)
              if not gone[y * w + x] and dist(px[x, y]) <= 2.5 * tol
              and any(0 <= nx < w and 0 <= ny < h and gone[ny * w + nx] == 1
                      for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)))]
    for x, y in fringe:
        gone[y * w + x] = 2
    halo = unmix_halo(px, w, h, gone, labs)
    for y in range(h):
        for x in range(w):
            if gone[y * w + x]:
                px[x, y] = (0, 0, 0, 0)
    how = "backdrop " + " + ".join(pal.hexrgb(c) for c in colours) + f" removed (tolerance {tol:.3f})"
    if halo:
        how += f", {halo} px of halo"
    if found:
        where = ", ".join(f"{len(r)} px near {r[0][0]},{r[0][1]}" for r in sorted(found, key=len, reverse=True)[:3])
        more = ", ..." if len(found) > 3 else ""
        how += f"; {len(found)} enclosed pocket(s) of backdrop colour " + (
            f"removed ({where}{more})" if cut_pockets else f"kept ({where}{more}; --pockets remove clears them)")
    if len(colours) > 1:
        how += ("; with a checkerboard, pale colours along the outline can go with it "
                "(a flat saturated backdrop avoids that)")
    return im, how


HALO_RINGS = 4      # how deep into the outline the halo pass looks
HALO_REACH = 3      # px inward to find the body colour an outline pixel blends from
HALO_SHARE = 0.25   # removed when at least this much of the pixel is backdrop
HALO_LINE = 0.04    # OKLab distance from the body-to-backdrop line that still counts as a blend


def unmix_halo(px, w, h, gone, backdrop_labs):
    """Remove the halo: outline pixels that are a blend of the body colour just
    inside them and the backdrop (blur, antialiasing, JPEG). Works inward one
    ring at a time; marks removed pixels in `gone` and returns how many."""
    labs: dict[tuple, tuple] = {}

    def lab(p):
        k = (p[0] >> 2, p[1] >> 2, p[2] >> 2)
        v = labs.get(k)
        if v is None:
            v = labs[k] = pal.oklab((k[0] * 4 + 2, k[1] * 4 + 2, k[2] * 4 + 2))
        return v

    def is_halo(x, y, dx, dy):
        ix, iy = x + dx * HALO_REACH, y + dy * HALO_REACH
        if not (0 <= ix < w and 0 <= iy < h) or gone[iy * w + ix]:
            return False
        p, body = lab(px[x, y]), lab(px[ix, iy])
        for bg in backdrop_labs:
            seg = [b - c for b, c in zip(bg, body)]
            n = sum(v * v for v in seg)
            if n < 1e-6:
                continue
            t = sum((pv - c) * v for pv, c, v in zip(p, body, seg)) / n
            if t >= HALO_SHARE and math.dist(p, [c + min(t, 1.0) * v for c, v in zip(body, seg)]) <= HALO_LINE:
                return True
        return False

    def edge_dir(x, y):
        """Direction pointing inward, away from a removed neighbour, or None."""
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x - dx, y - dy
            if 0 <= nx < w and 0 <= ny < h and gone[ny * w + nx]:
                return dx, dy
        return None

    ring = [(x, y) for y in range(h) for x in range(w) if not gone[y * w + x] and edge_dir(x, y)]
    removed = 0
    for _ in range(HALO_RINGS):
        cut = [(x, y) for x, y in ring if is_halo(x, y, *edge_dir(x, y))]
        if not cut:
            break
        for x, y in cut:
            gone[y * w + x] = 3
        removed += len(cut)
        nxt = set()
        for x, y in cut:
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                if 0 <= nx < w and 0 <= ny < h and not gone[ny * w + nx]:
                    nxt.add((nx, ny))
        ring = list(nxt)
    return removed


# ---- 2-3. crop, scale, downsample -------------------------------------------------

def content_box(im, grain=8):
    """Bounding box of the animal, ignoring specks of leftover backdrop: it is
    measured on the mask shrunk by `grain`, where a speck under half a cell fades."""
    mask = im.getchannel("A").point(lambda a: 255 if a >= 128 else 0)
    small = mask.resize((max(1, im.width // grain), max(1, im.height // grain)), Image.BOX)
    box = small.point(lambda v: 255 if v >= 128 else 0).getbbox()
    if box is None:
        box = mask.getbbox()  # smaller than one grain: use the plain box
        if box is None:
            raise SystemExit("nothing left after removing the background; try a smaller --tolerance or --bg none")
        return box
    sx, sy = im.width / small.width, im.height / small.height
    pad = grain  # the shrink can trim up to a cell off each side
    box = (max(0, int(box[0] * sx) - pad), max(0, int(box[1] * sy) - pad),
           min(im.width, int(box[2] * sx) + pad), min(im.height, int(box[3] * sy) + pad))
    x0, y0, x1, y1 = mask.crop(box).getbbox()  # tighten to real pixels
    return box[0] + x0, box[1] + y0, box[0] + x1, box[1] + y1


def families(colours, radius=FAMILY_RADIUS):
    """Group colours closer than `radius` in OKLab (palettes often end several ramps
    in near-identical darks), so noise that scatters one dark eye across three of
    them still counts as one eye. Returns colour -> family id."""
    cols = sorted(colours)
    labs = [pal.oklab(c) for c in cols]
    parent = list(range(len(cols)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    for i in range(len(cols)):
        for j in range(i + 1, len(cols)):
            if math.dist(labs[i], labs[j]) < radius:
                parent[root(i)] = root(j)
    return {c: root(i) for i, c in enumerate(cols)}


def _winner(counts, fam):
    """The family with the most votes, then its most common colour."""
    by_fam = Counter()
    for c, n in counts.items():
        by_fam[fam[c]] += n
    best = by_fam.most_common(1)[0][0]
    return max((c for c in counts if fam[c] == best), key=counts.__getitem__)


def downsample(im, tw, th, method, coverage, match, exact, features=0.0):
    """im (cropped RGBA) to tw x th; returns (rows of rgb-or-None, features restored)."""
    w, h = im.size
    px = im.load()
    xs = [round(i * w / tw) for i in range(tw + 1)]
    ys = [round(j * h / th) for j in range(th + 1)]
    col_of = [0] * w  # source x -> sprite column
    for i in range(tw):
        for x in range(xs[i], xs[i + 1]):
            col_of[x] = i
    row_of = [0] * h  # source y -> sprite row
    for j in range(th):
        for y in range(ys[j], ys[j + 1]):
            row_of[y] = j
    # every reference pixel snapped once: the vote and the feature pass share it
    snapped = [match(px[x, y]) if px[x, y][3] >= 128 else None for y in range(h) for x in range(w)]
    fam = families({c for c in snapped if c is not None})

    out = []
    for j in range(th):
        row = []
        for i in range(tw):
            x0, x1 = xs[i], max(xs[i + 1], xs[i] + 1)
            y0, y1 = ys[j], max(ys[j + 1], ys[j] + 1)
            total = (x1 - x0) * (y1 - y0)
            opaque = [c for y in range(y0, y1) for c in snapped[y * w + x0:y * w + x1] if c is not None]
            if len(opaque) < coverage * total:
                row.append(None)
            elif method == "mode":
                row.append(_winner(Counter(opaque), fam))
            else:  # average in linear light, then snap
                ps = [px[x, y] for y in range(y0, y1) for x in range(x0, x1) if px[x, y][3] >= 128]
                lin = [sum(pal._lin(p[c]) for p in ps) / len(ps) for c in range(3)]
                rgb = tuple(round(255 * (v * 12.92 if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055))
                            for v in lin)
                row.append(exact(rgb))
        out.append(row)
    if features <= 0:
        return out, 0

    # A region of one colour family covering at least `features` of a sprite
    # pixel (an eye, a nose, a spot) that won no pixel anywhere recolours the
    # pixel it overlaps most. The vote alone loses anything that straddles pixels.
    # Blur and JPEG also leave thin bands of in-between colours along every edge.
    # So a feature must be
    #  - compact: its bounding box at least a quarter of a sprite pixel on the
    #    short side (a straight band isn't) and at least 40% full (a diagonal
    #    band isn't). Noise leaves holes in a feature, so outline length is no guide;
    #  - distinct from the pixel it replaces (an eye against fur; a blend lies
    #    between its neighbours, so it is close to the pixel there);
    #  - inside the silhouette. Outside it, the half-removed backdrop around the
    #    outline (a halo) can't be told from a lost ear tip, and a stray pixel of
    #    halo is worse than an ear tip to paint back.
    block = (w / tw) * (h / th)
    min_area, min_side = features * block, 0.25 * math.sqrt(block)
    seen = bytearray(w * h)
    claims = []
    for start in range(w * h):
        c0 = snapped[start]
        if c0 is None or seen[start]:
            continue
        f = fam[c0]
        seen[start] = 1
        stack, area, overlap, colours = [start], 0, Counter(), Counter()
        bx0, by0 = bx1, by1 = start % w, start // w
        while stack:
            k = stack.pop()
            y, x = divmod(k, w)
            area += 1
            colours[snapped[k]] += 1
            overlap[(col_of[x], row_of[y])] += 1
            bx0, bx1, by0, by1 = min(bx0, x), max(bx1, x), min(by0, y), max(by1, y)
            for nk in (k - 1 if x > 0 else -1, k + 1 if x < w - 1 else -1,
                       k - w if y > 0 else -1, k + w if y < h - 1 else -1):
                if nk >= 0 and snapped[nk] is not None and not seen[nk] and fam[snapped[nk]] == f:
                    seen[nk] = 1
                    stack.append(nk)
        bw, bh = bx1 - bx0 + 1, by1 - by0 + 1
        if (area >= min_area and min(bw, bh) >= min_side and area >= 0.4 * bw * bh
                and not any(out[j][i] is not None and fam.get(out[j][i]) == f for i, j in overlap)):
            inside = [(n, i, j) for (i, j), n in overlap.items() if out[j][i] is not None]
            if inside:
                n, i, j = max(inside)
                claims.append((n, i, j, colours.most_common(1)[0][0]))
    taken = set()
    for n, i, j, c in sorted(claims, reverse=True):
        if (i, j) in taken or math.dist(pal.oklab(c), pal.oklab(out[j][i])) < FEATURE_CONTRAST:
            continue
        taken.add((i, j))
        out[j][i] = c
    return out, len(taken)


# ---- 4. cleanup and outline ---------------------------------------------------------

N4 = ((-1, 0), (1, 0), (0, -1), (0, 1))
N8 = N4 + ((-1, -1), (1, -1), (-1, 1), (1, 1))


def _at(g, x, y):
    return g[y][x] if 0 <= y < len(g) and 0 <= x < len(g[0]) else None


def cleanup(g):
    h, w = len(g), len(g[0])
    lone = [(x, y) for y in range(h) for x in range(w)
            if g[y][x] is not None and all(_at(g, x + dx, y + dy) is None for dx, dy in N8)]
    for x, y in lone:
        g[y][x] = None
    holes = []
    for y in range(h):
        for x in range(w):
            if g[y][x] is None:
                around = [_at(g, x + dx, y + dy) for dx, dy in N4]
                if all(c is not None for c in around):
                    holes.append((x, y, Counter(around).most_common(1)[0][0]))
    for x, y, c in holes:
        g[y][x] = c
    return len(lone), len(holes)


def outline(cell, colour_for):
    """Transparent pixels touching the sprite (4-neighbours) take colour_for(neighbour colours)."""
    h, w = len(cell), len(cell[0])
    adds = []
    for y in range(h):
        for x in range(w):
            if cell[y][x] is None:
                near = [c for c in (_at(cell, x + dx, y + dy) for dx, dy in N4) if c is not None]
                if near:
                    adds.append((x, y, colour_for(near)))
    for x, y, c in adds:
        cell[y][x] = c


def selective_outline(entries):
    """colour -> darkest colour of its ramp; for several neighbours, the darkest of those."""
    by_ramp: dict[str, list] = {}
    for e in entries:
        by_ramp.setdefault(e.ramp, []).append(e.rgb)
    darkest = {}
    for cols in by_ramp.values():
        d = min(cols, key=lambda c: pal.oklab(c)[0])
        for c in cols:
            darkest.setdefault(c, d)
    return lambda near: min((darkest.get(c, c) for c in near), key=lambda c: pal.oklab(c)[0])


# ---- preview --------------------------------------------------------------------------

def checker(w, h, a=(58, 62, 59), b=(72, 76, 73), size=8):
    im = Image.new("RGB", (w, h), a)
    px = im.load()
    for y in range(h):
        for x in range(w):
            if (x // size + y // size) % 2:
                px[x, y] = b
    return im


def make_preview(refs, frames, cell, scene=None, at=None, zoom=8):
    cw, ch = cell
    tile_w, tile_h = cw * zoom, ch * zoom
    rows = 3 if scene is not None else 2
    pad = 8
    out = Image.new("RGB", (len(frames) * (tile_w + pad) + pad, rows * (tile_h + pad) + pad), (30, 33, 31))
    for i, (ref, fr) in enumerate(zip(refs, frames)):
        x = pad + i * (tile_w + pad)
        t = checker(tile_w, tile_h)
        r = ref.copy()
        r.thumbnail((tile_w, tile_h))
        t.paste(r, ((tile_w - r.width) // 2, (tile_h - r.height) // 2), r)
        out.paste(t, (x, pad))
        t = checker(tile_w, tile_h, size=zoom * 2)
        big = fr.resize((tile_w, tile_h), Image.NEAREST)
        t.paste(big, (0, 0), big)
        out.paste(t, (x, 2 * pad + tile_h))
        if scene is not None:
            fx, fy = at
            s = scene.copy()
            s.paste(fr, (fx - cw // 2, fy - ch), fr)
            # a region 2 cells wide and 2 tall around the feet, at zoom / 2
            box = (fx - cw, fy - ch * 3 // 2, fx + cw, fy + ch // 2)
            crop = Image.new("RGBA", (box[2] - box[0], box[3] - box[1]), (0, 0, 0, 255))
            crop.paste(s.crop(box), (0, 0))
            out.paste(crop.resize((tile_w, tile_h), Image.NEAREST).convert("RGB"), (x, 3 * pad + 2 * tile_h))
    return out


# ---- main ---------------------------------------------------------------------------------

def load_refs(paths, split, flip):
    refs = []
    for path in paths:
        im = Image.open(path).convert("RGBA")
        name = os.path.basename(path)
        if split:  # split first: the size cap and the flip apply to each pose, in sheet order
            c, r = split
            pw, ph = im.width // c, im.height // r
            pieces = [(f"{name}[{j * c + i}]", im.crop((i * pw, j * ph, (i + 1) * pw, (j + 1) * ph)))
                      for j in range(r) for i in range(c)]
        else:
            pieces = [(name, im)]
        for pname, p in pieces:
            if max(p.size) > MAX_REF:
                k = MAX_REF / max(p.size)
                p = p.resize((round(p.width * k), round(p.height * k)), Image.BOX)
            if flip:
                p = p.transpose(Image.FLIP_LEFT_RIGHT)
            refs.append((pname, p))
    return refs


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog="\n\n".join(__doc__.split("\n\n")[1:]))
    ap.add_argument("palette")
    ap.add_argument("refs", nargs="+", metavar="REF.png")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--cell", required=True, help="frame size, e.g. 32x32")
    ap.add_argument("--fit", help="box the animal is scaled into (default: the cell)")
    ap.add_argument("--groups", help="palette groups or single ramps to use, e.g. animal,fx or "
                                     "animal/deer,animal/cream,animal/ink (default: all)")
    ap.add_argument("--split", help="cut each reference into a CxR grid of poses")
    ap.add_argument("--flip", action="store_true", help="mirror the references (sprites face right)")
    ap.add_argument("--bg", default="auto", help="auto, none, or the backdrop colour as #rrggbb")
    ap.add_argument("--tolerance", type=float,
                    help="backdrop match distance in OKLab (default: just above the backdrop's own noise)")
    ap.add_argument("--pockets", choices=("auto", "remove", "keep"), default="auto",
                    help="backdrop enclosed by the animal (default auto: remove it if the backdrop is saturated)")
    ap.add_argument("--method", choices=("mode", "average"), default="mode")
    ap.add_argument("--features", type=float, default=FEATURES,
                    help="keep any one-colour region at least this fraction of a sprite pixel in area "
                         f"(eyes, noses); 0 = off (default {FEATURES})")
    ap.add_argument("--coverage", type=float, default=0.45,
                    help="fraction of a block that must be opaque for an opaque pixel (default 0.45)")
    ap.add_argument("--outline", help="sel (darkest of each neighbour's ramp) or #rrggbb")
    ap.add_argument("--columns", type=int, help="frames per row in the sheet (default: all in one row)")
    ap.add_argument("--preview", help="write a comparison image here")
    ap.add_argument("--scene", help="with --preview: an image to stand the frames in")
    ap.add_argument("--at", help="with --scene: the feet position X,Y in that image")
    a = ap.parse_args(argv)

    cw, ch = parse_size(a.cell, "--cell")
    entries = pal.read_gpl(a.palette)
    groups = [g.strip() for g in a.groups.split(",")] if a.groups else None
    try:
        entries = pal.select(entries, groups)
    except ValueError as e:
        raise SystemExit(f"--groups: {e}") from None
    match, exact = FastMatcher(entries), pal.Matcher(entries)
    margin = 1 if a.outline else 0
    fw, fh = parse_size(a.fit, "--fit") if a.fit else (cw - 2 * margin, ch - margin * 2)
    if fw > cw - 2 * margin or fh > ch - 2 * margin:
        raise SystemExit(f"--fit {fw}x{fh} does not fit the {cw}x{ch} cell"
                         + (" with room for the outline" if margin else ""))
    if a.outline == "sel":
        outline_colour = selective_outline(entries)
    elif a.outline:
        oc = pal.parse_hex(a.outline)
        snapped = exact(oc)
        if snapped != oc:
            print(f"--outline {a.outline} is not in the palette; using {pal.hexrgb(snapped)}")

        def outline_colour(near, c=snapped):
            return c
    if (a.scene is None) != (a.at is None):
        raise SystemExit("--scene and --at go together")

    refs = load_refs(a.refs, parse_size(a.split, "--split") if a.split else None, a.flip)
    cleaned = []
    for name, im in refs:
        im, how = remove_background(im, a.bg, a.tolerance, a.pockets)
        box = content_box(im)
        cleaned.append((name, im.crop(box), how))
    scale = min(min(fw / im.width, fh / im.height) for _, im, _ in cleaned)

    frames = []
    for name, im, how in cleaned:
        tw, th = max(1, round(im.width * scale)), max(1, round(im.height * scale))
        g, restored = downsample(im, tw, th, a.method, a.coverage, match, exact, a.features)
        dropped, filled = cleanup(g)
        cell = [[None] * cw for _ in range(ch)]
        ox, oy = (cw - tw) // 2, ch - margin - th
        for y in range(th):
            for x in range(tw):
                cell[oy + y][ox + x] = g[y][x]
        if a.outline:
            outline(cell, outline_colour)
        fr = Image.new("RGBA", (cw, ch), (0, 0, 0, 0))
        fpx = fr.load()
        for y in range(ch):
            for x in range(cw):
                if cell[y][x] is not None:
                    fpx[x, y] = cell[y][x] + (255,)
        frames.append(fr)
        extra = ", ".join(s for s in (f"{restored} features kept" if restored else "",
                                      f"{dropped} lone px dropped" if dropped else "",
                                      f"{filled} pinholes filled" if filled else "") if s)
        print(f"  {name}: {how}; {im.width}x{im.height} -> {tw}x{th}" + (f"; {extra}" if extra else ""))

    cols = max(1, min(a.columns or len(frames), len(frames)))
    rows = math.ceil(len(frames) / cols)
    sheet = Image.new("RGBA", (cols * cw, rows * ch), (0, 0, 0, 0))
    for i, fr in enumerate(frames):
        sheet.paste(fr, ((i % cols) * cw, (i // cols) * ch))
    sheet.save(a.output)
    used = sorted({px[:3] for _, px in sheet.getcolors(1 << 24) if px[3]})
    print(f"{a.output}: {len(frames)} frame(s) of {cw}x{ch} in {cols}x{rows}, scale 1/{1 / scale:.1f}, "
          f"{len(used)} colours")
    print(f"  pack.yaml:  sheet: {a.output}   frame: [{cw}, {ch}]")

    if a.preview:
        scene = None
        at = None
        if a.scene:
            scene = Image.open(a.scene).convert("RGBA")
            try:
                at = tuple(int(v) for v in a.at.split(","))
                assert len(at) == 2
            except (ValueError, AssertionError):
                raise SystemExit(f"--at: expected X,Y, got {a.at!r}") from None
        make_preview([im for _, im in refs], frames, (cw, ch), scene, at).save(a.preview)
        print(f"  preview: {a.preview}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
