#!/usr/bin/env python3
"""Tests for the Studio dev server: python3 studio/test_server.py

Runs the server on a copy of the forest pack, edits the copy, and checks that
builds, the event stream and the served pack follow along.
"""
from __future__ import annotations

import http.client
import json
import os
import shutil
import sys
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import server  # noqa: E402


class Events:
    """Reads the server-sent event stream in a background thread."""

    def __init__(self, port: int):
        self.conn = http.client.HTTPConnection("127.0.0.1", port, timeout=30)
        self.conn.request("GET", "/studio/events")
        self.resp = self.conn.getresponse()
        self.items: list[dict] = []
        self.cond = threading.Condition()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        try:
            for raw in self.resp:
                line = raw.decode().rstrip("\n")
                if line.startswith("data: "):
                    with self.cond:
                        self.items.append(json.loads(line[6:]))
                        self.cond.notify_all()
        except (OSError, ValueError):
            pass

    def wait(self, pred, timeout=10.0) -> dict:
        with self.cond:
            ok = self.cond.wait_for(lambda: any(pred(s) for s in self.items), timeout)
            if not ok:
                raise AssertionError(f"no matching event; got {self.items}")
            return next(s for s in self.items if pred(s))

    def close(self):
        self.conn.close()


class StudioServerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="mb-studio-")
        cls.pack = os.path.join(cls.tmp, "forest")
        shutil.copytree(os.path.join(ROOT, "packs", "forest"), cls.pack)
        cls.yaml = os.path.join(cls.pack, "pack.yaml")
        with open(cls.yaml, encoding="utf-8") as f:
            cls.original = f.read()
        web = os.path.join(cls.tmp, "web")
        os.makedirs(web)
        with open(os.path.join(web, "microbiome.wasm"), "wb") as f:
            f.write(b"\0asm")  # stands in for the wasm build
        cls.srv, cls.builder, cls.stop = server.start(cls.pack, web, port=0, log=lambda *_: None)
        cls.port = cls.srv.server_address[1]

    @classmethod
    def tearDownClass(cls):
        cls.stop.set()
        cls.srv.shutdown()
        cls.srv.server_close()
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def get(self, path):
        conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=10)
        conn.request("GET", path)
        r = conn.getresponse()
        body = r.read()
        conn.close()
        return r, body

    def write_yaml(self, text):
        with open(self.yaml, "w", encoding="utf-8") as f:
            f.write(text)
        bump(self.yaml)

    def test_1_initial_build_and_static_files(self):
        r, body = self.get("/studio/status")
        s = json.loads(body)
        self.assertEqual((s["state"], s["version"], s["packVersion"], s["pack"]), ("ok", 1, 1, "forest"))
        self.assertEqual(s["warnings"], [])
        r, body = self.get("/studio/pack.mbp")
        self.assertEqual(r.status, 200)
        self.assertEqual(r.getheader("X-Pack-Version"), "1")
        self.assertEqual(body, server.packc.compile_pack(self.pack)[0])
        r, _ = self.get("/")
        self.assertEqual((r.status, r.getheader("Location")), (302, "/index.html?studio"))
        r, body = self.get("/index.html")  # from the source tree
        self.assertEqual(r.status, 200)
        self.assertIn(b"startStudio", body)
        r, _ = self.get("/microbiome.wasm")  # from --web
        self.assertEqual(r.getheader("Content-Type"), "application/wasm")
        for bad in ("/../studio/server.py", "/%2e%2e/README.md", "/nope.js"):
            self.assertEqual(self.get(bad)[0].status, 404, bad)

    def test_2_edit_break_fix(self):
        ev = Events(self.port)
        try:
            first = ev.wait(lambda s: True)
            v = first["version"]
            good = self.get("/studio/pack.mbp")[1]

            # A YAML syntax error: reported, and the last good pack stays served.
            self.write_yaml(self.original + "\nlanes: [\n")
            err = ev.wait(lambda s: s["version"] > v and s["state"] == "error")
            self.assertIn("pack.yaml", err["error"])
            self.assertEqual(err["packVersion"], first["packVersion"])
            r, body = self.get("/studio/pack.mbp")
            self.assertEqual((r.getheader("X-Pack-Version"), body), (str(first["packVersion"]), good))

            # A packc error (not YAML): the message names the field.
            self.write_yaml(self.original.replace("interval: 4s", "interval: soon"))
            err2 = ev.wait(lambda s: s["version"] > err["version"] and s["state"] == "error")
            self.assertIn("soon", err2["error"])

            # Fixed, with a real change: a new pack, and the schema is happy.
            self.write_yaml(self.original.replace("max_actors: 12", "max_actors: 6"))
            ok = ev.wait(lambda s: s["version"] > err2["version"] and s["state"] == "ok")
            self.assertEqual(ok["packVersion"], ok["version"])
            self.assertEqual(ok["warnings"], [])
            r, body = self.get("/studio/pack.mbp")
            self.assertEqual(r.getheader("X-Pack-Version"), str(ok["version"]))
            self.assertNotEqual(body, good)

            # Art changes rebuild too.
            art = os.path.join(self.pack, "art", sorted(os.listdir(os.path.join(self.pack, "art")))[0])
            bump(art)
            ev.wait(lambda s: s["version"] > ok["version"] and s["state"] == "ok")
        finally:
            ev.close()
            self.write_yaml(self.original)

    def test_3_schema_warning(self):
        ev = Events(self.port)
        try:
            v = ev.wait(lambda s: True)["version"]
            # packc ignores unknown keys; the schema flags them.
            self.write_yaml(self.original.replace("name: Forest", "name: Forest\nnmae: typo"))
            s = ev.wait(lambda s: s["version"] > v)
            if server._validator is None:
                self.skipTest("jsonschema not installed")
            self.assertEqual(s["state"], "ok")
            self.assertTrue(any("nmae" in w for w in s["warnings"]), s["warnings"])
        finally:
            ev.close()
            self.write_yaml(self.original)


def bump(path):
    """Move a file's mtime forward, so the change shows on coarse clocks too."""
    t = time.time() + 2
    os.utime(path, (t, t))
    time.sleep(0.05)


if __name__ == "__main__":
    unittest.main(verbosity=2)
