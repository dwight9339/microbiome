#!/usr/bin/env python3
"""packc - compile a microbiome content pack (pack.yaml + PNGs) into a .mbp blob.

    python3 tools/packc/packc.py packs/forest -o build/forest.mbp
    python3 tools/packc/packc.py packs/forest -o build/forest.mbp --watch

The binary layout mirrors core/include/mb/pack_format.h exactly; keep the two
in sync and bump VERSION on any change. See docs/pack-format.md for the YAML
schema.
"""
from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import time
from dataclasses import dataclass, field

try:
    import yaml
    from PIL import Image
except ImportError as exc:  # pragma: no cover
    sys.exit(f"packc needs PyYAML and Pillow: pip install pyyaml pillow ({exc})")

MAGIC = 0x4B50424D
VERSION = 1

SECTIONS = [
    "world", "palettes", "images", "layers", "anims", "frames", "actors", "states",
    "next", "reactions", "conds", "clauses", "vars", "spawns", "lanes", "places",
    "particles", "blob", "strings",
]

# struct formats (little-endian, no implicit padding) - must match pack_format.h
FMT = {
    "header":    "<IHHII",
    "section":   "<II",
    "world":     "<HHHBBHHhhHHIBBBxI",
    "palette":   "<HH256I",
    "images":    "<HHI",
    "layers":    "<HHhhhhI",
    "anims":     "<HHHH",
    "frames":    "<H",
    "actors":    "<IIHHHhhHHBBHBBHHHH",
    "states":    "<IHBBHHHBBI",
    "next":      "<BBH",
    "reactions": "<IHBB",
    "conds":     "<HH",
    "clauses":   "<BBHii",
    "vars":      "<IIIIBBH",
    "spawns":    "<HHHBBHHBBH",
    "lanes":     "<hhI",
    "places":    "<hhIIHH",
    "particles": "<HHBBBBhhhhhhB3x",
}
EXPECTED_SIZES = {
    "world": 32, "palette": 1028, "images": 8, "layers": 16, "anims": 8, "frames": 2,
    "actors": 36, "states": 20, "next": 4, "reactions": 8, "conds": 4, "clauses": 12,
    "vars": 20, "spawns": 16, "lanes": 8, "places": 16, "particles": 24,
}
for _k, _v in EXPECTED_SIZES.items():
    assert struct.calcsize(FMT[_k]) == _v, (_k, struct.calcsize(FMT[_k]))

LAYER_REPEAT_X, LAYER_FRONT = 0x01, 0x02
PARALLAX_AUTO = -1
ANIM_LOOP = 0x01
ACTOR_FACES_LEFT = 0x01
NO_LANE = NO_STATE = 0xFF
MOVES = {"idle": 0, "wander": 1, "seek": 2, "exit": 3, "flee": 4}
STATE_UNTIL_ARRIVE, STATE_DESPAWN_END = 0x01, 0x02
PART_TOP = 0xFF
OPS = {"in": 0, "not": 1, "not_in": 1, "eq": 2, "ne": 3, "lt": 4, "le": 5, "gt": 6, "ge": 7,
       "between": 8}
VAR_CONST, VAR_MINUTE, VAR_HOUR, VAR_MONTH, VAR_DAY, VAR_STEP, VAR_MARKOV = range(7)
ENTER = {"edge": 0, "place": 1, "anywhere": 2}
PART_BLINK, PART_WANDER = 0x01, 0x02


class PackError(Exception):
    pass


def fail(where: str, msg: str):
    raise PackError(f"{where}: {msg}")


# ---------------------------------------------------------------- parsing helpers

