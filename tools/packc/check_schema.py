#!/usr/bin/env python3
"""check_schema - validate pack.yaml files against tools/packc/pack.schema.json.

    python3 tools/packc/check_schema.py packs/forest [more pack dirs or yaml files]
    python3 tools/packc/check_schema.py --self-test packs/forest

packc is the authority on what a pack means. The schema exists for editors
(autocomplete, typo detection) and for the studio's forms. This script keeps
the two honest: every real pack must pass, and --self-test checks that a set
of known mistakes is rejected. Needs: pip install pyyaml jsonschema
"""
from __future__ import annotations

import argparse
import copy
import json
import os
import sys

try:
    import yaml
    from jsonschema import Draft202012Validator
except ImportError as exc:  # pragma: no cover
    sys.exit(f"check_schema needs PyYAML and jsonschema: pip install pyyaml jsonschema ({exc})")

SCHEMA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "pack.schema.json")


def load_validator() -> Draft202012Validator:
    with open(SCHEMA, encoding="utf-8") as f:
        schema = json.load(f)
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def load_pack(path: str):
    if os.path.isdir(path):
        path = os.path.join(path, "pack.yaml")
    with open(path, encoding="utf-8") as f:
        return path, yaml.safe_load(f)


def errors(validator, spec) -> list[str]:
    out = []
    for err in sorted(validator.iter_errors(spec), key=lambda e: list(e.absolute_path)):
        where = ".".join(str(p) for p in err.absolute_path) or "(top level)"
        # oneOf/anyOf failures read better as the branch that nearly matched:
        # a pattern or unexpected key rather than "is not of type 'number'".
        best = min(err.context, key=lambda e: (e.validator == "type", len(e.message)), default=None)
        msg = (best or err).message
        out.append(f"{where}: {msg if len(msg) <= 160 else msg[:157] + '...'}")
    return out


# Each mutation of a valid pack must be rejected: (description, function(spec)).
def _first(d):
    return next(iter(d))


MUTATIONS = [
    ("unknown top-level key", lambda s: s.__setitem__("actor", {})),
    ("typo in a state key", lambda s: s["actors"][_first(s["actors"])]["states"][
        _first(s["actors"][_first(s["actors"])]["states"])].__setitem__("duraton", "4s")),
    ("bad duration", lambda s: s["world"]["spawning"].__setitem__("interval", "4 sec")),
    ("seek without an affordance", lambda s: s["actors"][_first(s["actors"])]["states"].__setitem__(
        "x", {"move": "seek"})),
    ("idle state without a duration", lambda s: s["actors"][_first(s["actors"])]["states"].__setitem__(
        "x", {"anim": "walk"})),
    ("unknown enter mode", lambda s: s["spawns"][0].__setitem__("enter", "door")),
    ("bad colour", lambda s: s["world"].__setitem__("clear_color", "#ggg000")),
    ("spawn max of 0", lambda s: s["spawns"][0].__setitem__("max", 0)),
    ("unknown condition operator", lambda s: s["spawns"][0].__setitem__("when", {"phase": {"is": "day"}})),
    ("redefined built-in variable", lambda s: s["vars"].__setitem__("hour", {"const": 3})),
    ("var with no kind", lambda s: s["vars"].__setitem__("x", {"sensor": "temp"})),
    ("bad time of day", lambda s: s["clock"].__setitem__("start", "25:00")),
    ("bad frame list", lambda s: s["actors"][_first(s["actors"])]["anims"].__setitem__("x", {"frames": "0..3"})),
    ("layer without image", lambda s: s["layers"].append({"name": "x"})),
    ("too many obstacles", lambda s: s["obstacles"].update(
        {f"o{i}": {"lane": "ground", "ellipse": [0, 0, 1, 1]} for i in range(9)})),
]


def self_test(validator, spec, path) -> int:
    failed = 0
    base = errors(validator, spec)
    if base:
        print(f"FAIL - {path} must be valid to self-test:\n  " + "\n  ".join(base))
        return 1
    for desc, mutate in MUTATIONS:
        bad = copy.deepcopy(spec)
        mutate(bad)
        errs = errors(validator, bad)
        print(f"{'ok' if errs else 'FAIL'} - rejects {desc}" + (f" ({errs[0]})" if errs else ""))
        failed += not errs
    return failed


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("packs", nargs="+", help="pack directories or pack.yaml files")
    ap.add_argument("--self-test", action="store_true", help="also check that known mistakes are rejected")
    args = ap.parse_args(argv)

    validator = load_validator()
    failed = 0
    for p in args.packs:
        path, spec = load_pack(p)
        errs = errors(validator, spec)
        if errs:
            failed += 1
            print(f"{path}: {len(errs)} schema error(s)")
            for e in errs:
                print(f"  {e}")
        else:
            print(f"{path}: ok")
        if args.self_test and not errs:
            failed += self_test(validator, spec, path)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
