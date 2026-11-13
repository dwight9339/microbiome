"""Microbiome prototype 1 enclosure -- concept v1 (CadQuery).

Run:  python enclosure.py            -> writes STEP/STL into ./out
      python enclosure.py --check    -> also runs the interference check

Device frame (everything is modelled un-tilted, in this frame):
  X = right when looking at the screen, Y = up, Z = towards the viewer.
  Origin = centre of the carrier PCB, on its FRONT (display-side) face.
Parts export in this device frame; previews apply TILT for the standing desk view.

All dimensions in mm.  Change the parameters below and re-run.
"""
import argparse
import json
import math
import os
import re
import sys

import cadquery as cq

HERE = os.path.dirname(os.path.abspath(__file__))
PCB_STEP = os.path.join(HERE, "..", "prototype_1", "prototype_1_pcb.step")
PCB_FILE = os.path.join(HERE, "..", "prototype_1", "prototype_1.kicad_pcb")
OUT = os.path.join(HERE, "out")

# ----------------------------------------------------------------------------
# Parameters
# ----------------------------------------------------------------------------
# Carrier outline, thickness and feature positions are read from PCB_FILE below.

# Confirmed direct-soldered stack-ups: carrier surface -> module PCB underside.
# 2.6 = modules soldered straight onto 2.54 mm pin headers (the header plastic is
# the spacer).  Socketed modules would be ~11.0 (8.5 mm socket + 2.5 mm plastic).
DISPLAY_GAP = 2.6
FEATHER_GAP = 2.6

# Display module (MSP3223), device coords
DISP_STACK = 5.66                           # module PCB underside -> lens front
# Active area relative to the J2 footprint origin, in KiCad axes at 90 degrees.
# These offsets describe the asymmetric LCD glass on the MSP3223 module.
LCD_DX0, LCD_DX1, LCD_HALF_H = -35.36, 29.44, 24.3
WINDOW_MARGIN = 0.8

# Feather RP2350 (back side)
FEATHER_THICK = 6.37                        # underside -> tallest part (JST)
USB_CY_OFFSET = -1.0595                    # USB centre y relative to J1, KiCad axes
USB_W, USB_H = 13.0, 7.5                    # opening for a plug overmold

# Back-side controls.  Button and speaker positions are read from the .kicad_pcb
# (see read_pcb below), so moving them in KiCad moves them here.
SLIDE_Z0, SLIDE_Z1 = -5.2, -1.5
SPK_BODY = (11.0, 15.0)                     # speaker body in footprint x, y
SPK_THICK = 3.0
SPK_SPRING = 0.5                            # compressed spring-contact height
SPK_WALL = 1.2                              # wall of the speaker pocket / duct

# Battery (cylindrical 18650-size pack with JST-PH lead)
BATT_D, BATT_L = 18.6, 69.0

# Shell
WALL = 2.4
FRONT_T = 1.6                               # bezel face thickness
FLOOR_T = 2.5
SIDE_CLR = 2.2                              # board edge -> side wall (clears USB-C overhang)
TOP_CLR = 0.4                               # board top edge -> top wall (slide switch reach)
REAR_CLR = 1.0                              # Feather -> rear wall
TILT = 12.0                                 # degrees the screen leans back
FLARE_F, FLARE_R = 4.0, 16.0                # how far the foot sticks out front / rear
FOOT_H_F, FOOT_H_R, FOOT_LIP = 9.0, 14.0, 3.0
BEZEL_Y0 = -34.0                            # bottom edge of the removable bezel
FIT = 0.15                                  # general printed-fit clearance per side
BOSS_D, INSERT_D, INSERT_DEPTH = 7.5, 4.2, 7.0   # M3 heat-set insert bosses

