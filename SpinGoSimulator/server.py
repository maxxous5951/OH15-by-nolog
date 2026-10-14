#!/usr/bin/env python3
"""Interface web locale du simulateur Spin & Go (stdlib uniquement).

Lancement :  python3 server.py [--port 8765]   puis ouvrir http://127.0.0.1:8765
"""
import argparse
import glob
import json
import os
import re
import sys
import threading
import traceback
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from spingosim.config import SimConfig, DEFAULT_BLIND_LEVELS, DEFAULT_MULTIPLIERS  # noqa: E402
from spingosim.runner import run_simulation, Progress, unique_names  # noqa: E402
from spingosim import symbols as S  # noqa: E402
from dataclasses import asdict  # noqa: E402

OUT_ROOT = os.path.join(HERE, "output")
UPLOADS = os.path.join(HERE, "uploads")
PROFILES = os.path.join(HERE, "profiles")
REPO = S.find_repo_root(HERE)

STATE = {"progress": None, "thread": None, "lock": threading.Lock()}


def list_profiles():
    items = [{"label": "DefaultBot (Termita SNG)", "path": "DefaultBot", "group": "Intégrés"}]
    for g, d in (("Exemples", PROFILES), ("Importés", UPLOADS)):
        for p in sorted(glob.glob(os.path.join(d, "*.ohf"))):
            items.append({"label": os.path.basename(p), "path": p, "group": g})
    if REPO:
        for p in sorted(glob.glob(os.path.join(REPO, "DemoBots", "**", "*.ohf"), recursive=True)):
            rel = os.path.relpath(p, os.path.join(REPO, "DemoBots"))
            items.append({"label": rel, "path": p, "group": "DemoBots OpenHoldem"})
    return items


def check_profile(path):
    if path != "DefaultBot" and not os.path.isfile(path):
        return {"ok": False, "error": "Fichier introuvable"}
    allowed = [PROFILES, UPLOADS] + ([os.path.join(REPO, "DemoBots")] if REPO else [])
    if path != "DefaultBot" and not any(os.path.abspath(path).startswith(os.path.abspath(a) + os.sep) for a in allowed):
        return {"ok": False, "error": "Chemin non autorisé"}
    fc = S.build_profile(path, SimConfig())
    errs = S.profile_errors(fc)
    user_fn = [f for f in fc.functions.values() if not f.read_only]
    return {"ok": True, "type": "OH-script" if S.is_ohscript(fc) else "OpenPPL",
            "functions": len(user_fn), "errors": [{"function": n, "message": e} for n, e in errs][:20],
            "error_count": len(errs)}


def safe_run_dir(name):
    if not re.match(r"^[A-Za-z0-9_.-]+$", name or ""):
        raise ValueError("nom de run invalide")
    d = os.path.join(OUT_ROOT, name)
    if not os.path.isdir(d):
        raise ValueError("run introuvable")
    return d


def default_config():
    d = asdict(SimConfig())
    d["players"] = [{"name": "Joueur %d" % (i + 1), "path": "DefaultBot"} for i in range(3)]
    d["output_dir"] = ""
    return d


def start_run(body):
    with STATE["lock"]:
        th = STATE["thread"]
        if th and th.is_alive():
            raise ValueError("Une simulation est déjà en cours")
        body = dict(body)
        stamp = __import__("time").strftime("%Y%m%d_%H%M%S")
        run_name = "run_" + stamp
        body["output_dir"] = os.path.join(OUT_ROOT, run_name)
        cfg = SimConfig.from_dict(body)
        for p in cfg.players:
            r = check_profile(p.get("path") or "DefaultBot")
            if not r["ok"]:
                raise ValueError("Profil %s : %s" % (p.get("name"), r["error"]))
        prog = Progress(cfg.num_games)
        STATE["progress"] = prog
        prog.run_name = run_name

        def work():
            try:
                run_simulation(cfg, prog)
            except Exception:
                prog.error = traceback.format_exc(limit=3)
                prog.finished = True
        th = threading.Thread(target=work, daemon=True)
        STATE["thread"] = th
        th.start()
        return run_name


def list_runs():
    out = []
    if os.path.isdir(OUT_ROOT):
        for n in sorted(os.listdir(OUT_ROOT), reverse=True):
            sp = os.path.join(OUT_ROOT, n, "summary.json")
            if os.path.isfile(sp):
                try:
                    s = json.load(open(sp, encoding="utf-8"))
                    out.append({"name": n, "games": s["games"], "seconds": s["seconds"],
                                "players": [p["name"] for p in s["players"]]})
                except Exception:
                    pass
    return out


