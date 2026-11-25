"""Solder paste stencil jig for prototype 1 (CadQuery).

Run:  python paste_jig.py            -> writes STEP/STL into ./out, runs the checks
      python paste_jig.py --render   -> also writes preview PNGs (needs pyvista)

One printed part with two nested pockets: the board drops into the lower one
back-side-up (all of the paste is on B.Cu), the frameless stencil into the upper
one.  U-shaped finger notches pass right through the jig at the middle of the top
and bottom edges, reaching under both.

Jig frame: origin = board centre on the table, X right / Y up looking down at the
BACK of the board (so KiCad X is mirrored), Z up.  All dimensions in mm.
The outline and apertures come from the gerbers that were sent to fab.
"""
import argparse
import os
import re

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))
GERBERS = os.path.join(HERE, "..", "prototype_1", "gerbers")
OUT = os.path.join(HERE, "out")

# ----------------------------------------------------------------------------
# Parameters -- measure the real board and stencil, then adjust the first block
# ----------------------------------------------------------------------------
BOARD_DW, BOARD_DH = 0.0, 0.0       # measured board size minus nominal
BOARD_T = 1.6                       # nominal thickness; fab tolerance is +/-10 %
STENCIL_MARGIN = 5.0                # as ordered, per side
STENCIL_DW, STENCIL_DH = 0.0, 0.0   # measured stencil size minus nominal
STENCIL_DX, STENCIL_DY = 0.0, 0.0   # stencil centre relative to the board centre when the
                                    # apertures line up (non-zero if the pattern is off-centre)

BOARD_CLR = 0.15                # per side
STENCIL_CLR = 0.25              # per side; leaves room to nudge by eye

# Z steps are multiples of 0.2 so they survive a 0.2 (or 0.1) mm layer height.
BASE_T = 5.0                    # under the board; also the finger room in the notches
BOARD_DEPTH = 1.4               # < thinnest in-tolerance board: the board is always
                                # proud, so the stencil always lies on it
LIP_H = 0.8                     # wall around the stencil
LIP_LEAD = 0.4                  # lead-in chamfer on that wall
FOIL_T = 0.12                   # stencil thickness

FRAME = 8.0                     # wall width outside the stencil
NOTCH_W = 26.0                  # finger notch width
NOTCH_X = 0.0                   # finger notch centre
NOTCH_UNDER = 3.0               # how far the notch reaches in under the board
NOTCH_R = 8.0                   # inner corner radius of the notch
SIDE_GAP = 44.0                 # opening in the lip on the left/right sides (tape, squeegee)
RELIEF_D = 3.0                  # pocket corner relief
OUTER_R = 4.0


# ----------------------------------------------------------------------------
# Gerbers
# ----------------------------------------------------------------------------
def _read(name):
    with open(os.path.join(GERBERS, f"prototype_1-{name}.gbr"), encoding="utf-8") as f:
        text = f.read()
    if "%FSLAX46Y46*%" not in text or "%MOMM*%" not in text:
        raise ValueError(f"{name}: expected 4.6 mm gerber coordinates")
    return text


def outline():
    pts = [(int(x) / 1e6, int(y) / 1e6) for x, y in
           re.findall(r"^X(-?\d+)Y(-?\d+)(?:I-?\d+J-?\d+)?D0[12]\*", _read("Edge_Cuts"), re.M)]
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), max(xs), min(ys), max(ys)


GX0, GX1, GY0, GY1 = outline()
GCX, GCY = (GX0 + GX1) / 2, (GY0 + GY1) / 2
BOARD_W, BOARD_H = GX1 - GX0 + BOARD_DW, GY1 - GY0 + BOARD_DH
BOARD_R = 1.0                   # corner radius on Edge.Cuts


def apertures():
    """[(ref, x, y, w, h)] of every stencil opening, in jig coordinates."""
    text = _read("B_Paste")
    size = {}
    for code, kind, args in re.findall(r"^%ADD(\d+)(RoundRect|R),([^*]+)\*%", text, re.M):
        v = [float(a) for a in args.split("X")]
        if kind == "R":
            size[code] = (v[0], v[1])
        else:                                   # radius, then four corner-centre points
            size[code] = tuple(2 * (max(abs(c) for c in v[1 + i:9:2]) + v[0]) for i in (0, 1))
    out, ref, cur = [], "?", None
    for line in text.splitlines():
        if m := re.match(r"%TO\.C,([^*]+)\*%", line):
            ref = m[1]
        elif m := re.match(r"D(\d\d+)\*", line):
            cur = m[1]
        elif m := re.match(r"X(-?\d+)Y(-?\d+)D03\*", line):
            x, y = int(m[1]) / 1e6, int(m[2]) / 1e6
            out.append((ref, GCX - x, y - GCY, *size[cur]))     # gerber Y is already up
    if len(out) != text.count("D03*"):
        raise ValueError("Did not understand every paste flash")
    if _read("F_Paste").count("D03*"):
        raise ValueError("F_Paste is no longer empty: this jig only does the back")
    return out