# ----------------------------------------------------------------------------
# Positions from the board file
# ----------------------------------------------------------------------------
def _read_board(path=PCB_FILE):
    """Read KiCad's S-expression without depending on indentation or line endings."""
    with open(path, encoding="utf-8") as f:
        tokens = re.findall(r'"(?:\\.|[^"\\])*"|[()]|[^\s()]+', f.read())
    root, stack = [], []
    current = root
    for token in tokens:
        if token == "(":
            node = []
            current.append(node)
            stack.append(current)
            current = node
        elif token == ")":
            current = stack.pop()
        else:
            current.append(json.loads(token) if token.startswith('"') else token)
    if stack or len(root) != 1 or root[0][0] != "kicad_pcb":
        raise ValueError("Expected a complete KiCad PCB file")
    return root[0]


def _children(node, kind):
    return [n for n in node if isinstance(n, list) and n[0] == kind]


def _child(node, kind):
    return next(iter(_children(node, kind)))


BOARD = _read_board()
FOOTPRINTS = {next(p[2] for p in _children(f, "property") if p[1] == "Reference"): f
              for f in _children(BOARD, "footprint")}


def _outline():
    edges = [n for n in BOARD if isinstance(n, list) and n[0].startswith("gr_")
             and _children(n, "layer") and _child(n, "layer")[1] == "Edge.Cuts"]
    if len(edges) != 1 or edges[0][0] != "gr_rect":
        raise ValueError("Enclosure currently requires one rectangular Edge.Cuts outline")
    a, b = _child(edges[0], "start"), _child(edges[0], "end")
    x0, x1 = sorted((float(a[1]), float(b[1])))
    y0, y1 = sorted((float(a[2]), float(b[2])))
    return x0, x1, y0, y1


PCB_X0, PCB_X1, PCB_Y0, PCB_Y1 = _outline()
PCB_W, PCB_H = PCB_X1 - PCB_X0, PCB_Y1 - PCB_Y0
PCB_T = float(_child(_child(BOARD, "general"), "thickness")[1])
KICAD_CX, KICAD_CY = (PCB_X0 + PCB_X1) / 2, (PCB_Y0 + PCB_Y1) / 2
BUTTON_TOP = -(PCB_T + 7.38)
USB_Z = -(PCB_T + FEATHER_GAP + 2.68)


def pad_position(footprint, pad):
    at = _child(footprint, "at")
    x, y = float(at[1]), float(at[2])
    rot = math.radians(float(at[3]) if len(at) > 3 else 0)
    px, py = map(float, _child(pad, "at")[1:3])
    return x + px * math.cos(rot) + py * math.sin(rot), y - px * math.sin(rot) + py * math.cos(rot)


def read_pcb():
    """{reference: (x, y, rotation, [electrical pad centres])} in KiCad coordinates."""
    out = {}
    for ref, f in FOOTPRINTS.items():
        at = _child(f, "at")
        x, y = map(float, at[1:3])
        rot = float(at[3]) if len(at) > 3 else 0.0
        pads = [pad_position(f, p) for p in _children(f, "pad") if p[2] in ("thru_hole", "smd")]
        out[ref] = (x, y, rot, pads)
    return out


def to_dev(kx, ky):
    return (kx - KICAD_CX, KICAD_CY - ky)


def _buttons(pcb):
    pts = []
    for ref in ("SW1", "SW2", "SW3", "SW4", "SW5"):
        pads = pcb[ref][3]
        pts.append(to_dev(sum(p[0] for p in pads) / len(pads), sum(p[1] for p in pads) / len(pads)))
    mx, my = sum(p[0] for p in pts) / 5, sum(p[1] for p in pts) / 5
    centre = min(pts, key=lambda p: math.hypot(p[0] - mx, p[1] - my))
    rest = [p for p in pts if p is not centre]
    return {"centre": centre, "up": max(rest, key=lambda p: p[1]), "down": min(rest, key=lambda p: p[1]),
            "left": min(rest, key=lambda p: p[0]), "right": max(rest, key=lambda p: p[0])}


def _speaker(pcb):
    x, y, rot, _ = pcb["LS1"]
    w, h = SPK_BODY if round(rot) % 180 == 0 else SPK_BODY[::-1]
    cx, cy = to_dev(x, y)
    return cx - w / 2, cx + w / 2, cy - h / 2, cy + h / 2


