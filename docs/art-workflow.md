# Making animal sprites

The route from "I need a fox" to a fox walking around the Glade. A generator
(or a painting, or a photo you trace) supplies the drawing, a script turns it
into a palette-locked sprite, and Aseprite is where it gets finished and
animated. The script gives you a clean starting point, not finished art: expect
to fix a few pixels per frame and to animate by editing parts of a good pose.

## 1. Decide the size first

Every actor has a cell size in `pack.yaml` (`frame: [16, 16]` for the rabbit).
Settle it before generating anything, by eye against the painting: rocks in the
clearing are about 30 px wide, the buck cell is 80x80. Cleanup done at the wrong
size is wasted. The preview in step 3 puts a frame in the scene to check this;
for the real thing, `mbsnap` shows the 320x240 the display does. Actors don't
shrink with distance, so check both ends of the lane.

## 2. Generate a reference

One pose per image, or a sheet of poses in a regular grid. What helps the
converter most, in rough order:

- **A flat, saturated backdrop the animal doesn't use**, such as solid magenta
  `#ff00ff`. White and fake checkerboards work, but pale fur (tails, chests)
  that touches the edge of the animal can disappear with them.
- **Side view, facing right, full body, nothing cut off by the frame.** Facing
  left is fine too: `--flip` mirrors it.
- **No ground, no cast shadow, no text.** Anything that isn't backdrop becomes
  part of the sprite.
- **The painting's style and light**: soft painterly pixel art, light from the
  upper left and behind. Giving the generator `packs/glade/src/scene.png` as a
  style reference helps.
- **Large and sharp.** About 1024 px is plenty; the converter shrinks anything
  bigger.

## 3. Convert

```sh
python3 tools/sprite_from_ref.py packs/glade/palette.gpl fox.png \
        -o packs/glade/art/fox.png --cell 32x24 --groups animal,fx \
        --outline sel --preview build/fox-preview.png \
        --scene packs/glade/art/scene.png --at 300,200
```

- Several references become several frames, all at one shared scale, so a walk
  cycle keeps its size. `--split 4x1` cuts one generated sheet into four poses.
- `--groups animal,fx` keeps the sprite on the animal ramps, away from the
  scene's own browns and greens, so it reads against the painting. It takes
  single ramps too: when a coat drifts onto another animal's colours, name the
  ramps it should use (`--groups animal/deer,animal/cream,animal/ink`). If shades
  then run together, the ramp is missing a step: add it to `palette.gpl`.
- `--pockets remove` clears backdrop the animal encloses, such as the gap between
  two antlers. A saturated backdrop does this by itself; on a pale one, where a
  pocket looks like white fur, the script only reports what it found.
- `--outline sel` adds a 1 px outline in the darkest shade of each neighbouring
  colour's ramp. Try with and without; the painting itself has no outlines.
- `--method average` keeps more shading than the default `mode`, at the cost of
  softer edges. Worth trying on painterly references.
- The preview shows each reference, the frame enlarged, and the frame standing
  in the scene with its feet at `--at`. Judge size and contrast there.

`python3 tools/sprite_from_ref.py --help` describes every step and option.

## 4. Finish in Aseprite

Open the sheet and load the palette (Palette menu, Load Palette,
`packs/glade/palette.gpl`). Draw only with palette colours; `packc` rejects
anything else and names the pixel.

- **Fix the face first.** Eyes and noses are kept when they are big enough to
  matter, but check them: one clean dark eye pixel does more than anything else.
- **Thin parts** (legs, ears, tails) are the usual losses at small sizes.
- **Animate by editing a good pose.** At these sizes a walk cycle is mostly the
  legs swapping and the body bobbing a pixel; grazing is the head and neck
  dropping. The frame numbers each anim uses are in `pack.yaml`.
- Keep the feet on the bottom row of the cell unless you set an `anchor`.

## 5. See it move

Run the pack compiler in watch mode next to the simulator (see the README).
Every save in Aseprite rebuilds the pack, and `mbsim` reloads it:

```sh
python3 tools/packc/packc.py packs/glade -o build/glade.mbp --watch
./build/mbsim build/glade.mbp
```

Check it at dusk and night too (`[` and `]` step the simulator's hour, or use
`mbsnap --time`), because the lighting darkens every colour that isn't
`emissive`.