def parse_duration(v, where, default=None) -> float:
    """'500ms', '4s', '2m', '1.5h', or a bare number of seconds -> seconds."""
    if v is None:
        if default is None:
            fail(where, "duration required")
        return default
    if isinstance(v, (int, float)):
        return float(v)
    m = re.fullmatch(r"\s*([0-9.]+)\s*(ms|s|m|h)?\s*", str(v))
    if not m:
        fail(where, f"bad duration {v!r} (use e.g. 500ms, 4s, 2m, 1h)")
    n, unit = float(m.group(1)), m.group(2) or "s"
    return n * {"ms": 0.001, "s": 1, "m": 60, "h": 3600}[unit]


def parse_range(v, where, conv=float):
    if isinstance(v, (list, tuple)):
        if len(v) != 2:
            fail(where, f"expected [min, max], got {v!r}")
        return conv(v[0]), conv(v[1])
    return conv(v), conv(v)


def parse_clock(v, where) -> int:
    """'HH:MM' -> minute of day. YAML may have already turned 07:30 into an int (sexagesimal)."""
    if isinstance(v, int):
        return v % 1440
    m = re.fullmatch(r"\s*(\d{1,2}):(\d{2})\s*", str(v))
    if not m:
        fail(where, f"bad time {v!r} (use 'HH:MM', quoted)")
    h, mi = int(m.group(1)), int(m.group(2))
    if h > 23 or mi > 59:
        fail(where, f"bad time {v!r}")
    return h * 60 + mi


def parse_color(v, where):
    if isinstance(v, (list, tuple)) and len(v) == 3:
        return tuple(int(c) for c in v)
    m = re.fullmatch(r"#?([0-9a-fA-F]{6})", str(v).strip())
    if not m:
        fail(where, f"bad colour {v!r} (use '#rrggbb')")
    h = m.group(1)
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))


def as_list(v):
    if v is None:
        return []
    return list(v) if isinstance(v, (list, tuple)) else [v]


def parse_frames(v, where):
    """[0,1,2] or '0-3' or '0-3,5' -> list of ints."""
    if isinstance(v, int):
        return [v]
    if isinstance(v, (list, tuple)):
        out = []
        for x in v:
            out += parse_frames(x, where)
        return out
    out = []
    for part in str(v).split(","):
        part = part.strip()
        m = re.fullmatch(r"(\d+)\s*-\s*(\d+)", part)
        if m:
            a, b = int(m.group(1)), int(m.group(2))
            out += list(range(a, b + 1)) if a <= b else list(range(a, b - 1, -1))
        elif part.isdigit():
            out.append(int(part))
        else:
            fail(where, f"bad frame list {v!r}")
    return out


# ---------------------------------------------------------------- builder

@dataclass
class Var:
    name: str
    kind: int
    index: int
    values: list[str] = field(default_factory=list)  # enum labels
    param: int = 0
    count: int = 0
    data: bytes = b""
    sensor: str = ""

    def value(self, v, where):
        if self.values:
            if isinstance(v, str) and v in self.values:
                return self.values.index(v)
            fail(where, f"{self.name} has no value {v!r} (choices: {', '.join(self.values)})")
        if self.kind == VAR_MINUTE:
            return parse_clock(v, where)
        if isinstance(v, bool) or not isinstance(v, int):
            fail(where, f"{self.name} expects a number, got {v!r}")
        return v