PCB = read_pcb()
# The fixed glass/USB offsets and top-facing slide opening assume these orientations.
for ref, rotation in (("J2", 90), ("J1", 0), ("SW6", 0)):
    if not math.isclose(PCB[ref][2] % 360, rotation):
        raise ValueError(f"Update enclosure feature orientation for rotated {ref}")
DISP_CX, DISP_CY = to_dev(*PCB["J2"][:2])
ACTIVE_X0, ACTIVE_X1 = DISP_CX + LCD_DX0, DISP_CX + LCD_DX1
ACTIVE_Y0, ACTIVE_Y1 = DISP_CY - LCD_HALF_H, DISP_CY + LCD_HALF_H
MOUNT_HOLES = [to_dev(*pad_position(FOOTPRINTS["J2"], p))
               for p in _children(FOOTPRINTS["J2"], "pad") if p[2] == "np_thru_hole"]
if len(MOUNT_HOLES) != 4:
    raise ValueError("Expected four display mounting holes")
FEATHER_CY = to_dev(PCB["J1"][0], PCB["J1"][1] + USB_CY_OFFSET)[1]
SLIDE_X = to_dev(*PCB["SW6"][:2])[0]
BUTTONS = _buttons(PCB)
SPK_X0, SPK_X1, SPK_Y0, SPK_Y1 = _speaker(PCB)

# ----------------------------------------------------------------------------
# Derived
# ----------------------------------------------------------------------------
S, C = math.sin(math.radians(TILT)), math.cos(math.radians(TILT))
W = PCB_W + 2 * SIDE_CLR + 2 * WALL                       # outer width
ZF = DISPLAY_GAP + DISP_STACK + 0.3 + FRONT_T             # outer front face
ZB = -(PCB_T + FEATHER_GAP + FEATHER_THICK + REAR_CLR + WALL)   # outer rear face
Y1 = PCB_H / 2 + TOP_CLR + WALL                           # outer top face
SPLIT_Z = ZF - 6.0                                        # bezel / rear-shell split plane
BATT_Z = (ZB + WALL + ZF - FRONT_T) / 2                   # battery axis: mid-depth of the cavity
BUTTON_GAP = BUTTON_TOP - (ZB + WALL)                     # rear wall (inside) -> switch tip
GUIDE = min(5.0, max(0.0, BUTTON_GAP - 2.6))              # depth of the D-pad guide block
BATT_Y = -(PCB_H / 2 + 1.0 + BATT_D / 2)                  # battery axis, device y
H_FLOOR = BATT_Y * C + BATT_Z * S - BATT_D / 2 - 0.5 - FLOOR_T   # world height of the desk
Y0 = (H_FLOOR - ZB * S) / C                               # rear-bottom corner of the slab


def dev(f, h):
    """World (forward, height) -> device (y, z)."""
    return (h * C - f * S, h * S + f * C)


def world(y, z):
    """Device (y, z) -> world (forward, height)."""
    return (-y * S + z * C, y * C + z * S)


def yz_prism(pts, x0, x1):
    """Extrude a polygon given in device (y, z) from x0 to x1."""
    return (cq.Workplane("YZ").polyline(pts).close().extrude(x1 - x0)
            .translate((x0, 0, 0)))


def box(x0, x1, y0, y1, z0, z1):
    return (cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False)
            .translate((x0, y0, z0)))


def cyl_z(x, y, d, z0, z1):
    return cq.Workplane("XY").circle(d / 2).extrude(z1 - z0).translate((x, y, z0))