APERTURES = apertures()

# ----------------------------------------------------------------------------
# Derived
# ----------------------------------------------------------------------------
PW, PH = BOARD_W + 2 * BOARD_CLR, BOARD_H + 2 * BOARD_CLR                   # board pocket
SW = GX1 - GX0 + 2 * STENCIL_MARGIN + STENCIL_DW                            # stencil
SH = GY1 - GY0 + 2 * STENCIL_MARGIN + STENCIL_DH
QW, QH = SW + 2 * STENCIL_CLR, SH + 2 * STENCIL_CLR                         # stencil pocket
JW, JH = QW + 2 * FRAME, QH + 2 * FRAME                                     # jig
Z_SHOULDER = BASE_T + BOARD_DEPTH
Z_TOP = Z_SHOULDER + LIP_H
NOTCH_Y = (GY1 - GY0) / 2 - NOTCH_UNDER                                     # inner end of the notch


def block(w, h, z0, z1, x=0.0, y=0.0):
    return cq.Workplane("XY").box(w, h, z1 - z0, centered=(True, True, False)).translate((x, y, z0))


def rounded(w, h, r, z0, z1, x=0.0, y=0.0):
    return block(w, h, z0, z1, x, y).edges("|Z").fillet(r)


def corner_reliefs(w, h, z0, z1, x=0.0, y=0.0):
    return (cq.Workplane("XY").rect(w, h, forConstruction=True).vertices()
            .circle(RELIEF_D / 2).extrude(z1 - z0).translate((x, y, z0)))


def notch_profile(s):
    """Plan view of one finger notch, flared where it leaves the jig; s = +1 top, -1 bottom."""
    h, r, f = NOTCH_W / 2, NOTCH_R, 3.0
    d = JH / 2 - NOTCH_Y                        # inner end -> outer edge of the jig
    k = 2 ** -0.5
    w = (cq.Workplane("XY").moveTo(-h + r, 0).lineTo(h - r, 0)
         .threePointArc((h - r + r * k, r - r * k), (h, r))
         .lineTo(h, d - f)
         .threePointArc((h + f - f * k, d - f + f * k), (h + f, d))
         .lineTo(h + f, d + 5).lineTo(-h - f, d + 5).lineTo(-h - f, d)
         .threePointArc((-h - f + f * k, d - f + f * k), (-h, d - f))
         .lineTo(-h, r)
         .threePointArc((-h + r - r * k, r - r * k), (-h + r, 0))
         .close().extrude(Z_TOP + 2).translate((NOTCH_X, NOTCH_Y, -1)))
    return w if s > 0 else w.mirror("XZ")


def build():
    jig = block(JW, JH, 0, Z_TOP).edges("|Z").fillet(OUTER_R)
    jig = jig.faces("<Z or >Z").edges().chamfer(0.6)

    # board pocket
    jig = jig.cut(block(PW, PH, BASE_T, Z_TOP + 1))
    jig = jig.cut(corner_reliefs(PW, PH, BASE_T, Z_SHOULDER + 0.01))

    # stencil pocket, with a lead-in chamfer
    jig = jig.cut(block(QW, QH, Z_SHOULDER, Z_TOP + 1, STENCIL_DX, STENCIL_DY))
    jig = jig.cut(corner_reliefs(QW, QH, Z_SHOULDER, Z_TOP + 1, STENCIL_DX, STENCIL_DY))
    lead = (cq.Workplane("XY").rect(QW, QH).workplane(offset=LIP_LEAD)
            .rect(QW + 2 * LIP_LEAD, QH + 2 * LIP_LEAD).loft(combine=True))
    jig = jig.cut(lead.translate((STENCIL_DX, STENCIL_DY, Z_TOP - LIP_LEAD)))

    # lip openings on the short sides: tape lands flat, squeegee can run off
    jig = jig.cut(block(JW + 2, SIDE_GAP, Z_SHOULDER, Z_TOP + 1))

    # finger notches, straight through and open to the outside
    for s in (1, -1):
        jig = jig.cut(notch_profile(s))

    # orientation marker, visible with the jig empty
    text = cq.Workplane("XY").text("BACK SIDE UP", 8, 1.0, combine=False).translate((0, 0, BASE_T - 0.4))
    return jig.cut(text)


# ----------------------------------------------------------------------------
# Checks
# ----------------------------------------------------------------------------
def overlap(a, b):
    return sum(s.Volume() for s in a.intersect(b).solids().vals())


def require(ok, message):
    if not ok:
        raise SystemExit(f"CHECK FAILED: {message}")


