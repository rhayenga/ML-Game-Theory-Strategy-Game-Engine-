#!/usr/bin/env python3
# Local HTTP server: static UI + JSON bridge to catan_bridge.

from __future__ import annotations

import json
import os
import subprocess
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
BRIDGE = ROOT / "build" / "catan_bridge"
TRAIN = ROOT / "build" / "catan_train"
TORCH_TRAIN = ROOT / "ml" / "train_value.py"
WEB = Path(__file__).resolve().parent
HOST, PORT = "127.0.0.1", 8765


def _torch_available() -> bool:
    try:
        import torch  # noqa: F401

        return True
    except Exception:
        return False


class TrainJob:

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.running = False
        self.games = 0
        self.done = 0
        self.log_tail: list[str] = []
        self.error: str | None = None
        self.last_result: dict | None = None
        self.proc: subprocess.Popen | None = None

    def status(self) -> dict:
        with self.lock:
            return {
                "ok": True,
                "running": self.running,
                "games": self.games,
                "done": self.done,
                "error": self.error,
                "log_tail": list(self.log_tail[-12:]),
                "last_result": self.last_result,
            }

    def _append_log(self, line: str) -> None:
        self.log_tail.append(line)
        if len(self.log_tail) > 40:
            self.log_tail = self.log_tail[-40:]

    def start(self, games: int = 1000) -> dict:
        with self.lock:
            if self.running:
                return {
                    "ok": False,
                    "error": "training already running",
                    "running": True,
                    "games": self.games,
                    "done": self.done,
                }
            if not TRAIN.exists():
                return {"ok": False, "error": f"missing {TRAIN} — run make build/catan_train"}
            if not TORCH_TRAIN.exists():
                return {"ok": False, "error": f"missing {TORCH_TRAIN}"}
            if not _torch_available():
                return {
                    "ok": False,
                    "error": "PyTorch required for Train — run: make ui   (or pip install -r ml/requirements.txt in your venv)",
                }
            self.running = True
            self.games = max(1, int(games))
            self.done = 0
            self.error = None
            self.last_result = None
            self.log_tail = []
            seed = int.from_bytes(os.urandom(4), "little") or 1
            # C++ self-play dumps samples + visit memory; lr=0 so PyTorch alone updates weights.
            cmd = [
                str(TRAIN),
                str(self.games),
                "--seed",
                str(seed),
                "--epsilon",
                "0.12",
                "--lr",
                "0",
                "--weights",
                "build/eval_weights.json",
                "--stats",
                "build/train_stats.json",
                "--samples",
                "build/train_samples.jsonl",
            ]
            self.proc = subprocess.Popen(
                cmd,
                cwd=str(ROOT),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )
            threading.Thread(target=self._watch, args=(self.proc, self.games), daemon=True).start()
            return {
                "ok": True,
                "running": True,
                "games": self.games,
                "done": 0,
                "seed": seed,
                "message": f"PyTorch train: {self.games} self-play games then value-net fit (seed {seed})",
            }

    def _run_pytorch(self) -> None:
        samples = ROOT / "build" / "train_samples.jsonl"
        if not samples.exists() or samples.stat().st_size == 0:
            raise RuntimeError("No feature samples from self-play — cannot run PyTorch training")
        with self.lock:
            self._append_log("PyTorch value-net training…")
        cmd = [
            sys.executable,
            str(TORCH_TRAIN),
            "--samples",
            "build/train_samples.jsonl",
            "--weights",
            "build/eval_weights.json",
            "--checkpoint",
            "build/value_net.pt",
        ]
        proc = subprocess.Popen(
            cmd,
            cwd=str(ROOT),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert proc.stdout
        for line in proc.stdout:
            line = line.rstrip()
            with self.lock:
                self._append_log(line)
        code = proc.wait()
        if code != 0:
            raise RuntimeError(f"PyTorch trainer exited with code {code}")
        with self.lock:
            self._append_log("PyTorch training done — eval_weights.json + value_net.pt updated.")

    def _watch(self, proc: subprocess.Popen, games: int) -> None:
        assert proc.stdout
        try:
            for line in proc.stdout:
                line = line.rstrip()
                with self.lock:
                    self._append_log(line)
                    if "game " in line and "/" in line:
                        try:
                            part = line.strip().split()[1]
                            cur = int(part.split("/")[0])
                            self.done = cur
                        except (IndexError, ValueError):
                            pass
            code = proc.wait()
            if code != 0:
                with self.lock:
                    self.running = False
                    self.error = f"self-play exited with code {code}"
                return
            try:
                self._run_pytorch()
            except Exception as e:
                with self.lock:
                    self.running = False
                    self.done = games
                    self.error = str(e)
                    self._append_log(f"Training failed: {e}")
                return
            with self.lock:
                self.running = False
                self.done = games
                self.last_result = {
                    "games": games,
                    "weights": "build/eval_weights.json",
                    "checkpoint": "build/value_net.pt",
                    "stats": "build/train_stats.json",
                    "visits": "build/position_visits.json",
                    "samples": "build/train_samples.jsonl",
                    "pytorch": True,
                }
                self._append_log(
                    "Training finished — PyTorch weights saved. Click New game (or reload) to use them."
                )
        except Exception as e:
            with self.lock:
                self.running = False
                self.error = str(e)

TRAIN_JOB = TrainJob()

class Engine:
    def __init__(self) -> None:
        if not BRIDGE.exists():
            raise SystemExit(f"Missing {BRIDGE}. Run: make -j4 build/catan_bridge")
        self.proc = subprocess.Popen(
            [str(BRIDGE)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            cwd=str(ROOT),
        )
        self.lock = threading.Lock()

    def call(self, payload: dict) -> dict:
        line = json.dumps(payload, separators=(",", ":"))
        with self.lock:
            assert self.proc.stdin and self.proc.stdout
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()
            out = self.proc.stdout.readline()
            if not out:
                err = self.proc.stderr.read() if self.proc.stderr else ""
                raise RuntimeError(f"bridge died: {err}")
            return json.loads(out)

    def close(self) -> None:
        try:
            self.call({"op": "quit"})
        except Exception:
            pass
        self.proc.terminate()

ENGINE = Engine()

class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _send(self, code: int, body: bytes, content_type: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, code: int, obj: dict) -> None:
        self._send(code, json.dumps(obj).encode(), "application/json")

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/" or path == "/index.html":
            data = (WEB / "index.html").read_bytes()
            self._send(200, data, "text/html; charset=utf-8")
            return
        if path == "/app.js":
            self._send(200, (WEB / "app.js").read_bytes(), "application/javascript")
            return
        if path == "/style.css":
            self._send(200, (WEB / "style.css").read_bytes(), "text/css")
            return
        if path == "/api/train/status":
            self._json(200, TRAIN_JOB.status())
            return
        self._json(404, {"ok": False, "error": "not found"})

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        length = int(self.headers.get("Content-Length", "0"))
        raw = self.rfile.read(length) if length else b"{}"
        try:
            body = json.loads(raw.decode() or "{}")
        except json.JSONDecodeError:
            self._json(400, {"ok": False, "error": "bad json"})
            return

        try:
            if path == "/api/new":
                payload = {
                    "op": "new",
                    "you": int(body.get("you", 0)),
                    "seed": int(body.get("seed", 42)),
                    "weights": body.get("weights", "build/eval_weights.json"),
                }
                if "opp_subopt" in body:
                    payload["opp_subopt"] = float(body["opp_subopt"])
                self._json(200, ENGINE.call(payload))
            elif path == "/api/advise":
                self._json(
                    200,
                    ENGINE.call({"op": "advise", "sims": int(body.get("sims", 800))}),
                )
            elif path == "/api/apply":
                act = body.get("action") or body
                payload = {"op": "apply", **act}
                self._json(200, ENGINE.call(payload))
            elif path == "/api/auto":
                self._json(200, ENGINE.call({"op": "auto"}))
            elif path == "/api/state":
                self._json(200, ENGINE.call({"op": "state"}))
            elif path == "/api/train":
                games = int(body.get("games", 1000))
                self._json(200, TRAIN_JOB.start(games))
            elif path == "/api/reload_visits":
                self._json(200, ENGINE.call({"op": "reload_visits"}))
            else:
                self._json(404, {"ok": False, "error": "not found"})
        except Exception as e:
            self._json(500, {"ok": False, "error": str(e)})

def main() -> None:
    print(ENGINE.call({"op": "ping"}))
    httpd = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"Catan UI → http://{HOST}:{PORT}")
    print("Open that URL in your browser. Ctrl+C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        ENGINE.close()
        httpd.server_close()

if __name__ == "__main__":
    main()