# ----------------------------------------------------------------------------
# Outer body and cavity
# ----------------------------------------------------------------------------
def outer_profile():
    f_rb, _ = world(Y0, ZB)                     # rear-bottom corner (on the floor)
    y_fb = (H_FLOOR - ZF * S) / C               # where the front face meets the floor
    f_fb, _ = world(y_fb, ZF)
    y_rf = Y0 + FOOT_H_R / C                    # foot meets rear face here
    y_ff = y_fb + FOOT_H_F / C                  # foot meets front face here
    return [
        dev(f_rb - FLARE_R, H_FLOOR),
        dev(f_rb - FLARE_R, H_FLOOR + FOOT_LIP),
        (y_rf, ZB),
        (Y1, ZB),
        (Y1, ZF),
        (y_ff, ZF),
        dev(f_fb + FLARE_F, H_FLOOR + FOOT_LIP),
        dev(f_fb + FLARE_F, H_FLOOR),
    ]


def cavity_profile():
    zr, zf = ZB + WALL, ZF - FRONT_T
    h = H_FLOOR + FLOOR_T
    return [((h - zr * S) / C, zr), ((h - zf * S) / C, zf), (Y1 - WALL, zf), (Y1 - WALL, zr)]


def build_body():
    body = yz_prism(outer_profile(), -W / 2, W / 2)
    for r in (3.0, 2.0, 1.2):                   # round the left/right silhouette
        try:
            body = body.faces("<X or >X").edges().fillet(r)
            break
        except Exception:
            continue
    try:                                        # soften the long edges
        body = body.edges("|X").fillet(1.2)
    except Exception:
        pass
    cavity = yz_prism(cavity_profile(), -W / 2 + WALL, W / 2 - WALL)
    return body, cavity


# ----------------------------------------------------------------------------
# Parts
# ----------------------------------------------------------------------------
def bezel_region():
    return box(-W, W, BEZEL_Y0, Y1 + 50, SPLIT_Z, ZF + 50)


def build_rear(body, cavity):
    inner_rear = ZB + WALL
    rear = body.cut(cavity).cut(bezel_region())

    # --- board bosses (M3 heat-set inserts), screws come in from the display side
    for (x, y) in MOUNT_HOLES:
        rear = rear.union(cyl_z(x, y, BOSS_D, inner_rear - 0.1, -PCB_T))
    for (x, y) in MOUNT_HOLES:
        rear = rear.cut(cyl_z(x, y, INSERT_D, -PCB_T - INSERT_DEPTH, -PCB_T + 0.1))

    # --- speaker tower: pocket holds the speaker against its PCB pads, duct to the grille
    seat = -(PCB_T + SPK_SPRING + SPK_THICK)            # speaker rear face
    rim = seat + SPK_THICK - 0.5                        # pocket rim (clear of the PCB)
    px0, px1, py0, py1 = SPK_X0 - FIT, SPK_X1 + FIT, SPK_Y0 - FIT, SPK_Y1 + FIT
    tower = box(px0 - SPK_WALL, px1 + SPK_WALL, py0 - SPK_WALL, py1 + SPK_WALL, inner_rear - 0.1, rim)
    rear = rear.union(tower)
    rear = rear.cut(box(px0, px1, py0, py1, seat, rim + 0.1))                       # pocket
    rear = rear.cut(box(px0 + 1.5, px1 - 1.5, py0 + 1.5, py1 - 1.5, inner_rear, seat + 0.1))  # duct
    cx = (SPK_X0 + SPK_X1) / 2
    for i in range(4):                                  # grille slots
        y = SPK_Y0 + 2.6 + i * 2.0
        rear = rear.cut(box(cx - 5, cx + 5, y, y + 1.1, ZB - 1, inner_rear + 0.1))

    # --- D-pad guide block and holes
    block_z = inner_rear + GUIDE
    for (x, y) in BUTTONS.values():
        rear = rear.union(cyl_z(x, y, 11.0, inner_rear - 0.1, block_z))
    bx, by = BUTTONS["centre"]
    rear = rear.union(box(BUTTONS["left"][0], BUTTONS["right"][0], by - 5.5, by + 5.5,
                          inner_rear - 0.1, block_z))
    rear = rear.union(box(bx - 5.5, bx + 5.5, BUTTONS["down"][1], BUTTONS["up"][1],
                          inner_rear - 0.1, block_z))
    for (x, y) in BUTTONS.values():
        rear = rear.cut(cyl_z(x, y, CAP_D + 2 * 0.2, ZB - 1, block_z + 0.1))

    # --- USB-C opening (left wall)
    usb = (cq.Workplane("YZ").rect(USB_W, USB_H).extrude(WALL + 2)
           .edges("|X").fillet(2.0).translate((-W / 2 - 1, FEATHER_CY, USB_Z)))
    rear = rear.cut(usb)

    # --- power slide switch: slot through the top wall plus a thumb scoop
    rear = rear.cut(box(SLIDE_X - 3.6, SLIDE_X + 3.6, Y1 - WALL - 1, Y1 + 1, SLIDE_Z0, SLIDE_Z1))
    rear = rear.cut(box(SLIDE_X - 7, SLIDE_X + 7, Y1 - 1.2, Y1 + 1, SLIDE_Z0 - 2.5, SLIDE_Z1 + 2.5))

    # --- battery cradle: two saddle ribs on the floor
    for x in (-22.0, 22.0):
        rib = box(x - 1.5, x + 1.5, Y0 - 30, BATT_Y, BATT_Z - 12, BATT_Z + 12).intersect(cavity)
        rear = rear.union(rib.cut(cq.Workplane("YZ").circle(BATT_D / 2 + 0.3).extrude(10)
                                  .translate((x - 5, BATT_Y, BATT_Z))))
    return rear