def check(jig):
    solid = jig.solids().vals()
    require(len(solid) == 1 and solid[0].isValid(), "jig is not one valid solid")

    require(all(abs(x) + w / 2 < BOARD_W / 2 and abs(y) + h / 2 < BOARD_H / 2
                for _, x, y, w, h in APERTURES), "aperture outside the board")

    # the board must be supported under every aperture
    room = [(NOTCH_Y - (abs(y) + h / 2), ref) for ref, x, y, w, h in APERTURES
            if abs(x - NOTCH_X) < NOTCH_W / 2 + w / 2]
    tight = min(room) if room else (float("inf"), "-")
    require(tight[0] > 1.0, f"{tight[1]} paste is too close to a finger notch")

    # largest in-tolerance board (+0.2 outline), thinnest and thickest, with the stencil on it
    proud = [BOARD_T * k - BOARD_DEPTH for k in (0.9, 1.1)]
    require(proud[0] > 0, "a thin board sits below the shoulder")
    require(proud[1] + FOIL_T < LIP_H, "the stencil rides above the lip")
    for p in proud:
        t = BOARD_DEPTH + p
        board = rounded(BOARD_W + 0.2, BOARD_H + 0.2, BOARD_R, BASE_T, BASE_T + t)
        foil = block(SW, SH, BASE_T + t, BASE_T + t + FOIL_T, STENCIL_DX, STENCIL_DY)
        require(overlap(board, jig) < 1e-3, "board does not fit its pocket")
        require(overlap(foil, jig) < 1e-3, "stencil does not fit its pocket")
    # the marker text must be engraved, not raised
    require(jig.val().BoundingBox().zmax <= Z_TOP + 1e-6, "jig too tall")

    xs = [x + s * w / 2 for _, x, _, w, _ in APERTURES for s in (1, -1)]
    ys = [y + s * h / 2 for _, _, y, _, h in APERTURES for s in (1, -1)]
    print(f"board   {BOARD_W:.2f} x {BOARD_H:.2f}   pocket {PW:.2f} x {PH:.2f} x {BOARD_DEPTH}")
    print(f"stencil {SW:.2f} x {SH:.2f}  pocket {QW:.2f} x {QH:.2f} x {LIP_H}")
    print(f"jig     {JW:.1f} x {JH:.1f} x {Z_TOP:.1f}, {solid[0].Volume() / 1000:.0f} cm3 solid")
    print(f"paste   {len(APERTURES)} openings, x {min(xs):.1f}..{max(xs):.1f}, y {min(ys):.1f}..{max(ys):.1f}"
          f" ({max(ys) - min(ys):.1f} tall); nearest to a notch: {tight[1]}, {tight[0]:.2f} clear")
    print(f"board sits proud of the shoulder by {proud[0]:.2f}..{proud[1]:.2f}")


# ----------------------------------------------------------------------------
# Previews
# ----------------------------------------------------------------------------
def render(jig):
    import tempfile
    import pyvista as pv
    pv.OFF_SCREEN = True

    def mesh(wp):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "m.stl")
            cq.exporters.export(wp, p, tolerance=0.05, angularTolerance=0.2)
            return pv.read(p)

    zb = BASE_T + BOARD_T

    def flashes(z0, z1, dx=0.0, dy=0.0):
        wp = cq.Workplane("XY")
        for _, x, y, w, h in APERTURES:
            wp = wp.add(block(w, h, z0, z1, x + dx, y + dy).val())
        return cq.Workplane(cq.Compound.makeCompound(wp.vals()))

    foil = block(SW, SH, zb, zb + FOIL_T, STENCIL_DX, STENCIL_DY).cut(flashes(zb - 1, zb + 1))
    J = (mesh(jig), "#e9e6df")
    B = (mesh(rounded(BOARD_W, BOARD_H, BOARD_R, BASE_T, zb)), "#2e6b3f")
    P = (mesh(flashes(zb - 0.1, zb + 0.03)), "#d4b04a")
    F = (mesh(foil), "#aab2bd")

    def shot(name, items, pos, zoom=1.2, up=(0, 1, 0), clip=None, focus=(0, 0, 3)):
        p = pv.Plotter(off_screen=True, window_size=(1500, 1100))
        p.set_background("white")
        for m, col in items:
            if clip:
                m = m.clip(normal=clip[0], origin=clip[1])
            p.add_mesh(m, color=col, smooth_shading=True, split_sharp_edges=True,
                       specular=0.2, ambient=0.25)
        p.camera_position = [pos, focus, up]
        p.camera.parallel_projection = True
        p.reset_camera()
        p.camera.zoom(zoom)
        p.enable_anti_aliasing("ssaa")
        p.screenshot(os.path.join(OUT, f"preview_{name}.png"))
        p.close()

    iso = (60, -190, 190)
    shot("empty", [J], iso)
    shot("board", [J, B, P], iso)
    shot("stencil", [J, B, P, F], iso)
    shot("top", [J, B, P, F], (0, 0, 400))
    # section through the finger notches, looking along X
    shot("section_notch", [J, B, F], (400, 0, 3), up=(0, 0, 1), clip=((1, 0, 0), (NOTCH_X, 0, 0)))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--render", action="store_true", help="also write preview PNGs")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    jig = build()
    check(jig)
    cq.exporters.export(jig, os.path.join(OUT, "paste_jig.step"))
    cq.exporters.export(jig, os.path.join(OUT, "paste_jig.stl"), tolerance=0.05, angularTolerance=0.2)
    if args.render:
        render(jig)
