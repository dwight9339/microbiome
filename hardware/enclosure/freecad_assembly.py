"""Bridge CadQuery's Python environment to FreeCAD's bundled Python.

The generated FCStd is a positioned inspection assembly using native App::Part
containers and Part::Feature solids, with no external geometry dependencies.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def freecad_python():
    configured = os.environ.get("FREECAD_PYTHON")
    if configured:
        return configured
    if os.name == "nt":
        candidates = sorted(Path(os.environ.get("ProgramFiles", "C:/Program Files"))
                            .glob("FreeCAD*/bin/python.exe"), reverse=True)
        if candidates:
            return str(candidates[0])
    raise RuntimeError("Set FREECAD_PYTHON to a Python interpreter that can import FreeCAD and FreeCADGui")


def export_assembly(parts, e, python_exe=None):
    """Export the same geometry/positions used by the enclosure fit check."""
    import cadquery as cq

    output = Path(e.OUT) / "microbiome_assembly.FCStd"
    with tempfile.TemporaryDirectory(prefix="microbiome-freecad-") as scratch:
        scratch = Path(scratch)
        entries = []

        def add(name, label, group, shape, color, position=(0, 0, 0)):
            if isinstance(shape, cq.Workplane):
                shape = shape.val()
            elif isinstance(shape, list):
                shape = cq.Compound.makeCompound(shape)
            shape.exportBrep(str(scratch / (name + ".brep")))
            placed = shape.translate(position)
            b = placed.BoundingBox()
            entries.append({"name": name, "label": label, "group": group,
                            "brep": name + ".brep", "color": color,
                            "position": position, "volume": placed.Volume(),
                            "solids": len(placed.Solids()),
                            "bounds": [b.xmin, b.xmax, b.ymin, b.ymax, b.zmin, b.zmax]})

        add("RearShell", "Rear shell", "Enclosure", parts["rear_shell"], [0.914, 0.902, 0.875])
        add("FrontBezel", "Front bezel", "Enclosure", parts["front_bezel"], [0.851, 0.835, 0.800])
        for name, xy in e.BUTTONS.items():
            add("Cap_" + name, "Button cap - " + name, "Controls", parts["button_cap"],
                [0.239, 0.435, 0.557], (*xy, 0))
        for i, xy in enumerate(e.MOUNT_HOLES, 1):
            add("Spacer_" + str(i), "Display spacer " + str(i), "Spacers", parts["display_spacer"],
                [0.788, 0.706, 0.345], (*xy, 0.05))
        add("BatteryEnvelope", "Battery envelope (18.6 x 69 mm)", "Battery", e.battery(), [0.227, 0.471, 0.761])
        el = e.load_electronics()
        for key, name, label, color in (
                ("carrier", "CarrierPCB", "Carrier PCB", [0.180, 0.420, 0.247]),
                ("display", "DisplayModule", "Display module (J2)", [0.106, 0.114, 0.133]),
                ("feather", "FeatherModule", "Feather RP2350 (J1)", [0.133, 0.145, 0.169]),
                ("parts", "PCBComponents", "Headers, controls and other components", [0.541, 0.561, 0.596])):
            add(name, label, "Electronics", el[key], color)
        manifest = {"output": str(output.resolve()), "pcb_step": str(Path(e.PCB_STEP).resolve()),
                    "pcb_sha256": hashlib.sha256(Path(e.PCB_STEP).read_bytes()).hexdigest(),
                    "tilt_deg": e.TILT, "display_gap_mm": e.DISPLAY_GAP,
                    "feather_gap_mm": e.FEATHER_GAP, "entries": entries}
        manifest_path = scratch / "assembly.json"
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
        if os.name == "nt":
            env["QT_QPA_FONTDIR"] = str(Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts")
        result = subprocess.run([python_exe or freecad_python(), str(Path(__file__).resolve()),
                                 "--build", str(manifest_path)], env=env, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("FreeCAD assembly export failed:\n" + result.stdout + result.stderr)
        print(result.stdout.strip())
    return output


def build_document(manifest_path):
    """Runs in FreeCAD's Python, never imports CadQuery."""
    import FreeCAD as App
    import FreeCADGui as Gui
    import Part

    manifest_path = Path(manifest_path)
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    # Offscreen GUI creates persistent view providers (colors, visibility, camera).
    Gui.showMainWindow()
    Gui.getMainWindow().hide()
    doc = App.newDocument("MicrobiomeAssembly")
    doc.Label = "Microbiome - PCB and enclosure"
    assembly = doc.addObject("App::Part", "Assembly")
    assembly.Label = "Microbiome assembly"
    assembly.addProperty("App::PropertyString", "AssemblyType", "Assembly")
    assembly.AssemblyType = "Positioned inspection assembly; no kinematic joints"
    assembly.addProperty("App::PropertyString", "CoordinateFrame", "Assembly")
    assembly.CoordinateFrame = "PCB front Z=0; X right, Y up, Z towards viewer; same frame as part STEP exports"
    for prop, value in (("DisplayGap", data["display_gap_mm"]), ("FeatherGap", data["feather_gap_mm"])):
        assembly.addProperty("App::PropertyLength", prop, "Assembly")
        setattr(assembly, prop, value)
    assembly.addProperty("App::PropertyAngle", "DeskTilt", "Assembly")
    assembly.DeskTilt = data["tilt_deg"]
    groups = {}
    for name in ("Enclosure", "Electronics", "Controls", "Spacers", "Battery"):
        group = doc.addObject("App::Part", name)
        group.Label = "PCB STEP and modules" if name == "Electronics" else name
        assembly.addObject(group)
        groups[name] = group
    electronics = groups["Electronics"]
    electronics.addProperty("App::PropertyFileIncluded", "OriginalPCBSTEP", "Source")
    electronics.OriginalPCBSTEP = data["pcb_step"]
    electronics.addProperty("App::PropertyString", "SourceSHA256", "Source")
    electronics.SourceSHA256 = data["pcb_sha256"]
    for entry in data["entries"]:
        obj = doc.addObject("Part::Feature", entry["name"])
        obj.Label = entry["label"]
        shape = Part.Shape()
        shape.read(str(manifest_path.parent / entry["brep"]))
        obj.Shape = shape
        obj.Placement = App.Placement(App.Vector(*entry["position"]), App.Rotation())
        obj.ViewObject.ShapeColor = tuple(entry["color"])
        obj.ViewObject.LineColor = (0.18, 0.18, 0.18)
        obj.ViewObject.DisplayMode = "Flat Lines"
        obj.ViewObject.Visibility = True
        groups[entry["group"]].addObject(obj)
    doc.recompute()
    # Camera axes are independent of world Z; keep device Y pointing up on screen.
    # A small oblique view shows the front and the right side, matching the previews.
    view = Gui.getDocument(doc.Name).activeView()
    view.setCameraOrientation(App.Rotation(App.Vector(0, 1, 0), 22).Q)
    view.setCameraType("Orthographic")
    view.fitAll()
    filename = manifest_path.parent / "microbiome_assembly.FCStd"
    doc.saveAs(str(filename))
    App.closeDocument(doc.Name)
    doc = App.openDocument(str(filename))
    # Reopen before delivering: ensure geometry, placement, colors and embedded
    # source survive serialization and do not depend on the temporary BREP files.
    for entry in data["entries"]:
        obj = doc.getObject(entry["name"])
        shape = obj.Shape
        assert not shape.isNull() and shape.isValid(), entry["name"]
        assert len(shape.Solids) == entry["solids"], entry["name"]
        assert math.isclose(shape.Volume, entry["volume"], rel_tol=1e-7, abs_tol=1e-5), entry["name"]
        b = shape.optimalBoundingBox(False)
        bounds = [b.XMin, b.XMax, b.YMin, b.YMax, b.ZMin, b.ZMax]
        assert all(abs(a - c) < 0.001 for a, c in zip(bounds, entry["bounds"])), (entry["name"], bounds, entry["bounds"], str(obj.Placement))
        assert all(abs(a - c) < 1e-6 for a, c in zip(obj.Placement.Base, entry["position"])), entry["name"]
        assert obj.ViewObject.Visibility, entry["name"]
        assert all(abs(a - c) < 0.001 for a, c in zip(obj.ViewObject.ShapeColor, entry["color"])), entry["name"]
    embedded = Path(doc.getObject("Electronics").OriginalPCBSTEP)
    assert hashlib.sha256(embedded.read_bytes()).hexdigest() == data["pcb_sha256"]
    App.closeDocument(doc.Name)
    os.replace(filename, data["output"])
    print(f"wrote {data['output']} ({len(data['entries'])} positioned components, embedded PCB STEP verified)")


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] != "--build":
        raise SystemExit("Run enclosure.py --freecad using the CadQuery environment")
    build_document(sys.argv[2])