def build_bezel(body, cavity):
    bezel = body.cut(cavity).intersect(bezel_region())
    # window over the LCD active area, chamfered on the outside
    x0, x1 = ACTIVE_X0 - WINDOW_MARGIN, ACTIVE_X1 + WINDOW_MARGIN
    y0, y1 = ACTIVE_Y0 - WINDOW_MARGIN, ACTIVE_Y1 + WINDOW_MARGIN
    bezel = bezel.cut(box(x0, x1, y0, y1, ZF - FRONT_T - 1, ZF + 1))
    try:
        bezel = bezel.faces(">Z").edges("not(<X or >X or <Y or >Y)").chamfer(0.8)
    except Exception:
        pass
    # locating tongue: slips inside the rear shell on the left, right and top
    xi, yt = W / 2 - WALL - FIT, Y1 - WALL - FIT
    t = 1.2
    tongue = (box(-xi, xi, BEZEL_Y0 + 2, yt, SPLIT_Z - 3, SPLIT_Z + 0.1)
              .cut(box(-xi + t, xi - t, BEZEL_Y0, yt - t, SPLIT_Z - 4, SPLIT_Z + 1)))
    # Bridge the fit gap above the split: without this shoulder the locating
    # tongue is a disconnected solid, even though it looks attached in previews.
    shoulder = (box(-xi - FIT - 0.4, xi + FIT + 0.4, BEZEL_Y0 + 2,
                    yt + FIT + 0.4, SPLIT_Z, SPLIT_Z + 1.2)
                .cut(box(-xi + t, xi - t, BEZEL_Y0, yt - t,
                         SPLIT_Z - 0.1, SPLIT_Z + 1.3)))
    return bezel.union(tongue).union(shoulder)


CAP_D = 7.6


def build_cap():
    """D-pad button cap, modelled at the origin with its outer face at z=0 (print 5)."""
    out = ZB - 1.2                                      # cap face proud of the rear wall
    flange_z = ZB + WALL + GUIDE                        # sits on the inside of the guide block
    tip = BUTTON_TOP - 0.2
    cap = cq.Workplane("XY").circle(CAP_D / 2).extrude(flange_z - out)
    cap = cap.faces("<Z").edges().fillet(0.8)
    cap = cap.union(cq.Workplane("XY").circle(4.4).extrude(1.2).translate((0, 0, flange_z - out)))
    if tip - flange_z - 1.2 > 0.05:
        cap = cap.union(cq.Workplane("XY").circle(2.5).extrude(tip - flange_z - 1.2)
                        .translate((0, 0, flange_z - out + 1.2)))
    return cap.translate((0, 0, out))                  # now in device z, at x=y=0


