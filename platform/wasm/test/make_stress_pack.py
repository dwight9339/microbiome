#!/usr/bin/env python3
"""Write a pack built to stress the wasm host's string handling, then compile it.

    python3 make_stress_pack.py REPO_ROOT OUT_DIR OUT.mbp

- Names and labels with HTML markup, quotes, backslashes, tabs and non-ASCII,
  which must round-trip exactly through the JSON API and must never be
  interpreted as markup by the viewer.
- Enough long state names that the pack-info JSON is well over 64 KiB, the
  host's initial JSON area, so the grow-and-retry path runs on every load.
"""
import os
import subprocess
import sys

import yaml

root, out_dir, out_mbp = sys.argv[1:4]
art = os.path.join(os.path.abspath(root), "packs", "forest", "art")

NAMES = ['<img src=x onerror="window.__pwned=1">', 'quote"back\\slash\ttab', "hérisson 🦔"]
STATES = 120          # per actor
STATE_NAME_LEN = 220  # 3 x 120 x ~220 bytes of names alone is ~80 KiB of JSON


def state_name(i):
    stem = f"<b>state {i}</b> \"q\" \\ "
    return stem + "x" * (STATE_NAME_LEN - len(stem))


actors = {}
for n in NAMES:
    names = [state_name(i) for i in range(STATES)]
    states = {}
    for i, s in enumerate(names):
        states[s] = {"anim": "walk", "move": "wander", "duration": "1s",
                     "next": names[(i + 1) % STATES]}
    actors[n] = {"sheet": os.path.join(art, "deer.png"), "frame": [32, 32], "lane": "ground",
                 "speed": 14, "anims": {"walk": {"frames": "0-3", "fps": 6}}, "states": states}

pack = {
    "name": '<script>window.__pwned=1</script> "Stress"',
    "world": {"size": [1280, 300], "spawning": {"interval": "1s", "chance": 100, "max_actors": 12}},
    "vars": {
        "mood": {"step": "hour", "table": {0: "<i>calm</i>", 12: 'say "hi"'}},
        # 255 bytes each (packc's limit), the same up to the last byte.
        "long": {"step": "hour", "table": {0: "x" * 254 + "A", 12: "x" * 254 + "B"}},
    },
    "layers": [{"name": "<u>ground</u>", "image": os.path.join(art, "ground.png")}],
    "lanes": {"ground": [232, 290]},
    "actors": actors,
    "spawns": [{"actor": n, "max": 4, "count": [1, 2]} for n in NAMES],
}

os.makedirs(out_dir, exist_ok=True)
with open(os.path.join(out_dir, "pack.yaml"), "w", encoding="utf-8") as f:
    yaml.safe_dump(pack, f, allow_unicode=True, sort_keys=False)
packc = os.path.join(root, "tools", "packc", "packc.py")
sys.exit(subprocess.call([sys.executable, packc, out_dir, "-o", out_mbp]))
