"""Lancement d'une simulation de N parties (multiprocessing, reproductible)."""
import csv
import json
import multiprocessing as mp
import os
import random
import threading
import time

from .config import SimConfig
from .engine import Game
from .handhistory import OutputWriter
from .stats import PlayerStats, update_from_game
from . import symbols as S


def unique_names(players):
    seen = {}
    out = []
    for p in players:
        base = p.get("name") or os.path.splitext(os.path.basename(p.get("path", "DefaultBot")))[0] or "Player"
        k = seen.get(base, 0)
        seen[base] = k + 1
        out.append(base if k == 0 else "%s_%d" % (base, k + 1))
    return out


def _play_one(args):
    cfg_dict, game_id = args
    cfg = SimConfig.from_dict(cfg_dict)
    rng = random.Random((cfg.seed * 1000003 + game_id) & 0xFFFFFFFF)
    n = len(cfg.players)
    order = list(range(n))
    if cfg.rotate_seats:
        rng.shuffle(order)
    names = unique_names(cfg.players)
    brains, nm, pids = [], [], []
    for seat in range(n):
        pid = order[seat]
        pl = cfg.players[pid]
        b = S.make_brain(pl.get("path") or "DefaultBot", cfg, random.Random(rng.random()), names[pid])
        brains.append(b)
        nm.append(names[pid])
        pids.append(pid)
    game = Game(cfg, brains, nm, game_id, rng, profile_ids=pids)
    res = game.play()
    res["unknown_symbols"] = sorted({u for b in brains for u in b.ctx.unknown})
    return res


class Progress:
    def __init__(self, total):
        self.total = total
        self.done = 0
        self.started = time.time()
        self.finished = False
        self.error = None
        self.rows = []
        self.out_dir = ""
        self.unknown = set()
        self.cancel = threading.Event()

    def as_dict(self):
        el = time.time() - self.started
        return {"total": self.total, "done": self.done, "elapsed": round(el, 1),
                "finished": self.finished, "error": self.error, "rows": self.rows,
                "out_dir": self.out_dir, "unknown_symbols": sorted(self.unknown)[:50]}


def run_simulation(cfg, progress=None, on_update=None):
    cfg.validate()
    if len(cfg.players) != cfg.num_players:
        raise ValueError("Il faut %d profils (un par joueur), %d fournis" % (cfg.num_players, len(cfg.players)))
    names = unique_names(cfg.players)
    progress = progress or Progress(cfg.num_games)
    out_dir = cfg.output_dir
    os.makedirs(out_dir, exist_ok=True)
    progress.out_dir = os.path.abspath(out_dir)
    with open(os.path.join(out_dir, "config.json"), "w", encoding="utf-8") as f:
        f.write(cfg.to_json())
    writer = OutputWriter(out_dir, names, cfg.write_handhistory, cfg.write_player_files)
    stats = {i: PlayerStats(names[i]) for i in range(len(names))}
    from dataclasses import asdict
    cfg_dict = asdict(cfg)
    workers = cfg.workers or max(1, (os.cpu_count() or 1))
    workers = min(workers, cfg.num_games)
    jobs = [(cfg_dict, g + 1) for g in range(cfg.num_games)]

    def consume(res):
        writer.write_game(res)
        update_from_game(stats, res, cfg.buyin)
        progress.done += 1
        progress.unknown.update(res.get("unknown_symbols", []))
        if progress.done % 5 == 0 or progress.done == progress.total:
            progress.rows = [stats[i].row(cfg.buyin) for i in sorted(stats)]
            if on_update:
                on_update(progress)

    try:
        if workers <= 1:
            for j in jobs:
                if progress.cancel.is_set():
                    break
                consume(_play_one(j))
        else:
            ctx = mp.get_context("fork") if hasattr(os, "fork") else mp.get_context("spawn")
            with ctx.Pool(workers) as pool:
                for res in pool.imap(_play_one, jobs, chunksize=1):
                    consume(res)
                    if progress.cancel.is_set():
                        pool.terminate()
                        break
    except Exception as e:  # remonte l'erreur à l'UI
        progress.error = "%s: %s" % (type(e).__name__, e)
        raise
    finally:
        writer.close()
        progress.rows = [stats[i].row(cfg.buyin) for i in sorted(stats)]
        progress.finished = True
        write_summary(out_dir, cfg, progress.rows, progress)
        if on_update:
            on_update(progress)
    return progress


def write_summary(out_dir, cfg, rows, progress):
    with open(os.path.join(out_dir, "summary.json"), "w", encoding="utf-8") as f:
        json.dump({"games": progress.done, "seconds": round(time.time() - progress.started, 1),
                   "players": rows, "unknown_symbols": sorted(progress.unknown)}, f, indent=2, ensure_ascii=False)
    if rows:
        with open(os.path.join(out_dir, "player_stats.csv"), "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