def build_spacer():
    """Display stand-off, one per mounting hole."""
    return (cq.Workplane("XY").circle(3.25).circle(1.7).extrude(DISPLAY_GAP - 0.1))


def battery():
    return (cq.Workplane("YZ").circle(BATT_D / 2).extrude(BATT_L)
            .translate((-BATT_L / 2, BATT_Y, BATT_Z)))


# ----------------------------------------------------------------------------
# Board + modules, re-stacked for direct soldering (fit checks and pictures)
# ----------------------------------------------------------------------------
def load_electronics():
    """Returns {name: [solids]} in device coords with direct-soldered gaps applied."""
    if os.path.getmtime(PCB_STEP) + 1 < os.path.getmtime(PCB_FILE):
        raise ValueError("PCB STEP is stale; re-export the saved PCB before checking/rendering")
    solids = cq.importers.importStep(PCB_STEP).val().Solids()
    carriers = [s for s in solids if
                math.isclose(s.BoundingBox().xlen, PCB_W, abs_tol=0.05) and
                math.isclose(s.BoundingBox().ylen, PCB_H, abs_tol=0.05) and
                abs(s.BoundingBox().center.x - KICAD_CX) < 0.05 and
                abs(s.BoundingBox().center.y + KICAD_CY) < 0.05 and
                s.BoundingBox().zlen < PCB_T + 0.1]
    if len(carriers) != 1:
        raise ValueError("STEP carrier outline does not match PCB_FILE; re-export the PCB")
    carrier = carriers[0]
    front = carrier.BoundingBox().zmax
    groups = {"carrier": [], "display": [], "feather": [], "parts": []}
    # KiCad positions models 0.085 mm beyond the exported substrate faces.
    # Honour the model offsets saved in each footprint instead of assuming a gap.
    display_model = _children(FOOTPRINTS["J2"], "model")[0]
    feather_model = next(m for m in _children(FOOTPRINTS["J1"], "model")
                         if "6000 Feather" in m[1])
    raw_display_bottom = front + 0.085 + float(_child(_child(display_model, "offset"), "xyz")[3])
    raw_feather_bottom = -0.085 - float(_child(_child(feather_model, "offset"), "xyz")[3])
    fx, fy = PCB["J1"][:2]
    for s in solids:
        b = s.BoundingBox()
        dz = -front
        if s is carrier:
            name = "carrier"
        elif b.zmin >= front - 0.01 and b.zlen < 6:
            name, dz = "display", DISPLAY_GAP - raw_display_bottom
        elif (b.zmax <= 0.05 and fx - 28 < b.center.x < fx + 26
              and -fy - 13 < b.center.y < -fy + 15 and b.zmin < -2.6):
            name, dz = "feather", -PCB_T - FEATHER_GAP - raw_feather_bottom
        else:
            name = "parts"
            if b.zmax <= 0.9:
                dz = -PCB_T
        groups[name].append(s.translate((-KICAD_CX, KICAD_CY, dz)))
    return groups


INTERFERENCE_TOL = 0.01                    # ignore numerical slivers below 0.01 mm3


def interference(part, solids, tol=INTERFERENCE_TOL):
    hits = []
    pb = part.val().BoundingBox()
    for s in solids:
        b = s.BoundingBox()
        if (b.xmin > pb.xmax or b.xmax < pb.xmin or b.ymin > pb.ymax or b.ymax < pb.ymin
                or b.zmin > pb.zmax or b.zmax < pb.zmin):
            continue
        # A failed Boolean must not be reported as a clear fit.
        v = part.val().intersect(s).Volume()
        if v > tol:
            c = b.center
            hits.append((round(v, 1), round(c.x, 1), round(c.y, 1), round(c.z, 1)))
    return hits


