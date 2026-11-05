#!/usr/bin/env python3
"""microbiome Studio dev server: live preview of a pack folder.

    python3 studio/server.py packs/forest
    # then open http://127.0.0.1:8000

Watches the pack folder (and any art it uses from elsewhere), recompiles with
packc whenever a file changes, and pushes the result to the browser viewer,
which reloads the pack in place and keeps its camera, clock and pins. If a
build fails, the viewer keeps showing the last good pack with the error over
it. Schema problems (if jsonschema is installed) show as warnings.

The viewer page and its JS wrapper are served from the source tree, so edits
to them only need a browser refresh. The wasm module comes from the wasm
build (default build-wasm/web): rebuild it after changing the engine core or
platform/wasm/mbw.c.

Routes: / redirects to the viewer in studio mode; /studio/pack.mbp is the
last good build; /studio/status is the build status as JSON; /studio/events
streams that status as server-sent events on every build.

Listens on 127.0.0.1 only by default: later versions write pack files.
"""
from __future__ import annotations

import argparse
import http.server
import json
import mimetypes
import os
import sys
import threading
import time
import traceback

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools", "packc"))

import packc  # noqa: E402  (needs the path above)

try:
    import check_schema  # noqa: E402
    _validator = check_schema.load_validator()
except (ImportError, SystemExit):  # check_schema exits when jsonschema is missing
    check_schema = None
    _validator = None

# Served from the source tree first, so viewer edits need no rebuild.
SOURCE_WEB = [os.path.join(ROOT, "platform", "wasm", "web"), os.path.join(ROOT, "platform", "wasm", "js")]

POLL_S = 0.25          # how often the watcher checks file times
KEEPALIVE_S = 15       # SSE comment interval, so proxies and browsers keep the stream


class Builder:
    """Watches a pack folder and keeps the latest build and its status."""

    def __init__(self, pack_dir: str):
        self.pack_dir = os.path.abspath(pack_dir)
        self.name = os.path.basename(self.pack_dir.rstrip(os.sep)) or "pack"
        self.inputs: set[str] = set()
        self.pack: bytes | None = None      # last good build
        self.pack_version = 0               # status version that produced self.pack
        # Changes when the server restarts, so viewers know versions start over.
        self.session = f"{os.getpid()}-{int(time.time())}"
        self.status: dict = {"version": 0, "state": "building", "pack": self.name, "session": self.session}
        self.cond = threading.Condition()

    def _files(self) -> dict[str, float]:
        files = set(self.inputs)
        for dp, dns, fns in os.walk(self.pack_dir):
            dns[:] = [d for d in dns if not d.startswith(".")]
            files |= {os.path.join(dp, f) for f in fns if not f.startswith(".")}
        out = {}
        for f in files:
            try:
                out[f] = os.path.getmtime(f)
            except OSError:
                pass  # deleted between walk and stat
        return out

    def _short(self, msg: str) -> str:
        """Paths inside the pack folder read better relative to it."""
        return msg.replace(self.pack_dir + os.sep, "")

    def build(self) -> dict:
        t0 = time.monotonic()
        status: dict = {"pack": self.name, "session": self.session, "warnings": []}
        try:
            data, b = packc.compile_pack(self.pack_dir)
        except packc.PackError as exc:
            status.update(state="error", error=self._short(str(exc)))
        except Exception as exc:  # a packc bug or an input it doesn't guard against
            status.update(state="error", error=f"packc crashed: {exc!r}",
                          detail=traceback.format_exc(limit=4))
        else:
            self.inputs = set(b.inputs)
            status.update(state="ok", size=len(data), summary=packc.summary(data, b))
            status["warnings"] += [self._short(w) for w in b.warnings]
        if _validator is not None:
            try:
                _, spec = check_schema.load_pack(self.pack_dir)
                status["warnings"] += [f"schema: {e}" for e in check_schema.errors(_validator, spec)]
            except Exception:
                pass  # packc already reported why pack.yaml can't be read
        status["ms"] = round((time.monotonic() - t0) * 1000)
        status["time"] = time.strftime("%H:%M:%S")
        with self.cond:
            status["version"] = self.status["version"] + 1
            if status["state"] == "ok":
                self.pack = data
                self.pack_version = status["version"]
            status["packVersion"] = self.pack_version  # what /studio/pack.mbp serves
            self.status = status
            self.cond.notify_all()
        return status

    def watch(self, stop: threading.Event, log=print):
        seen = self._files()
        while not stop.wait(POLL_S):
            now = self._files()
            if now == seen:
                continue
            s = self.build()
            seen = self._files()  # inputs may have changed with the build
            if s["state"] == "ok":
                log(f"studio: rebuilt {self.name} in {s['ms']} ms ({s['size'] / 1024:.0f} KiB)"
                    + (f", {len(s['warnings'])} warning(s)" if s["warnings"] else ""))
            else:
                log(f"studio: build failed: {s['error']}")

    def wait_newer(self, version: int, timeout: float) -> dict:
        with self.cond:
            self.cond.wait_for(lambda: self.status["version"] > version, timeout)
            return self.status


