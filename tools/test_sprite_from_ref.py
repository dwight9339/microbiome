#!/usr/bin/env python3
"""Regression test for tools/sprite_from_ref.py (run by ctest as sprite_from_ref).

It fakes the references a generator produces: the Glade deer placeholder blown
up 12x, blurred, grained and JPEG-compressed on a backdrop, so the right answer
is known. Checks:
  - at the deer's own scale the frames come back pixel for pixel, white tail
    (next to an off-white backdrop) included;
  - at an awkward scale the eyes survive (the feature pass);
  - --split cuts a sheet into poses in sheet order, and --flip mirrors each pose;
  - --outline stays inside the cell;
  - backdrop enclosed by the animal is removed on a saturated backdrop, and on a
    pale one only with --pockets remove;
  - --groups takes single ramps, and refuses names the palette doesn't have.
"""
import io
import os
import random
import sys
import tempfile

from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import palette  # noqa: E402
import sprite_from_ref  # noqa: E402

PALETTE = os.path.join(ROOT, "packs", "glade", "palette.gpl")
SHEET = os.path.join(ROOT, "packs", "glade", "art", "deer.png")
K = 12
INK = (0x23, 0x1e, 0x1a)  # the deer's eye and nose


def frame(i):
    s = Image.open(SHEET).convert("RGBA")
    return s.crop((i * 32, 0, i * 32 + 32, 32))


def fake_ref(fr, backdrop, rng, size=32 * K + 64):
    im = Image.new("RGB", (size, size), backdrop)
    big = fr.resize((32 * K, 32 * K), Image.NEAREST)
    im.paste(big, (32, 32), big)
    im = im.filter(ImageFilter.GaussianBlur(1.0))
    px = im.load()
    for y in range(size):
        for x in range(size):
            n = rng.randint(-5, 5)
            px[x, y] = tuple(max(0, min(255, c + n)) for c in px[x, y])
    buf = io.BytesIO()
    im.save(buf, "JPEG", quality=85)
    return Image.open(buf).convert("RGB")


def trimmed(im):
    return im.crop(im.getchannel("A").getbbox())


def close(p, q):
    """Same pixel, counting near-identical palette colours (one family) as equal."""
    if not p[3] or not q[3]:
        return not p[3] and not q[3]
    return palette.math.dist(palette.oklab(p), palette.oklab(q)) < sprite_from_ref.FAMILY_RADIUS


def differences(a, b):
    a, b = trimmed(a), trimmed(b)
    if a.size != b.size:
        return [f"size {a.size} vs {b.size}"]
    return [f"({x}, {y}) {palette.hexrgb(a.getpixel((x, y)))}/{a.getpixel((x, y))[3]} "
            f"vs {palette.hexrgb(b.getpixel((x, y)))}/{b.getpixel((x, y))[3]}"
            for y in range(a.height) for x in range(a.width) if not close(a.getpixel((x, y)), b.getpixel((x, y)))]


def has_eye(im):
    return any(c[3] and close(c, INK + (255,)) for _, c in im.getcolors(1 << 16))


def run(args, groups="animal,fx"):
    if sprite_from_ref.main([PALETTE] + args + ["--groups", groups]) != 0:
        raise SystemExit(f"sprite_from_ref failed: {args}")


def main():
    rng = random.Random(1)
    fails = []
    with tempfile.TemporaryDirectory() as tmp:
        refs = []
        for i in range(2):
            p = os.path.join(tmp, f"ref{i}.png")
            fake_ref(frame(i), (250, 250, 248), rng).save(p)
            refs.append(p)

        out = os.path.join(tmp, "native.png")
        run(refs + ["-o", out, "--cell", "32x32", "--fit", "26x30"])
        got = Image.open(out).convert("RGBA")
        for i in range(2):
            d = differences(got.crop((i * 32, 0, i * 32 + 32, 32)), frame(i))
            if d:
                fails.append(f"native scale: frame {i} differs from the original: {', '.join(d[:4])}")

        out = os.path.join(tmp, "small.png")
        run(refs + ["-o", out, "--cell", "24x24", "--fit", "20x23"])
        got = Image.open(out).convert("RGBA")
        for i in range(2):
            if not has_eye(got.crop((i * 24, 0, i * 24 + 24, 24))):
                fails.append(f"awkward scale: frame {i} lost the eye")

        sheet = Image.new("RGB", (2 * (32 * K + 64), 32 * K + 64))
        for i in range(2):
            sheet.paste(fake_ref(frame(i), (255, 0, 255), rng).transpose(Image.FLIP_LEFT_RIGHT),
                        (i * (32 * K + 64), 0))
        sp = os.path.join(tmp, "sheet.png")
        sheet.save(sp)
        out = os.path.join(tmp, "split.png")
        run([sp, "-o", out, "--cell", "32x32", "--fit", "26x30", "--split", "2x1", "--flip"])
        got = Image.open(out).convert("RGBA")
        for i in range(2):
            d = differences(got.crop((i * 32, 0, i * 32 + 32, 32)), frame(i))
            if d:
                fails.append(f"split/flip: frame {i} differs from the original: {', '.join(d[:4])}")

        out = os.path.join(tmp, "outline.png")
        run(refs[:1] + ["-o", out, "--cell", "32x32", "--outline", "sel"])
        got = Image.open(out).convert("RGBA")
        box = got.getchannel("A").getbbox()
        if box is None or box[2] - box[0] > 32 or box[3] - box[1] > 32:
            fails.append(f"outline: content box {box} is outside the 32x32 cell")
        if not has_eye(got):
            fails.append("outline: the eye is gone")

        # a ring: the gap in the middle is backdrop the border doesn't reach
        for name, backdrop, args, cleared in (("magenta", (255, 0, 255), [], True),
                                              ("magenta-keep", (255, 0, 255), ["--pockets", "keep"], False),
                                              ("white", (250, 250, 248), [], False),
                                              ("white-remove", (250, 250, 248), ["--pockets", "remove"], True)):
            ring = Image.new("RGBA", (32, 32))
            ring.paste((0x9a, 0x6a, 0x3e, 255), (8, 8, 24, 24))
            ring.paste((0, 0, 0, 0), (13, 13, 19, 19))
            rp, out = os.path.join(tmp, f"ring-{name}.png"), os.path.join(tmp, f"ring-{name}-out.png")
            fake_ref(ring, backdrop, rng).save(rp)
            run([rp, "-o", out, "--cell", "16x16"] + args)
            if (Image.open(out).convert("RGBA").getpixel((8, 8))[3] == 0) != cleared:
                fails.append(f"pockets: {name}: the gap should be {'cleared' if cleared else 'kept'}")

        out = os.path.join(tmp, "ramps.png")
        run(refs[:1] + ["-o", out, "--cell", "32x32"], "animal/deer,animal/ink")
        allowed = {e.rgb for e in palette.read_gpl(PALETTE) if e.ramp in ("animal/deer", "animal/ink")}
        off = palette.off_palette(Image.open(out), allowed)
        if off:
            fails.append(f"groups: colours outside the ramps asked for: {', '.join(map(palette.hexrgb, off))}")
        try:
            palette.select(palette.read_gpl(PALETTE), ["animal/deer", "animal/dear"])
            fails.append("groups: an unknown ramp was accepted")
        except ValueError:
            pass

    for f in fails:
        print("FAIL:", f)
    print("sprite_from_ref:", "ok" if not fails else f"{len(fails)} failure(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