def read_hands(run, player, offset, limit):
    d = safe_run_dir(run)
    if player in ("", "global", None):
        fp = os.path.join(d, "handhistory_global.txt")
    else:
        if not re.match(r"^[A-Za-z0-9_.-]+$", player):
            raise ValueError("joueur invalide")
        fp = os.path.join(d, "players", player + "_hands.txt")
    if not os.path.isfile(fp):
        return {"text": "", "total": 0}
    with open(fp, encoding="utf-8") as f:
        blocks = f.read().strip().split("\n\n")
    hands, cur = [], []
    for b in blocks:
        if b.startswith("SpinSim Hand"):
            if cur:
                hands.append("\n\n".join(cur))
            cur = [b]
        else:
            cur.append(b)
    if cur:
        hands.append("\n\n".join(cur))
    return {"total": len(hands), "text": "\n\n".join(hands[offset:offset + limit])}


class Handler(BaseHTTPRequestHandler):
    server_version = "SpinGoSim/1.0"

    def log_message(self, fmt, *args):
        pass

    def _json(self, obj, code=200):
        data = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _body(self):
        n = int(self.headers.get("Content-Length") or 0)
        if n > 5 * 1024 * 1024:
            raise ValueError("corps trop gros")
        return json.loads(self.rfile.read(n) or b"{}")

    def do_GET(self):
        u = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(u.query).items()}
        try:
            if u.path in ("/", "/index.html"):
                data = open(os.path.join(HERE, "ui", "index.html"), "rb").read()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            elif u.path == "/api/defaults":
                self._json(default_config())
            elif u.path == "/api/profiles":
                self._json(list_profiles())
            elif u.path == "/api/status":
                p = STATE["progress"]
                if p is None:
                    self._json({"idle": True})
                else:
                    d = p.as_dict()
                    d["run_name"] = getattr(p, "run_name", "")
                    self._json(d)
            elif u.path == "/api/runs":
                self._json(list_runs())
            elif u.path == "/api/stats":
                d = safe_run_dir(q.get("run"))
                self._json(json.load(open(os.path.join(d, "summary.json"), encoding="utf-8")))
            elif u.path == "/api/hands":
                self._json(read_hands(q.get("run"), q.get("player"), int(q.get("offset", 0)), min(int(q.get("limit", 20)), 200)))
            elif u.path == "/api/download":
                d = safe_run_dir(q.get("run"))
                fn = q.get("file", "")
                if not re.match(r"^[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)?$", fn) or ".." in fn:
                    raise ValueError("fichier invalide")
                fp = os.path.join(d, fn)
                data = open(fp, "rb").read()
                self.send_response(200)
                self.send_header("Content-Type", "text/plain; charset=utf-8")
                self.send_header("Content-Disposition", 'attachment; filename="%s"' % os.path.basename(fp))
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)
            else:
                self._json({"error": "not found"}, 404)
        except Exception as e:
            self._json({"error": str(e)}, 400)

    def do_POST(self):
        u = urlparse(self.path)
        try:
            b = self._body()
            if u.path == "/api/run":
                self._json({"run": start_run(b)})
            elif u.path == "/api/cancel":
                if STATE["progress"]:
                    STATE["progress"].cancel.set()
                self._json({"ok": True})
            elif u.path == "/api/check":
                self._json(check_profile(b.get("path", "DefaultBot")))
            elif u.path == "/api/upload":
                name = re.sub(r"[^A-Za-z0-9_.-]+", "_", os.path.basename(b.get("name", "profil.ohf")))
                if not name.lower().endswith(".ohf"):
                    name += ".ohf"
                os.makedirs(UPLOADS, exist_ok=True)
                fp = os.path.join(UPLOADS, name)
                with open(fp, "w", encoding="utf-8", newline="") as f:
                    f.write(b.get("content", ""))
                self._json({"path": fp, "label": name, "check": check_profile(fp)})
            else:
                self._json({"error": "not found"}, 404)
        except Exception as e:
            self._json({"error": str(e)}, 400)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--host", default="127.0.0.1")
    a = ap.parse_args()
    os.makedirs(OUT_ROOT, exist_ok=True)
    srv = ThreadingHTTPServer((a.host, a.port), Handler)
    print("Simulateur Spin & Go : http://%s:%d  (Ctrl+C pour arrêter)" % (a.host, a.port))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
