#!/usr/bin/env python3
"""Test différentiel des symboles natifs : SpinGoSimulator/symbols.py (Python) contre symdump (C++) sur des états de table aléatoires.
Usage : symdiff.py <symdump> [nb_scénarios=400] [graine=1]
Exclus : symboles aléatoires/historiques/PokerTracker (non déterministes ou dépendants de l'historique) et ceux que le C++ calcule
volontairement comme OpenHoldem (prwin, nhands*, handrank*, ...)."""
import json, os, random, re, subprocess, sys, tempfile, types
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "SpinGoSimulator"))
from spingosim import symbols as S, cards as C, engine as E, ohlang as L

CHARS = "23456789TJQKA"
def cstr(c): return CHARS[c >> 2] + "cdhs"[c & 3]

# Écarts CONNUS où le C++ suit OpenHoldem et le Python s'en écarte (vérifiés dans les sources d'OpenHoldem) :
#   nouts, ncardsbetter : OpenHoldem compare des pokerval signés ; une quinte flush vaut INT_MIN, donc n'est jamais comptée (CSymbolEngineCards.cpp).
KNOWN = {"nouts": "outs en quinte flush : pokerval signé (INT_MIN) chez OpenHoldem",
         "ncardsbetter": "cartes complétant une quinte flush au board : pokerval signé (INT_MIN) chez OpenHoldem"}
EXCLUDE = re.compile(r"^(random|prwin|prtie|prlos|nhands|handrank|ishandup|me_|user|vs\$|pt_|ptt_|hi_|dll\$|sitename\$|log\$|msgbox\$|mh_|elapsed(?!hand))")

def symbol_names():
    names = set(S._NATIVE_TABLE)
    for k in S._NATIVE_DIGIT:
        for d in range(10): names.add(k + str(d))
        for r in ("preflop", "flop", "turn", "river"): names.add("%s_%s" % (k, r))
    for base in ("didcallround", "didchecround", "didraisround", "didbetsizeround", "didswaground", "didfoldround", "didalliround",
                 "nplayersround", "nbetsround", "raisbits", "callbits", "foldbits"):
        names.add(base + "previousround"); names.add(base + "_previousround")
    for r in ("preflop", "flop", "turn", "river"): names.add("raisbits" + r); names.add("raisbits_" + r)
    roles = "bigblind smallblind dealer button cutoff ep1 ep2 ep3 mp1 mp2 mp3 utg lastraiser firstraiser bigstack smallstack headsup user rais firstcaller lastcaller".split()
    for b in ("balance", "currentbet", "stacksize", "startingstacksize"):
        for r in roles: names.add("%s_%schair" % (b, r))
    for p in ("player", "common"):
        for s in ("clubs", "diamonds", "hearts", "spades"): names.add("suitbits%s_%s" % (p, s))
    names |= {"$AK", "$AKs", "$AKo", "$AA", "$KK", "$Axs", "$Kx", "$QJo", "$22", "$T9s", "$xx", "$A", "$Ax",
              "hand$AKs", "hand$AA", "hand$AK", "hand$KQsuited", "hand$As", "hand$7c2d", "board$QJ", "board$AAA", "board$KsQs", "board$22suited",
              "$$pc0", "$$pc1", "$$pr0", "$$pr1", "$$ps0", "$$ps1", "$$cc0", "$$cc4", "$$cr0", "$$cr3", "$$cs0", "$$cs4", "$$cr5"}
    return sorted(n for n in names if not EXCLUDE.match(n))

