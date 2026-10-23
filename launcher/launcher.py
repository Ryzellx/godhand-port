#!/usr/bin/env python3
"""God Hand Launcher — ISO manager + graphics/pad/keybind config + native port launcher.

Stdlib only. Serves a local web UI and opens it in app-mode window.
Run:  python3 launcher.py [--port 8931] [--no-window]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

HERE = Path(__file__).resolve().parent
UI = HERE / "ui"
CONFIG_DIR = Path(os.environ.get("GH_LAUNCHER_HOME", Path.home() / ".config" / "godhand-launcher"))
CONFIG_FILE = CONFIG_DIR / "config.json"
PORT_ROOT = Path(os.environ.get("GH_PORT_ROOT", "/root/ports/godhand"))
PORT_REPO = Path(os.environ.get("GH_REPO", "/root/projects/godhand-port"))
PROJECT_MD = PORT_ROOT / "PROJECT.md"

DEFAULTS = {
    "iso_path": "",
    "exe_path": "",
    "renderer": "vulkan",          # vulkan | d3d12 | opengl | software
    "resolution": "1920x1080",     # up to 4K + custom
    "window_mode": "windowed",     # windowed | borderless | fullscreen
    "internal_scale": 1,           # 1..4 (supersampling)
    "vsync": True,
    "fps_cap": 60,                 # 0 = uncapped
    "msaa": 1,                     # 1|2|4|8
    "anisotropy": 8,               # 1|2|4|8|16
    "texture_filter": "linear",    # nearest | linear
    "widescreen": False,
    "frame_skip": 0,
    "volume_master": 100,
    "volume_bgm": 80,
    "volume_sfx": 90,
    "audio_backend": "auto",       # auto | wasapi | asio | null
    "language": "en",              # en | jp
    "keybinds": {
        "up": "KeyW", "down": "KeyS", "left": "KeyA", "right": "KeyD",
        "punch": "KeyJ", "kick": "KeyK", "guard": "KeyL", "dodge": "Space",
        "special": "KeyI", "taunt": "KeyU", "pause": "Escape", "confirm": "Enter",
    },
    "gamepad": {
        "enabled": True,
        "deadzone": 0.15,
        "vibration": True,
        "map": {"punch": 2, "kick": 1, "guard": 6, "dodge": 0, "special": 3,
                "taunt": 8, "pause": 9, "confirm": 0},
    },
}

STATE = {"hash": {"status": "idle", "value": None, "pct": 0}, "last_launch": None}
LOCK = threading.Lock()


# ---------------------------------------------------------------- config
def load_config() -> dict:
    cfg = json.loads(json.dumps(DEFAULTS))
    if CONFIG_FILE.exists():
        try:
            saved = json.loads(CONFIG_FILE.read_text("utf-8"))
            for k, v in saved.items():
                if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                    cfg[k].update(v)
                else:
                    cfg[k] = v
        except Exception as e:
            print(f"[cfg] corrupt config ignored: {e}", file=sys.stderr)
    return cfg


def save_config(cfg: dict) -> None:
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    tmp = CONFIG_FILE.with_suffix(".tmp")
    tmp.write_text(json.dumps(cfg, indent=2), "utf-8")
    os.replace(tmp, CONFIG_FILE)


# ---------------------------------------------------------------- ISO
BOOT2_RE = re.compile(rb"BOOT2\s*=\s*cdrom0:\\?([A-Z0-9_.]{4,20});(\d)")
VMODE_RE = re.compile(rb"VMODE\s*=\s*([A-Z]+)")
VER_RE = re.compile(rb"VER\s*=\s*([0-9.]+)")


def inspect_iso(path_str: str) -> dict:
    p = Path(path_str).expanduser()
    if not p.exists():
        return {"ok": False, "error": "file tidak ditemukan"}
    if not p.is_file():
        return {"ok": False, "error": "bukan file"}
    size = p.stat().st_size
    if size < 600 * 1024 * 1024:
        return {"ok": False, "error": f"terlalu kecil untuk ISO PS2 DVD ({size/1e6:.0f} MB)"}

    with p.open("rb") as f:
        f.seek(16 * 2048)
        pvd = f.read(2048)
        if pvd[1:6] != b"CD001":
            return {"ok": False, "error": "bukan ISO9660 (PVD tidak valid)"}
        volid = pvd[40:72].decode("ascii", "ignore").strip()
        f.seek(0)
        head = f.read(32 * 1024 * 1024)

    m = BOOT2_RE.search(head)
    if not m:
        return {"ok": False, "error": "SYSTEM.CNF / BOOT2 tidak ada — bukan disc PS2 yang valid"}
    boot_file = m.group(1).decode("ascii")
    disc_id = boot_file.replace("_", "-")
    vmode = VMODE_RE.search(head)
    ver = VER_RE.search(head)
    known = disc_id.upper() in {"SLUS-215.03", "SLES-54404", "SLPM-66552", "SLUS-21503"}
    return {
        "ok": True,
        "path": str(p),
        "size": size,
        "size_h": f"{size/1e9:.2f} GB",
        "volume_id": volid,
        "boot_file": boot_file,
        "disc_id": disc_id,
        "region": vmode.group(1).decode() if vmode else "UNKNOWN",
        "version": ver.group(1).decode() if ver else "?",
        "title": "God Hand (USA)" if "SLUS" in disc_id else "God Hand (non-US)",
        "recognized": known,
        "sectors": size // 2048,
    }


def hash_worker(path: str) -> None:
    import hashlib
    h = hashlib.md5()
    total = os.path.getsize(path)
    read = 0
    with open(path, "rb") as f:
        while True:
            b = f.read(8 * 1024 * 1024)
            if not b:
                break
            h.update(b)
            read += len(b)
            with LOCK:
                STATE["hash"]["pct"] = round(read * 100 / total, 1)
    with LOCK:
        STATE["hash"].update(status="done", value=h.hexdigest(), pct=100)


# ---------------------------------------------------------------- port status
def port_status() -> dict:
    phases, notes = [], {}
    if PROJECT_MD.exists():
        for line in PROJECT_MD.read_text("utf-8", "ignore").splitlines():
            m = re.match(r"-\s*\[([ x])\]\s*(.+?)(?:\s+—.*)?$", line.strip())
            if not m:
                continue
            done = m.group(1) == "x"
            label = line.strip()[6:].strip()
            phases.append({"done": done, "label": label[:160]})
    done = sum(1 for p in phases if p["done"])
    nxt = next((p["label"] for p in phases if not p["done"]), None)

    binaries = []
    for root in (PORT_REPO / "build", PORT_REPO, PORT_REPO / "release"):
        if not root.is_dir():
            continue
        for f in root.iterdir():
            if f.is_file() and os.access(f, os.X_OK) and f.suffix.lower() in ("", ".exe"):
                if any(k in f.name.lower() for k in ("game", "godhand", "native", "play")):
                    if "test" not in f.name.lower():
                        binaries.append({"path": str(f), "name": f.name,
                                         "size": f.stat().st_size})
    playable = [b for b in binaries if re.search(r"(native|play|main)$", b["name"].lower().split(".")[0])]
    return {
        "phases": phases, "done": done, "total": len(phases),
        "percent": round(done * 100 / len(phases)) if phases else 0,
        "next": nxt, "binaries": binaries,
        "playable_binary": playable[0]["path"] if playable else None,
        "engine_ready": bool(playable),
    }


def valid_exe(path_str: str) -> bool:
    if not path_str:
        return False
    p = Path(path_str).expanduser()
    return p.is_file() and os.access(p, os.X_OK)


# ---------------------------------------------------------------- actions
def native_argv(exe: str, cfg: dict) -> list[str]:
    res = cfg["resolution"].lower().replace(" ", "")
    return [
        exe,
        "--renderer", cfg["renderer"],
        "--resolution", res,
        "--mode", cfg["window_mode"],
        "--scale", str(cfg["internal_scale"]),
        "--vsync", "1" if cfg["vsync"] else "0",
        "--fps", str(cfg["fps_cap"]),
        "--msaa", str(cfg["msaa"]),
        "--aniso", str(cfg["anisotropy"]),
        "--texfilter", cfg["texture_filter"],
        "--widescreen", "1" if cfg["widescreen"] else "0",
        "--iso", cfg["iso_path"],
    ]


def launch(cfg: dict) -> dict:
    iso = inspect_iso(cfg["iso_path"]) if cfg["iso_path"] else {"ok": False, "error": "ISO belum dipilih"}
    if not iso.get("ok"):
        return {"ok": False, "stage": "iso", "error": iso.get("error", "ISO invalid")}

    exe = cfg.get("exe_path") or port_status().get("playable_binary")
    if not exe or not valid_exe(exe):
        return {
            "ok": False, "stage": "engine",
            "error": "Binary native belum ada — engine masih di Phase 7b.9 (spin-loop pasca-crt0).",
            "hint": "Launcher siap; tombol Play aktif otomatis begitu binary port muncul.",
            "next": port_status().get("next"),
        }

    argv = native_argv(exe, cfg)
    try:
        log = CONFIG_DIR / "launch.log"
        CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        fh = log.open("ab")
        proc = subprocess.Popen(argv, stdout=fh, stderr=subprocess.STDOUT, cwd=str(Path(exe).parent))
        STATE["last_launch"] = {"pid": proc.pid, "argv": argv, "at": time.time()}
        return {"ok": True, "pid": proc.pid, "argv": argv, "log": str(log)}
    except Exception as e:
        return {"ok": False, "stage": "exec", "error": f"{type(e).__name__}: {e}", "argv": argv}


def run_diag(seconds: int = 45) -> dict:
    exe = PORT_REPO / "build" / "crt0_test"
    if not exe.exists():
        return {"ok": False, "error": f"tidak ada {exe}"}
    try:
        r = subprocess.run([str(exe), str(PORT_ROOT / "extract" / "SLUS_215.03"), "5000000"],
                           capture_output=True, text=True, timeout=seconds, cwd=str(PORT_REPO))
        out = (r.stdout or "") + (r.stderr or "")
        return {"ok": r.returncode == 0, "code": r.returncode, "tail": out.strip().splitlines()[-25:]}
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": "timeout", "tail": []}
    except Exception as e:
        return {"ok": False, "error": f"{type(e).__name__}: {e}", "tail": []}


def browse(kind: str = "iso") -> str:
    """Native file dialog in a child process (tkinter is not thread-safe)."""
    label = {"iso": "Pilih ISO God Hand", "exe": "Pilih executable port"}.get(kind, "Pilih file")
    types = ("*.iso *.ISO",) if kind == "iso" else ("*.exe *.EXE", "*")
    code = f"""