def make_handler(builder: Builder, web_dirs: list[str]):
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"
        server_version = "microbiome-studio"

        def log_message(self, fmt, *args):  # quiet: the watcher reports builds
            pass

        def _send(self, code: int, body: bytes, ctype: str, extra: dict | None = None):
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            for k, v in (extra or {}).items():
                self.send_header(k, v)
            self.end_headers()
            if self.command != "HEAD":
                self.wfile.write(body)

        def do_HEAD(self):
            self.do_GET()

        def do_GET(self):
            path = self.path.split("?", 1)[0]
            if path == "/":
                self.send_response(302)
                self.send_header("Location", "/index.html?studio")
                self.send_header("Content-Length", "0")
                self.end_headers()
            elif path == "/studio/status":
                self._send(200, json.dumps(builder.status).encode(), "application/json")
            elif path == "/studio/pack.mbp":
                with builder.cond:
                    data, ver = builder.pack, builder.pack_version
                if data is None:
                    self._send(503, b"no successful build yet", "text/plain; charset=utf-8")
                else:
                    self._send(200, data, "application/octet-stream", {"X-Pack-Version": str(ver)})
            elif path == "/studio/events":
                self._events()
            else:
                self._static(path)

        def _events(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            version = -1
            try:
                while True:
                    s = builder.wait_newer(version, KEEPALIVE_S)
                    if s["version"] > version:
                        version = s["version"]
                        self.wfile.write(f"data: {json.dumps(s)}\n\n".encode())
                    else:
                        self.wfile.write(b": keepalive\n\n")
                    self.wfile.flush()
            except (BrokenPipeError, ConnectionResetError, OSError):
                pass  # browser closed the tab or reloaded

        def _static(self, path: str):
            rel = os.path.normpath(path.lstrip("/"))
            if rel.startswith("..") or os.path.isabs(rel):
                return self._send(404, b"not found", "text/plain; charset=utf-8")
            for d in web_dirs:
                f = os.path.join(d, rel)
                if os.path.isfile(f):
                    with open(f, "rb") as fh:
                        body = fh.read()
                    ctype = "application/wasm" if f.endswith(".wasm") else \
                        "text/javascript" if f.endswith(".js") else \
                        mimetypes.guess_type(f)[0] or "application/octet-stream"
                    return self._send(200, body, ctype)
            self._send(404, b"not found", "text/plain; charset=utf-8")

    return Handler


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True      # open event streams don't block shutdown
    allow_reuse_address = True


def start(pack_dir: str, web: str, host="127.0.0.1", port=8000, log=print):
    """Build once, then serve and watch in background threads. Returns
    (server, builder, stop_event); call server.shutdown() and stop.set() to end."""
    builder = Builder(pack_dir)
    s = builder.build()
    if s["state"] != "ok":
        log(f"studio: build failed: {s['error']}")
    server = Server((host, port), make_handler(builder, SOURCE_WEB + [os.path.abspath(web)]))
    stop = threading.Event()
    threading.Thread(target=builder.watch, args=(stop, log), daemon=True).start()
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, builder, stop


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pack_dir", help="pack folder (the one with pack.yaml)")
    ap.add_argument("--web", default=os.path.join(ROOT, "build-wasm", "web"),
                    help="wasm build output with microbiome.wasm (default: build-wasm/web)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8000)
    args = ap.parse_args(argv)

    if not os.path.isfile(os.path.join(args.pack_dir, "pack.yaml")):
        print(f"studio: no pack.yaml in {args.pack_dir}", file=sys.stderr)
        return 2
    if not os.path.isfile(os.path.join(args.web, "microbiome.wasm")):
        print(f"studio: no microbiome.wasm in {args.web}; build it first:\n"
              "  cmake -S platform/wasm -B build-wasm && cmake --build build-wasm", file=sys.stderr)
        return 2
    try:
        server, builder, stop = start(args.pack_dir, args.web, args.host, args.port)
    except OSError as exc:
        print(f"studio: cannot listen on {args.host}:{args.port}: {exc}", file=sys.stderr)
        return 1
    host = "127.0.0.1" if args.host in ("", "0.0.0.0") else args.host
    print(f"studio: {builder.name} at http://{host}:{server.server_address[1]}/ (ctrl-c to stop)")
    try:
        while True:
            time.sleep(3600)
    except KeyboardInterrupt:
        stop.set()
        server.shutdown()
    return 0


if __name__ == "__main__":
    sys.exit(main())