def gen_scenario(rng):
    cash = rng.random() < 0.5
    n = rng.randint(2, 6)
    deck = list(range(52)); rng.shuffle(deck)
    nd = rng.randint(2, n)
    dealt_set = sorted(rng.sample(range(n), nd))
    dealer = rng.choice(dealt_set)
    order = [(dealer + i) % n for i in range(1, n + 1)]
    dealt_order = [c for c in order if c in dealt_set]
    if nd == 2: sb, bb = dealer, [c for c in dealt_order if c != dealer][0]
    else: sb, bb = dealt_order[0], dealt_order[1]
    br = rng.randint(1, 4)
    nb = {1: 0, 2: 3, 3: 4, 4: 5}[br]
    board = deck[:nb]; di = nb
    bblind = rng.choice([20, 40, 100, 200]); sblind = bblind // 2
    seats = []
    for c in range(n):
        d = c in dealt_set
        cards = [deck[di], deck[di + 1]] if d else []
        if d: di += 2
        seats.append({"chair": c, "dealt": d, "in_hand": d, "allin": False, "cards": cards, "stack": rng.randint(0, 3000),
                      "bet": 0, "committed": rng.randint(0, 800), "start_stack": rng.randint(200, 3500),
                      "acts": {r: [] for r in (1, 2, 3, 4)}, "prevaction": -2, "hands_played": rng.randint(0, 50)})
    actions = []
    actions.append({"r": 1, "c": sb, "k": "sb", "amt": sblind, "to": sblind})
    actions.append({"r": 1, "c": bb, "k": "bb", "amt": bblind, "to": bblind})
    for r in range(1, br + 1):
        cur = bblind if r == 1 else 0
        last_to = {}
        for _ in range(rng.randint(0, 6)):
            live = [s for s in seats if s["in_hand"]]
            if len(live) < 2: break
            s = rng.choice(live)
            k = rng.choice(["check", "call", "call", "bet", "raise", "fold"])
            if k in ("bet", "raise"):
                cur = cur + rng.randint(1, 4) * bblind
                to = cur; code = rng.choice([2, 3, 4])
                k = "bet" if cur == rng.randint(1, 4) * bblind and r > 1 and not any(a["r"] == r and a["k"] in ("bet", "raise") for a in actions) else k
            elif k == "call": to = cur; code = 1
            elif k == "check": to = last_to.get(s["chair"], 0); code = 0
            else: to = last_to.get(s["chair"], 0); code = -1
            last_to[s["chair"]] = to if k not in ("fold",) else last_to.get(s["chair"], 0)
            actions.append({"r": r, "c": s["chair"], "k": k, "amt": rng.randint(0, 600), "to": to})
            s["acts"][r].append(code); s["prevaction"] = code
            if k == "fold": s["in_hand"] = False
    cb = 0
    for s in seats:
        if s["in_hand"]:
            s["bet"] = rng.choice([0, 0, bblind, 2 * bblind, rng.randint(0, 600)]); cb = max(cb, s["bet"])
            if rng.random() < 0.08: s["allin"] = True; s["stack"] = 0
    chair = rng.choice([s["chair"] for s in seats if s["in_hand"]] or dealt_set)
    me = seats[chair]
    if not me["dealt"]: me["dealt"] = me["in_hand"] = True; me["cards"] = [deck[di], deck[di + 1]]
    return {"cash": cash, "nchairs": n, "dealer": dealer, "sb_chair": sb, "bb_chair": bb, "betround": br, "sblind": sblind, "bblind": bblind,
            "pot_prev": rng.randint(0, 2000), "clock": rng.uniform(0, 3000), "buyin": 10.0, "prizepool": 30.0, "multiplier": rng.choice([2.0, 5.0]),
            "current_bet": cb, "last_raise_size": bblind * rng.randint(1, 3), "board": [cstr(c) for c in board], "chair": chair,
            "level_seconds": rng.choice([60, 90, 120]), "starting_stack": rng.choice([500, 1000, 1500]), "hand_base": 10.0, "action_sec": 3.0,
            "payout": rng.choice([[0.75, 0.15, 0.10], [1.0], [0.65, 0.35]]),
            "actions": actions,
            "seats": [{**s, "cards": [cstr(x) for x in (s["cards"] + [None, None])[:2]] if s["cards"] else ["??", "??"], "acts": {str(k): v for k, v in s["acts"].items()}} for s in seats]}