class Builder:
    def __init__(self, root: str):
        self.root = root
        self.strings = bytearray(b"\0")
        self._str_index = {"": 0}
        self.blob = bytearray()
        self.palette: dict[tuple, int] = {}  # rgb -> index (1..255)
        self.images: list[tuple] = []
        self._image_index: dict[str, int] = {}
        self.rows = {k: [] for k in SECTIONS if k not in ("blob", "strings")}
        self.vars: dict[str, Var] = {}
        self.cond_index = {(): 0}
        self.rows["conds"].append((0, 0))
        self.tags: dict[str, int] = {}
        self.affordances: dict[str, int] = {}
        self.inputs: set[str] = set()
        self.warnings: list[str] = []

    # ---- primitive tables
    def string(self, s: str) -> int:
        if s not in self._str_index:
            self._str_index[s] = len(self.strings)
            self.strings += s.encode("utf-8") + b"\0"
        return self._str_index[s]

    def blob_add(self, data: bytes) -> int:
        while len(self.blob) % 4:
            self.blob.append(0)
        off = len(self.blob)
        self.blob += data
        return off

    def color_index(self, rgb, where) -> int:
        rgb = tuple(int(c) for c in rgb[:3])
        if rgb not in self.palette:
            if len(self.palette) >= 255:
                fail(where, "more than 255 distinct colours across the pack; reduce the palette")
            self.palette[rgb] = len(self.palette) + 1
        return self.palette[rgb]

    def bit(self, table: dict, name: str, kind: str, where: str) -> int:
        if name not in table:
            if len(table) >= 32:
                fail(where, f"more than 32 {kind}s")
            table[name] = len(table)
        return table[name]

    def mask(self, table, names, kind, where) -> int:
        m = 0
        for n in as_list(names):
            m |= 1 << self.bit(table, str(n), kind, where)
        return m

    def image(self, rel: str, where: str) -> int:
        if rel in self._image_index:
            return self._image_index[rel]
        path = os.path.join(self.root, rel)
        self.inputs.add(path)
        try:
            im = Image.open(path).convert("RGBA")
        except (OSError, ValueError) as exc:
            fail(where, f"cannot load image {rel!r}: {exc}")
        w, h = im.size
        if w > 65535 or h > 65535:
            fail(where, f"{rel} is too large")
        px = im.load()
        data = bytearray(w * h)
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                if a >= 128:
                    data[y * w + x] = self.color_index((r, g, b), f"{where} ({rel})")
        off = self.blob_add(bytes(data))
        idx = len(self.rows["images"])
        self.rows["images"].append((w, h, off))
        self.images.append((rel, w, h))
        self._image_index[rel] = idx
        return idx

    # ---- conditions
    def cond(self, spec, where) -> int:
        if spec is None or spec == {}:
            return 0
        if not isinstance(spec, dict):
            fail(where, f"condition must be a mapping of var: test, got {spec!r}")
        clauses = []
        for vname, test in spec.items():
            w = f"{where}.{vname}"
            var = self.vars.get(vname)
            if var is None:
                fail(w, f"unknown variable {vname!r} (known: {', '.join(self.vars)})")
            if isinstance(test, dict):
                for op, arg in test.items():
                    if op not in OPS:
                        fail(w, f"unknown operator {op!r} (use {', '.join(OPS)})")
                    code = OPS[op]
                    if code in (0, 1):
                        clauses.append((var.index, code, self._in_mask(var, arg, w), 0))
                    elif code == 8:
                        lo, hi = parse_range(arg, w, conv=lambda x: x)
                        clauses.append((var.index, code, var.value(lo, w), var.value(hi, w)))
                    else:
                        clauses.append((var.index, code, var.value(arg, w), 0))
            elif isinstance(test, (list, tuple)):
                clauses.append((var.index, OPS["in"], self._in_mask(var, test, w), 0))
            else:
                clauses.append((var.index, OPS["eq"], var.value(test, w), 0))
        key = tuple(clauses)
        if key not in self.cond_index:
            first = len(self.rows["clauses"])
            for c in clauses:
                self.rows["clauses"].append((c[0], c[1], 0, c[2], c[3]))
            self.cond_index[key] = len(self.rows["conds"])
            self.rows["conds"].append((first, len(clauses)))
        return self.cond_index[key]

    def _in_mask(self, var, values, where):
        m = 0
        for v in as_list(values):
            n = var.value(v, where)
            if not 0 <= n < 32:
                fail(where, f"'in' tests only work on values 0..31 ({var.name}={n}); use between/lt/gt")
            m |= 1 << n
        return m - (1 << 32) if m >= 1 << 31 else m

    # ---- sections
    def build_vars(self, spec: dict):
        for name, kind in (("minute", VAR_MINUTE), ("hour", VAR_HOUR), ("month", VAR_MONTH),
                           ("day", VAR_DAY)):
            self.vars[name] = Var(name, kind, len(self.vars))
        for name, vs in (spec or {}).items():
            where = f"vars.{name}"
            if name in self.vars:
                fail(where, "redefines a built-in variable")
            if not isinstance(vs, dict):
                vs = {"const": vs}
            var = Var(name, VAR_CONST, len(self.vars), sensor=str(vs.get("sensor", "")))
            if "step" in vs:
                src = self.vars.get(vs["step"])
                if src is None:
                    fail(where, f"step source {vs['step']!r} must be defined before {name}")
                table = vs.get("table")
                if not isinstance(table, dict) or not table:
                    fail(where, "step needs a non-empty 'table' of key: value")
                pairs = []
                for k, v in table.items():
                    key = src.value(k, f"{where}.table")
                    if isinstance(v, str):
                        if v not in var.values:
                            var.values.append(v)
                        val = var.values.index(v)
                    else:
                        val = int(v)
                    pairs.append((key, val))
                pairs.sort()
                var.kind, var.param, var.count = VAR_STEP, src.index, len(pairs)
                var.data = b"".join(struct.pack("<hh", k, v) for k, v in pairs)
            elif "markov" in vs:
                mk = vs["markov"]
                states = mk.get("states") or {}
                if not states:
                    fail(where, "markov needs 'states'")
                var.values = list(states.keys())
                n = len(var.values)
                mat = bytearray(n * n)
                for i, (sname, row) in enumerate(states.items()):
                    for tname, wgt in (row or {}).items():
                        if tname not in var.values:
                            fail(f"{where}.states.{sname}", f"unknown state {tname!r}")
                        if not 0 <= int(wgt) <= 255:
                            fail(f"{where}.states.{sname}", "weights must be 0..255")
                        mat[i * n + var.values.index(tname)] = int(wgt)
                    if not any(mat[i * n:(i + 1) * n]):
                        mat[i * n + i] = 1  # absorbing unless told otherwise
                step = parse_duration(mk.get("step", "30m"), where)
                var.kind, var.count, var.param = VAR_MARKOV, n, max(1, round(step / 60))
                var.data = bytes(mat)
            elif "const" in vs:
                var.param = int(vs["const"]) & 0xFFFF
            else:
                fail(where, "variable needs one of: step, markov, const")
            self.vars[name] = var
        if len(self.vars) > 32:
            fail("vars", "at most 32 variables (including the 4 built-ins)")
        for var in self.vars.values():
            off = self.blob_add(var.data) if var.data else 0
            self.rows["vars"].append((
                self.string(var.name), self.string(var.sensor),
                self.string("|".join(var.values)), off, var.kind, var.count, var.param))

    def build_layers(self, layers):
        self.layer_names: list[str] = []
        for i, ls in enumerate(layers or []):
            where = f"layers[{i}]"
            if "image" not in ls:
                fail(where, "needs 'image'")
            img = self.image(ls["image"], where)
            px = ls.get("parallax", "auto")
            if isinstance(px, (list, tuple)):
                pxx, pxy = px
            else:
                pxx = pxy = px
            def q8(v):
                if v == "auto":
                    return PARALLAX_AUTO
                q = round(float(v) * 256)
                if q == PARALLAX_AUTO or not -32768 <= q <= 32767:
                    fail(where, f"bad parallax {v!r}")
                return q
            at = ls.get("at", [0, 0])
            flags = (LAYER_REPEAT_X if ls.get("repeat_x") else 0) | (LAYER_FRONT if ls.get("front") else 0)
            lname = str(ls.get("name", f"layer{i}"))
            self.layer_names.append(lname)
            self.rows["layers"].append((img, flags, int(at[0]), int(at[1]), q8(pxx), q8(pxy),
                                        self.string(lname)))

    def build_actor(self, name, a):
        where = f"actors.{name}"
        for k in ("sheet", "frame", "anims", "states"):
            if k not in a:
                fail(where, f"needs '{k}'")
        img = self.image(a["sheet"], where)
        fw, fh = (int(v) for v in a["frame"])
        iw, ih = self.rows["images"][img][0], self.rows["images"][img][1]
        if iw % fw or ih % fh:
            self.warnings.append(f"{where}: sheet {iw}x{ih} is not a multiple of frame {fw}x{fh}")
        cells = (iw // fw) * (ih // fh)
        anchor = a.get("anchor", [fw // 2, fh - 1])

        anim_ids = {}
        for an_name, an in a["anims"].items():
            aw = f"{where}.anims.{an_name}"
            if not isinstance(an, dict):
                an = {"frames": an}
            frames = parse_frames(an.get("frames"), aw)
            if not frames:
                fail(aw, "needs at least one frame")
            for f in frames:
                if not 0 <= f < cells:
                    fail(aw, f"frame {f} out of range (sheet has {cells} cells)")
            if "fps" in an:
                ms = round(1000 / float(an["fps"]))
            else:
                ms = round(parse_duration(an.get("frame", "125ms"), aw) * 1000)
            first = len(self.rows["frames"])
            self.rows["frames"] += [(f,) for f in frames]
            anim_ids[an_name] = len(self.rows["anims"])
            self.rows["anims"].append((first, len(frames), max(1, min(ms, 65535)),
                                       ANIM_LOOP if an.get("loop", True) else 0))

        states = a["states"]
        if not isinstance(states, dict) or not states:
            fail(where, "states must be a non-empty mapping (first state is the initial one)")
        names = list(states.keys())
        if len(names) > 254:
            fail(where, "too many states")
        state_first = len(self.rows["states"])
        pending = []
        for sname, st in states.items():
            sw = f"{where}.states.{sname}"
            st = st or {}
            an = st.get("anim", sname if sname in anim_ids else None)
            if an not in anim_ids:
                fail(sw, f"unknown anim {an!r} (have: {', '.join(anim_ids)})")
            move_spec = str(st.get("move", "idle")).split()
            move = move_spec[0]
            if move not in MOVES:
                fail(sw, f"unknown move {move!r} (use {', '.join(MOVES)})")
            param = 0
            if move == "seek":
                if len(move_spec) < 2:
                    fail(sw, "seek needs an affordance, e.g. 'seek drink'")
                param = self.mask(self.affordances, move_spec[1:], "affordance", sw)
            if "duration" in st:
                lo, hi = parse_range(st["duration"], sw, conv=lambda v: parse_duration(v, sw))
            else:
                lo = hi = 0.0
                if move == "idle":
                    fail(sw, "idle states need a 'duration'")
            flags = STATE_UNTIL_ARRIVE if st.get("until") == "arrive" else 0
            then = st.get("then")
            if then == "despawn":
                flags |= STATE_DESPAWN_END
            elif then is not None:
                fail(sw, f"unknown 'then: {then}' (only 'despawn' is supported)")
            speed = float(st.get("speed", 1.0))
            if not 0 < speed < 16:
                fail(sw, "speed multiplier must be between 0 and 16")
            if move in ("exit", "flee") and st.get("next"):
                self.warnings.append(f"{sw}: 'next' is ignored on {move} states (actor despawns)")
            pending.append((sname, st, sw))
            self.rows["states"].append([
                self.string(sname), anim_ids[an], MOVES[move], flags,
                round(lo * 10), round(hi * 10), 0, 0, max(1, round(speed * 16)), param])

        def state_ref(n, w):
            if n not in names:
                fail(w, f"unknown state {n!r} (have: {', '.join(names)})")
            return names.index(n)

        for i, (sname, st, sw) in enumerate(pending):
            nxt = st.get("next") or []
            if isinstance(nxt, dict):
                nxt = [{"to": k, "weight": v} for k, v in nxt.items()]
            elif isinstance(nxt, str):
                nxt = [{"to": nxt}]
            first = len(self.rows["next"])
            for j, n in enumerate(nxt):
                nw = f"{sw}.next[{j}]"
                if isinstance(n, str):
                    n = {"to": n}
                wgt = int(n.get("weight", 1))
                if not 0 < wgt <= 255:
                    fail(nw, "weight must be 1..255")
                self.rows["next"].append((state_ref(n.get("to"), nw), wgt, self.cond(n.get("if"), nw)))
            row = self.rows["states"][state_first + i]
            row[6], row[7] = first, len(nxt)

        react_first = len(self.rows["reactions"])
        for j, r in enumerate(a.get("react") or []):
            rw = f"{where}.react[{j}]"
            tags = self.mask(self.tags, r.get("to"), "tag", rw)
            self.rows["reactions"].append((tags, int(r.get("radius", 48)), state_ref(r.get("goto"), rw), 0))

        lane = NO_LANE
        if "lane" in a:
            if a["lane"] not in self.lane_names:
                fail(where, f"unknown lane {a['lane']!r}")
            lane = self.lane_names.index(a["lane"])
        leave = state_ref(a["leave"], where) if "leave" in a else NO_STATE
        despawn = self.cond(a.get("despawn_when"), f"{where}.despawn_when")
        lifetime = round(parse_duration(a.get("lifetime", 0), where))
        if (despawn or lifetime) and leave == NO_STATE:
            self.warnings.append(f"{where}: no 'leave' state, so it will vanish in place when it despawns")
        speed = float(a.get("speed", 16))
        flags = ACTOR_FACES_LEFT if a.get("faces") == "left" else 0
        tags = self.mask(self.tags, a.get("tags"), "tag", where)
        self.rows["actors"].append((
            self.string(name), tags, img, fw, fh, int(anchor[0]), int(anchor[1]),
            round(speed * 16), state_first, len(names), lane, react_first,
            len(self.rows["reactions"]) - react_first, leave, despawn, min(lifetime, 65535), flags, 0))
        self.actor_names.append(name)

    def build(self, spec: dict):
        if not isinstance(spec, dict):
            fail("pack.yaml", "top level must be a mapping")
        world = spec.get("world") or {}
        self.build_vars(spec.get("vars"))
        self.build_layers(spec.get("layers"))

        lanes = spec.get("lanes") or {}
        self.lane_names = list(lanes.keys())
        for ln, rng in lanes.items():
            lo, hi = parse_range(rng, f"lanes.{ln}", conv=int)
            self.rows["lanes"].append((lo, hi, self.string(ln)))

        for pn, p in (spec.get("places") or {}).items():
            pw = f"places.{pn}"
            at = p.get("at")
            if not at or len(at) != 2:
                fail(pw, "needs 'at: [x, y]'")
            self.rows["places"].append((int(at[0]), int(at[1]),
                                        self.mask(self.affordances, p.get("offers"), "affordance", pw),
                                        self.string(pn), int(p.get("spread", 0)), 0))

        self.actor_names: list[str] = []
        for an, a in (spec.get("actors") or {}).items():
            self.build_actor(an, a or {})

        for i, s in enumerate(spec.get("spawns") or []):
            sw = f"spawns[{i}]"
            if s.get("actor") not in self.actor_names:
                fail(sw, f"unknown actor {s.get('actor')!r}")
            enter_spec = str(s.get("enter", "edge")).split()
            if enter_spec[0] not in ENTER:
                fail(sw, f"enter must be one of {', '.join(ENTER)}")
            eparam = 0
            if enter_spec[0] == "place":
                if len(enter_spec) != 2:
                    fail(sw, "enter: place needs an affordance, e.g. 'place burrow'")
                eparam = self.bit(self.affordances, enter_spec[1], "affordance", sw)
            gmin, gmax = parse_range(s.get("count", 1), sw, conv=int)
            mx = int(s.get("max", max(1, gmax)))
            if gmin < 1 or gmax < gmin or mx < 1 or mx > 255:
                fail(sw, "bad count/max")
            self.rows["spawns"].append((
                self.actor_names.index(s["actor"]), self.cond(s.get("when"), f"{sw}.when"),
                int(s.get("weight", 1)), mx, ENTER[enter_spec[0]],
                round(parse_duration(s.get("cooldown", 0), sw)), eparam, gmin, gmax, 0))

        for pn, p in (spec.get("particles") or {}).items():
            pw = f"particles.{pn}"
            vx = parse_range(p.get("vx", 0), pw)
            vy = parse_range(p.get("vy", 0), pw)
            band = parse_range(p.get("band", [0, 0]), pw, conv=int)
            flags = (PART_BLINK if p.get("blink") else 0) | (PART_WANDER if p.get("wander") else 0)
            after = PART_TOP
            if "above" in p:
                if p["above"] not in self.layer_names:
                    fail(pw, f"'above' must name a layer (have: {', '.join(self.layer_names)})")
                after = self.layer_names.index(p["above"])
            self.rows["particles"].append((
                self.cond(p.get("when"), f"{pw}.when"), int(p.get("count", 32)),
                self.color_index(parse_color(p.get("color", "#ffffff"), pw), pw), flags,
                int(p.get("size", 1)), int(p.get("wobble", 0)),
                round(vx[0] * 16), round(vx[1] * 16), round(vy[0] * 16), round(vy[1] * 16),
                band[0], band[1], after))

        clear = self.color_index(parse_color(world.get("clear_color", "#000000"), "world"), "world.clear_color")
        self.build_palettes(spec.get("lighting") or {})

        size = world.get("size")
        if not size or len(size) != 2:
            fail("world", "needs 'size: [width, height]'")
        cam = world.get("camera") or {}
        start = cam.get("start", [size[0] // 2, size[1] // 2])
        spawning = world.get("spawning") or {}
        clock = spec.get("clock") or {}
        date = str(clock.get("date", "01-01"))
        m = re.fullmatch(r"(\d{1,2})-(\d{1,2})", date)
        if not m:
            fail("clock.date", "use 'MM-DD'")
        self.rows["world"].append((
            int(size[0]), int(size[1]), 0,
            int(spawning.get("max_actors", 12)), int(spawning.get("chance", 50)),
            round(parse_duration(spawning.get("interval", "5s"), "world.spawning") * 10),
            int(cam.get("pan_speed", 120)), int(start[0]), int(start[1]),
            round(parse_duration(cam.get("idle_drift", "30s"), "world.camera")),
            parse_clock(clock.get("start", "08:00"), "clock.start"),
            round(parse_duration(clock.get("day_length", "24h"), "clock.day_length")),
            clear, int(m.group(1)), int(m.group(2)), self.string(str(spec.get("name", "")))))

    def build_palettes(self, lighting: dict):
        base = [(0, 0, 0)] * 256
        for rgb, idx in self.palette.items():
            base[idx] = rgb
        emissive = {parse_color(c, "lighting.emissive") for c in lighting.get("emissive", [])}
        kfs = lighting.get("keyframes") or [{"at": "00:00"}]
        out = []
        for i, kf in enumerate(kfs):
            kw = f"lighting.keyframes[{i}]"
            minute = parse_clock(kf.get("at", "00:00"), kw)
            bright = float(kf.get("brightness", 1.0))
            sat = float(kf.get("saturation", 1.0))
            tint = parse_color(kf.get("tint", "#ffffff"), kw)
            ts = float(kf.get("tint_strength", 0.0 if "tint" not in kf else 0.5))
            cols = []
            for rgb in base:
                if rgb in emissive:
                    cols.append(rgb)
                    continue
                r, g, b = rgb
                lum = 0.299 * r + 0.587 * g + 0.114 * b
                c = [lum + (ch - lum) * sat for ch in (r, g, b)]
                c = [ch * bright for ch in c]
                c = [ch * (1 - ts + ts * t / 255) for ch, t in zip(c, tint)]
                cols.append(tuple(max(0, min(255, round(ch))) for ch in c))
            out.append((minute, cols))
        out.sort(key=lambda t: t[0])
        if len({m for m, _ in out}) != len(out):
            fail("lighting.keyframes", "two keyframes share the same time")
        for minute, cols in out:
            words = [(r << 16) | (g << 8) | b for r, g, b in cols]
            self.rows["palettes"].append((minute, 0, *words))

    # ---- serialisation
    def serialize(self) -> bytes:
        header_size = struct.calcsize(FMT["header"]) + struct.calcsize(FMT["section"]) * len(SECTIONS)
        body = bytearray()
        table = []
        for name in SECTIONS:
            while (header_size + len(body)) % 4:
                body.append(0)
            off = header_size + len(body)
            if name == "blob":
                data, count = bytes(self.blob), len(self.blob)
            elif name == "strings":
                data, count = bytes(self.strings), len(self.strings)
            else:
                fmt = FMT["palette" if name == "palettes" else name]
                rows = self.rows[name]
                try:
                    data = b"".join(struct.pack(fmt, *r) for r in rows)
                except struct.error as exc:
                    raise PackError(f"internal: packing {name}: {exc} (a value is out of range)")
                count = len(rows)
            body += data
            table.append((off, count))
        total = header_size + len(body)
        hdr = struct.pack(FMT["header"], MAGIC, VERSION, len(SECTIONS), total, 0)
        hdr += b"".join(struct.pack(FMT["section"], o, c) for o, c in table)
        return hdr + bytes(body)


def compile_pack(pack_dir: str) -> tuple[bytes, Builder]:
    yml = os.path.join(pack_dir, "pack.yaml")
    try:
        with open(yml, encoding="utf-8") as f:
            spec = yaml.safe_load(f)
    except (OSError, yaml.YAMLError) as exc:
        raise PackError(f"pack.yaml: {exc}")
    b = Builder(pack_dir)
    b.inputs.add(yml)
    b.build(spec)
    return b.serialize(), b


def summary(data: bytes, b: Builder) -> str:
    n = lambda k: len(b.rows[k])
    return (f"{len(data) / 1024:.1f} KiB | {len(b.palette)} colours, {n('palettes')} lighting keyframes | "
            f"{n('images')} images ({len(b.blob) / 1024:.1f} KiB pixels) | {n('layers')} layers | "
            f"{n('actors')} actors, {n('states')} states | {n('spawns')} spawn rules | "
            f"{n('vars')} vars | {n('particles')} particle systems")


def build_once(pack_dir: str, out: str, quiet=False) -> Builder | None:
    try:
        data, b = compile_pack(pack_dir)
    except PackError as exc:
        print(f"packc: error: {exc}", file=sys.stderr)
        return None
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    tmp = out + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, out)  # atomic, so a watching simulator never reads half a file
    for w in b.warnings:
        print(f"packc: warning: {w}", file=sys.stderr)
    if not quiet:
        print(f"packc: wrote {out}: {summary(data, b)}")
    return b


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pack_dir")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--watch", action="store_true", help="rebuild whenever an input changes")
    args = ap.parse_args(argv)

    b = build_once(args.pack_dir, args.out)
    if not args.watch:
        return 0 if b else 1

    def snapshot():
        files = set(b.inputs) if b else set()
        for dp, _, fns in os.walk(args.pack_dir):
            files |= {os.path.join(dp, f) for f in fns}
        return {f: os.path.getmtime(f) for f in files if os.path.exists(f)}

    seen = snapshot()
    print("packc: watching for changes (ctrl-c to stop)")
    try:
        while True:
            time.sleep(0.4)
            now = snapshot()
            if now != seen:
                seen = now
                b = build_once(args.pack_dir, args.out) or b
                seen = snapshot()
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    sys.exit(main())