import tkinter as tk
from tkinter import filedialog
r=tk.Tk(); r.withdraw(); r.attributes('-topmost',True)
print(filedialog.askopenfilename(title={label!r}, filetypes=[({types[0]!r} if len({types})==1 else ("Executable","*") , {(types[0] if kind=='iso' else '*')!r})]))
"""
    try:
        p = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, timeout=180)
        return p.stdout.strip()
    except Exception:
        return ""


# ---------------------------------------------------------------- HTTP
class Handler(BaseHTTPRequestHandler):
    server_version = "GodHandLauncher/1.0"

    def log_message(self, *a):
        pass

    def _send(self, code: int, body: bytes, ctype: str = "application/json"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code=200):
        self._send(code, json.dumps(obj).encode())

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/api/state":
            cfg = load_config()
            iso = inspect_iso(cfg["iso_path"]) if cfg["iso_path"] else None
            with LOCK:
                h = dict(STATE["hash"])
            return self._json({"config": cfg, "iso": iso, "port": port_status(),
                               "hash": h, "last_launch": STATE["last_launch"]})
        if path == "/api/hash":
            with LOCK:
                return self._json(STATE["hash"])
        if path == "/api/diag":
            return self._json(run_diag())
        if path in ("/", "/index.html"):
            f = UI / "index.html"
        else:
            f = (UI / path.lstrip("/")).resolve()
            if not str(f).startswith(str(UI.resolve())):
                return self._send(403, b"forbidden", "text/plain")
        if not f.exists():
            return self._send(404, b"not found", "text/plain")
        ctype = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
                 ".js": "application/javascript; charset=utf-8", ".svg": "image/svg+xml",
                 ".png": "image/png", ".jpg": "image/jpeg", ".woff2": "font/woff2"}.get(f.suffix, "application/octet-stream")
        self._send(200, f.read_bytes(), ctype)

    def do_POST(self):
        path = urlparse(self.path).path
        n = int(self.headers.get("Content-Length", 0) or 0)
        try:
            body = json.loads(self.rfile.read(n) or b"{}")
        except Exception:
            return self._json({"ok": False, "error": "bad json"}, 400)

        if path == "/api/iso":
            return self._json(inspect_iso(body.get("path", "")))
        if path == "/api/browse":
            p = browse(body.get("kind", "iso"))
            return self._json({"ok": bool(p), "path": p})
        if path == "/api/save":
            cfg = load_config()
            for k, v in body.items():
                if k in cfg:
                    cfg[k] = v
            save_config(cfg)
            return self._json({"ok": True, "config": cfg})
        if path == "/api/hash":
            cfg = load_config()
            if not cfg["iso_path"] or not Path(cfg["iso_path"]).exists():
                return self._json({"ok": False, "error": "ISO belum dipilih"}, 400)
            with LOCK:
                STATE["hash"] = {"status": "running", "value": None, "pct": 0}
            threading.Thread(target=hash_worker, args=(cfg["iso_path"],), daemon=True).start()
            return self._json({"ok": True})
        if path == "/api/play":
            cfg = load_config()
            res = launch(cfg)
            return self._json(res, 200 if res.get("ok") else 409)
        if path == "/api/reveal":
            cfg = load_config()
            p = cfg.get("iso_path") or ""
            return self._json({"ok": bool(p), "path": p})
        return self._json({"ok": False, "error": "unknown endpoint"}, 404)


def app_mode_url(url: str) -> None:
    """Open a chrome-less window when a chromium-family browser exists."""
    for name in ("chromium", "chromium-browser", "google-chrome", "microsoft-edge", "brave-browser"):
        exe = shutil.which(name)
        if exe:
            subprocess.Popen([exe, f"--app={url}", "--window-size=1280,860",
                              "--user-data-dir=" + str(CONFIG_DIR / "browser")],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            return
    if sys.platform.startswith("win"):
        for exe in (r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
                    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"):
            if Path(exe).exists():
                subprocess.Popen([exe, f"--app={url}", "--window-size=1280,860"])
                return
    webbrowser.open(url)


def free_port(preferred: int) -> int:
    for p in [preferred, 0]:
        try:
            s = socket.socket()
            s.bind(("127.0.0.1", p))
            port = s.getsockname()[1]
            s.close()
            return port
        except OSError:
            continue
    return preferred


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8931)
    ap.add_argument("--no-window", action="store_true")
    ap.add_argument("--open", action="store_true", help="buka UI lalu keluar dari foreground")
    args = ap.parse_args()

    port = free_port(args.port)
    srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    url = f"http://127.0.0.1:{port}/"
    print(f"God Hand Launcher → {url}")
    if not args.no_window:
        threading.Thread(target=lambda: (time.sleep(0.4), app_mode_url(url)), daemon=True).start()
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