def py_values(sc, names):
    t = E.Table(sc["nchairs"])
    t.dealer, t.sb_chair, t.bb_chair, t.betround = sc["dealer"], sc["sb_chair"], sc["bb_chair"], sc["betround"]
    t.sblind, t.bblind, t.pot_prev, t.clock = sc["sblind"], sc["bblind"], sc["pot_prev"], sc["clock"]
    t.buyin, t.prizepool, t.multiplier, t.current_bet, t.last_raise_size = sc["buyin"], sc["prizepool"], sc["multiplier"], sc["current_bet"], sc["last_raise_size"]
    t.starting_stack = sc["starting_stack"]
    t.board = [C.str_to_card(c) for c in sc["board"]]
    for s in sc["seats"]:
        st = E.Seat(s["chair"], "p%d" % s["chair"], None, s["stack"])
        st.bet, st.committed, st.start_stack, st.in_hand, st.allin, st.dealt = s["bet"], s["committed"], s["start_stack"], s["in_hand"], s["allin"], s["dealt"]
        st.cards = [C.str_to_card(c) for c in s["cards"] if c != "??"]
        st.acts = {int(k): list(v) for k, v in s["acts"].items()}
        st.prevaction, st.hands_played = s["prevaction"], s["hands_played"]
        t.seats.append(st)
    t.actions = [dict(a, code=0, allin=False) for a in sc["actions"]]
    cfg = types.SimpleNamespace(is_cash=sc["cash"], level_seconds=sc["level_seconds"], hand_base_seconds=sc["hand_base"], action_seconds=sc["action_sec"],
                                prwin_iterations=1000, prwin_max_iterations=20000, payout_split=sc["payout"], starting_stack=sc["starting_stack"])
    game = types.SimpleNamespace(t=t, cfg=cfg)
    ctx = S.SymbolContext(L.FormulaCollection(), cfg, random.Random(1), "x")
    ctx._bind(game, sc["chair"])
    out = []
    for nm in names:
        try:
            v = ctx._native(nm)
            out.append(None if v is None else float(v))
        except Exception as e:
            out.append("PYERR:%s" % type(e).__name__)
    return out

def main():
    exe = sys.argv[1]
    nscen = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    rng = random.Random(seed)
    names = symbol_names()
    scen = [gen_scenario(rng) for _ in range(nscen)]
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        json.dump({"scenarios": scen, "symbols": names}, f); path = f.name
    res = json.loads(subprocess.run([exe, path], capture_output=True, text=True, check=True).stdout)["results"]
    os.unlink(path)
    bad = {}; total = 0; pyerr = {}
    for sc, row in zip(scen, res):
        pv = py_values(sc, names)
        for nm, a, b in zip(names, pv, row):
            total += 1
            if isinstance(a, str):                       # le Python plante (symbole inexistant) : compté à part
                pyerr[nm] = pyerr.get(nm, 0) + 1
                continue
            ok = (a is None and b is None) or (isinstance(a, float) and isinstance(b, (int, float)) and abs(a - b) <= 1e-9 * max(1.0, abs(a)))
            if not ok: bad.setdefault(nm, []).append((a, b, sc))
    real = {k: v for k, v in bad.items() if k not in KNOWN}
    print("%d symboles x %d scénarios = %d comparaisons ; %d désaccord(s) inattendu(s), %d écart(s) connu(s), %d symbole(s) où le Python plante"
          % (len(names), nscen, total, len(real), len(bad) - len(real), len(pyerr)))
    for nm, lst in sorted(real.items(), key=lambda kv: -len(kv[1])):
        a, b, sc = lst[0]
        print("  INATTENDU %-30s %4d/%d  ex: python=%r c++=%r" % (nm, len(lst), nscen, a, b))
    for nm in sorted(set(bad) & set(KNOWN)):
        print("  connu     %-30s %4d/%d  %s" % (nm, len(bad[nm]), nscen, KNOWN[nm]))
    return 1 if real else 0

if __name__ == "__main__":
    sys.exit(main())