def check_assembly(parts):
    """Check static assembly plus 0.6 mm button motion; return overlap count."""
    el = load_electronics()
    electronics = [s for group in el.values() for s in group]
    caps = {name: parts["button_cap"].translate((x, y, 0))
            for name, (x, y) in BUTTONS.items()}
    spacers = [parts["display_spacer"].translate((x, y, 0.05)).val()
               for x, y in MOUNT_HOLES]
    batt = battery().val()
    reports = {}

    def check(label, part, obstacles):
        hits = interference(part, obstacles)
        reports[label] = hits
        print(f"interference {label}: {len(hits)}", hits[:12])

    for name in ("rear_shell", "front_bezel"):
        other = "front_bezel" if name == "rear_shell" else "rear_shell"
        check(name, parts[name], electronics + [batt, parts[other].val()] + spacers
              + [cap.val() for cap in caps.values()])
    check("battery/electronics", battery(), electronics + spacers + [c.val() for c in caps.values()])
    for name, cap in caps.items():
        check(f"cap_{name}/rest", cap, electronics + spacers
              + [c.val() for n, c in caps.items() if n != name])
        # Switch contact during pressing is intentional; check the moving cap against
        # the enclosure and spacers, rather than treating actuation as a collision.
        check(f"cap_{name}/pressed", cap.translate((0, 0, 0.6)),
              [parts["rear_shell"].val(), parts["front_bezel"].val(), batt] + spacers)
    for i, spacer in enumerate(spacers):
        check(f"spacer_{i}/electronics", cq.Workplane(obj=spacer), electronics)
    with open(os.path.join(OUT, "fit_check.json"), "w", encoding="utf-8") as f:
        json.dump({"pcb_mm": [PCB_W, PCB_H, PCB_T], "outer_width_mm": W,
                   "direct_solder_gap_mm": {"display": DISPLAY_GAP, "feather": FEATHER_GAP},
                   "overlap_tolerance_mm3": INTERFERENCE_TOL,
                   "button_motion_mm": 0.6, "overlaps": reports}, f, indent=2)
    return sum(len(hits) for hits in reports.values())


def build_all():
    body, cavity = build_body()
    parts = {
        "rear_shell": build_rear(body, cavity),
        "front_bezel": build_bezel(body, cavity),
        "button_cap": build_cap(),
        "display_spacer": build_spacer(),
    }
    return parts


def upright(shape):
    """Device frame -> standing on the desk (rotate back by TILT about X)."""
    return shape.rotate((0, 0, 0), (1, 0, 0), -TILT)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="check assembly interference")
    parser.add_argument("--freecad", action="store_true", help="also build out/microbiome_assembly.FCStd")
    parser.add_argument("--freecad-python", help="FreeCAD Python executable (or set FREECAD_PYTHON)")
    args = parser.parse_args()
    os.makedirs(OUT, exist_ok=True)
    parts = build_all()
    f0, h0 = world(*outer_profile()[0])
    f1, _ = world(*outer_profile()[-1])
    _, h1 = world(Y1, ZF)
    print(f"outer: {W:.1f} wide, {h1 - H_FLOOR:.1f} tall, slab {ZF - ZB:.1f} deep, "
          f"footprint {f1 - f0:.1f} deep, tilt {TILT:g} deg")
    for name, p in parts.items():
        v = p.val()
        print(f"{name}: valid={v.isValid()} volume={v.Volume() / 1000:.1f} cm3")
        if not v.isValid() or len(v.Solids()) != 1:
            raise ValueError(f"{name} must be one valid connected solid")
        cq.exporters.export(p, os.path.join(OUT, f"{name}.step"))
        cq.exporters.export(p, os.path.join(OUT, f"{name}.stl"), tolerance=0.05, angularTolerance=0.2)

    if args.check:
        if check_assembly(parts):
            sys.exit("Assembly interference detected; see out/fit_check.json")

    if args.freecad or args.freecad_python:
        from freecad_assembly import export_assembly
        export_assembly(parts, sys.modules[__name__], args.freecad_python)
