#!/usr/bin/env python3
"""Gain / perte par ligne de profil, a partir des fichiers HH_Profile*.txt.

Usage:  python hh_profile_stats.py <fichier_ou_dossier> [...]  [--csv sortie.csv]

Pour chaque main, le resultat net du joueur (jetons recus - jetons mis) est
attribue a chaque ligne de profil utilisee dans la main (une fois par main).
Une main avec 2 lignes differentes compte donc dans les 2 lignes.
La colonne "derniere" attribue la main seulement a la derniere ligne utilisee.
"""
import sys, re, glob, os, csv
from collections import defaultdict

NUM = r"([0-9]+(?:\.[0-9]+)?)"
def num(s): return float(s)

def parse_hand(text):
    lines = [l.rstrip("\r") for l in text.split("\n")]
    hero = None
    for l in lines:
        m = re.match(r"Dealt to (.+?) \[", l)
        if m: hero = m.group(1); break
    if not hero: return None
    inv = 0.0; street_total = 0.0; got = 0.0
    notes = []
    for l in lines:
        if l.startswith("*** ") and l.split()[1] in ("FLOP", "TURN", "RIVER"):
            street_total = 0.0
        m = re.match(re.escape(hero) + r": posts the ante " + NUM, l)
        if m: inv += num(m.group(1)); continue
        m = re.match(re.escape(hero) + r": posts (?:small blind|big blind) " + NUM, l)
        if m: inv += num(m.group(1)); street_total += num(m.group(1)); continue
        m = re.match(re.escape(hero) + r": calls " + NUM, l)
        if m: inv += num(m.group(1)); street_total += num(m.group(1)); continue
        m = re.match(re.escape(hero) + r": bets " + NUM, l)
        if m: inv += num(m.group(1)); street_total += num(m.group(1)); continue
        m = re.match(re.escape(hero) + r": raises .* to " + NUM, l)
        if m:
            to = num(m.group(1)); inv += to - street_total; street_total = to; continue
        m = re.match(r"Uncalled bet \(" + NUM + r"\) returned to " + re.escape(hero), l)
        if m: got += num(m.group(1)); continue
        m = re.match(re.escape(hero) + r" collected " + NUM + " from", l)
        if m: got += num(m.group(1)); continue
        m = re.match(r"\((.+)\)$", l.strip())
        if m and l.startswith("("): notes.append(m.group(1))
    return got - inv, notes

def read_hands(path):
    text = open(path, encoding="utf-8", errors="replace").read()
    for block in re.split(r"\n(?=PokerStars Hand #)", text):
        if block.startswith("PokerStars Hand #") or "Hand #" in block[:40]:
            r = parse_hand(block)
            if r: yield r

def main(argv):
    csv_out = None
    if "--csv" in argv:
        i = argv.index("--csv"); csv_out = argv[i + 1]; del argv[i:i + 2]
    files = []
    for a in argv:
        if os.path.isdir(a):
            files += glob.glob(os.path.join(a, "**", "HH_Profile*.txt"), recursive=True)
        else:
            files.append(a)
    if not files:
        print(__doc__); return 1
    stat = defaultdict(lambda: [0, 0.0, 0, 0.0])   # mains, net, mains(derniere), net(derniere)
    total_hands = 0; total_net = 0.0; no_note = 0
    for f in sorted(files):
        for net, notes in read_hands(f):
            total_hands += 1; total_net += net
            if not notes: no_note += 1; continue
            for n in set(notes):
                stat[n][0] += 1; stat[n][1] += net
            stat[notes[-1]][2] += 1; stat[notes[-1]][3] += net
    rows = sorted(stat.items(), key=lambda kv: kv[1][1])
    print("%d mains, resultat total %+.2f (%d sans ligne de profil)\n" % (total_hands, total_net, no_note))
    print("%8s %10s %9s | %8s %10s | ligne" % ("mains", "net", "moy/main", "derniere", "net"))
    for n, (c, net, c2, net2) in rows:
        print("%8d %+10.2f %+9.2f | %8d %+10.2f | %s" % (c, net, net / c, c2, net2, n))
    if csv_out:
        with open(csv_out, "w", newline="", encoding="utf-8") as fh:
            w = csv.writer(fh, delimiter=";")
            w.writerow(["ligne", "mains", "net", "moyenne", "mains_derniere", "net_derniere"])
            for n, (c, net, c2, net2) in rows:
                w.writerow([n, c, "%.2f" % net, "%.2f" % (net / c), c2, "%.2f" % net2])
        print("\nCSV: " + csv_out)
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
