#!/usr/bin/env python3
"""Simulateur Spin & Go en ligne de commande.

Exemple :
  python3 run_cli.py --games 300 --level-seconds 90 --out output \
      --player Alice=profiles/tight.ohf --player Bob=profiles/loose.ohf --player Carl=DefaultBot
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from spingosim.config import SimConfig  # noqa: E402
from spingosim.runner import run_simulation, Progress  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--players", type=int, default=3, choices=(2, 3))
    ap.add_argument("--player", action="append", default=[], help="NOM=chemin.ohf (ou NOM=DefaultBot)")
    ap.add_argument("--level-seconds", type=int, default=90, choices=(60, 90, 120))
    ap.add_argument("--seed", type=int, default=12345)
    ap.add_argument("--out", default="output")
    ap.add_argument("--workers", type=int, default=0)
    ap.add_argument("--config", help="fichier JSON de configuration complet")
    ap.add_argument("--no-handhistory", action="store_true")
    a = ap.parse_args()
    if a.config:
        import json
        cfg = SimConfig.from_dict(json.load(open(a.config, encoding="utf-8")))
    else:
        cfg = SimConfig()
    cfg.num_games, cfg.num_players, cfg.seed = a.games, a.players, a.seed
    cfg.level_seconds, cfg.output_dir, cfg.workers = a.level_seconds, a.out, a.workers
    if a.no_handhistory:
        cfg.write_handhistory = False
    if a.player:
        cfg.players = []
        for p in a.player:
            name, _, path = p.partition("=")
            cfg.players.append({"name": name, "path": path or "DefaultBot"})
    while len(cfg.players) < cfg.num_players:
        cfg.players.append({"name": "Default%d" % (len(cfg.players) + 1), "path": "DefaultBot"})
    cfg.players = cfg.players[:cfg.num_players]
    pr = Progress(cfg.num_games)

    def show(p):
        print("\r%d/%d parties" % (p.done, p.total), end="", flush=True)
    run_simulation(cfg, pr, show)
    print()
    cols = ["name", "games", "1st", "2nd", "3rd", "itm%", "roi%", "net_eur", "vpip%", "pfr%", "3bet%", "AF", "WTSD%", "W$SD%"]
    print("  ".join("%-10s" % c for c in cols))
    for r in pr.rows:
        print("  ".join("%-10s" % r[c] for c in cols))
    print("Sorties dans", pr.out_dir)


if __name__ == "__main__":
    main()
