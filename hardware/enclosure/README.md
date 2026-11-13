# Enclosure (concept v1)

Parametric CadQuery enclosure for prototype 1, updated for the narrower carrier
and confirmed direct-soldered modules. Current outer dimensions are approximately
**105.7 mm wide, 86.5 mm tall, 24.1 mm slab depth**, with a 44.7 mm deep desk
footprint and 12 degree backward lean.

## Build and inspect

From the repository root, using a project-local environment:

    python -m venv build/enclosure-venv
    build/enclosure-venv/Scripts/python.exe -m pip install -r hardware/enclosure/requirements.txt
    build/enclosure-venv/Scripts/python.exe hardware/enclosure/enclosure.py --check --freecad
    build/enclosure-venv/Scripts/python.exe hardware/enclosure/render.py

On Linux/macOS use build/enclosure-venv/bin/python instead.
Before rebuilding after PCB changes, save and re-export the board STEP (for example
with ../prototype_1/kicad_check.bat). The model rejects an older STEP or an outline
that does not match the PCB, instead of silently moving selected stale parts.

The STEP files open directly in FreeCAD (File > Import). Parts are exported in
the un-tilted assembly frame: PCB front surface at Z=0, X right, Y up, Z forward.
The preview images rotate the complete assembly into its standing position.
STLs retain the same assembly frame; choose print orientation in the slicer.

## FreeCAD assembly

The --freecad option also builds **out/microbiome_assembly.FCStd**. It uses an
installed FreeCAD Python interpreter, automatically discovered under Program Files
on Windows. Override it with --freecad-python PATH or the FREECAD_PYTHON environment
variable. The export runs offscreen and does not open a visible FreeCAD window.
Without --freecad, the STEP/STL build does not require FreeCAD.

Open the FCStd directly in FreeCAD. It contains named, colored groups for the two
enclosure halves, the PCB and modules, all five caps, all four spacers, and the
battery envelope. Select the front bezel or rear shell and press Space to hide it
and inspect the interior. All geometry is embedded; no STEP/BREP files are needed
to reopen or share the document.

Electronics are imported from the PCB STEP using the same normalization and
2.6 mm direct-solder gaps as the previews and interference checks. The original,
unmodified PCB STEP is also embedded in the Electronics group's OriginalPCBSTEP
property, with its SHA-256 recorded in SourceSHA256.

This is a positioned inspection assembly using native App::Part containers and
Part::Feature solids, without kinematic joints or an editable CadQuery feature
history. It retains the same device coordinate frame as the generated part STEP
files. Regenerate it after PCB/enclosure edits. Export validation reopens the FCStd
and checks all component solids, volumes, positions, colors, and the embedded STEP.

## What follows the PCB

- Outline width, height, centre and nominal thickness come from Edge.Cuts/general.
- Display mounting holes and window position follow J2, including the offset
  of the active area within the module.
- USB opening height follows J1; the opening stays in the left wall.
- Slide-switch opening X follows SW6; the opening stays in the top wall.
- Button caps/guides and speaker pocket/grille follow their footprints.

The current layout uses a rectangular outline, J2 at 90 degrees and J1/SW6 at
0 degrees. Other outline types or module orientations require updating the
feature geometry; unsupported orientations fail explicitly.

## Parts

| File | Qty | What |
| --- | --- | --- |
| rear_shell | 1 | Tilted body, flared foot, battery saddles, board bosses, speaker tower, D-pad holes |
| front_bezel | 1 | Screen frame and locating tongue, joined by a connecting shoulder |
| button_cap | 5 | Rear button caps |
| display_spacer | 4 | Stand-offs between carrier and display module |

Outputs are STEP/STL files plus six preview PNGs in out/.
All four printable parts must each be one valid connected solid.

## Fit verification

The --check option writes out/fit_check.json and fails on overlaps greater than
0.01 mm3. It checks both enclosure halves against the electronics, battery,
spacers, caps and each other; the battery and spacers against electronics; and
caps at rest plus 0.6 mm inward motion against the case. Switch-actuator contact
while pressing is intentional and excluded from the pressed-cap collision check.
Boolean-operation failures are errors, not silently treated as a clear fit.

This is a rigid CAD fit check. It does not model wires, solder fillets, USB cable
insertion, print tolerances or switch force/travel, and it is not an electrical
or electronics-to-electronics clearance sign-off. Verify those on a fit print.

## Confirmed assembly

Both modules are soldered directly onto 2.54 mm pin headers, with header plastic
providing a **2.6 mm carrier-to-module gap** (DISPLAY_GAP and FEATHER_GAP).
No tall female sockets are included in this stack-up.

## Remaining assumptions and next iteration

- Battery envelope remains an 18.6 mm diameter x 69 mm cylindrical pack with a JST-PH lead.
- M3 x 12 screws through the display mounting holes into M3 heat-set inserts.
- Wall 2.4 mm and fit clearance 0.15 mm per side need printer-specific confirmation.
- Add Feather RESET/BOOT access, positive bezel retention, battery restraint,
  cable routing/strain relief and rubber-foot recesses.
- The slide-switch actuator still needs a cap (it sits about 1 mm below the thumb scoop).
