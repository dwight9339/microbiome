"""Render preview images of the enclosure concept (needs pyvista).

python render.py   -> out/preview_*.png
"""
import os
import tempfile

import cadquery as cq
import pyvista as pv

import enclosure as e

pv.OFF_SCREEN = True
OUT = e.OUT
os.makedirs(OUT, exist_ok=True)


def mesh(shape, tol=0.08):
    """CadQuery shape (Workplane / Shape / list of solids) -> pyvista mesh, stood upright."""
    if isinstance(shape, list):
        shape = cq.Compound.makeCompound(shape)
    if isinstance(shape, cq.Workplane):
        shape = shape.val()
    shape = shape.rotate((0, 0, 0), (1, 0, 0), -e.TILT)
    with tempfile.NamedTemporaryFile(suffix=".stl", delete=False) as f:
        path = f.name
    shape.exportStl(path, tolerance=tol, angularTolerance=0.25)
    m = pv.read(path)
    os.unlink(path)
    return m


parts = e.build_all()
el = e.load_electronics()
print({k: len(v) for k, v in el.items()})

SHELL, BEZEL, CAPS = "#e9e6df", "#d9d5cc", "#3d6f8e"
scene = {
    "rear": (mesh(parts["rear_shell"]), SHELL),
    "bezel": (mesh(parts["front_bezel"]), BEZEL),
    "carrier": (mesh(el["carrier"]), "#2e6b3f"),
    "display": (mesh(el["display"]), "#1b1d22"),
    "feather": (mesh(el["feather"]), "#22252b"),
    "parts": (mesh(el["parts"]), "#8a8f98"),
    "battery": (mesh(e.battery()), "#3a78c2"),
}
caps = None
for (x, y) in e.BUTTONS.values():
    m = mesh(parts["button_cap"].translate((x, y, 0)))
    caps = m if caps is None else caps + m
scene["caps"] = (caps, CAPS)
sp = None
for (x, y) in e.MOUNT_HOLES:
    m = mesh(parts["display_spacer"].translate((x, y, 0.05)))
    sp = m if sp is None else sp + m
scene["spacers"] = (sp, "#c9b458")


def shot(name, show, cam, clip=None, offsets=None, size=(1500, 1100), zoom=1.25):
    p = pv.Plotter(off_screen=True, window_size=size)
    p.set_background("white")
    for key in show:
        m, col = scene[key]
        if offsets and key in offsets:
            m = m.translate(offsets[key], inplace=False)
        if clip:
            m = m.clip(normal=clip[0], origin=clip[1])
        p.add_mesh(m, color=col, smooth_shading=True, split_sharp_edges=True,
                   specular=0.2, ambient=0.25)
    p.camera_position = cam
    p.camera.parallel_projection = True
    p.reset_camera()
    p.camera.zoom(zoom)
    p.enable_anti_aliasing("ssaa")
    path = os.path.join(OUT, f"preview_{name}.png")
    p.screenshot(path)
    p.close()
    print("wrote", path)


ALL = list(scene)
C0 = (0, -8, 0)
shot("front", ALL, [(140, 60, 330), C0, (0, 1, 0)])
shot("rear", ALL, [(170, 70, -330), C0, (0, 1, 0)])
shot("section_feather", ALL, [(-400, 0, 0), C0, (0, 1, 0)], clip=((-1, 0, 0), (-20, 0, 0)))
shot("section_buttons", ALL, [(-400, 0, 0), C0, (0, 1, 0)], clip=((-1, 0, 0), (e.BUTTONS["centre"][0], 0, 0)))
shot("open", [k for k in ALL if k != "bezel"], [(120, 90, 330), C0, (0, 1, 0)])
shot("rear_inside", ["rear", "caps", "battery"], [(60, 110, 330), C0, (0, 1, 0)])
