"""Remplace PokerTracker : statistiques des adversaires calculées à partir des mains déjà jouées.

Portée : la partie en cours (Spin & Go : adversaires inconnus au départ, les stats se construisent
main après main). Les symboles ``pt_<stat><chair>`` (+ suffixes ``_raischair``, ``_userchair``…)
renvoient une fraction 0..1 (ou un facteur d'agression), ``-1`` s'il n'y a encore aucune main.
"""
from collections import defaultdict

STREET = {2: "f", 3: "t", 4: "r"}


def _new():
    return defaultdict(float)


class PTTracker:
    """Compteurs par NOM de joueur (comme le « screen name » dans PokerTracker).

    ``baseline`` : compteurs issus de parties précédentes (base persistante), copiés au départ.
    """

    def __init__(self, baseline=None):
        self.c = defaultdict(_new)
        for name, d in (baseline or {}).items():
            self.c[name].update(d)
        self.last_hand = None

    def to_dict(self):
        return {n: dict(d) for n, d in self.c.items()}

    def update(self, rec):
        key = (rec.get("game"), rec["hand"])
        if key == self.last_hand:
            return
        self.last_hand = key
        acts = rec["actions"]
        pre = [a for a in acts if a["r"] == 1 and a["k"] not in ("sb", "bb")]
        raises = [a for a in pre if a["k"] == "raise"]
        n = len(rec["seats"])
        board3 = len(rec["board"]) >= 3
        for s in rec["seats"]:
            ch = s["chair"]
            d = self.c[s["name"]]
            d["hands"] += 1
            mine = [a for a in pre if a["c"] == ch]
            if any(a["k"] in ("call", "raise") for a in mine):
                d["vpip"] += 1
            if any(a["k"] == "raise" for a in mine):
                d["pfr"] += 1
            seen, opp = False, False
            for a in pre:
                if a["c"] == ch and seen and not opp:
                    opp = True
                    d["3b_opp"] += 1
                    if a["k"] == "raise":
                        d["3b"] += 1
                if a["k"] == "raise":
                    seen = True
            # tentative de vol (bouton / SB, personne n'est entré avant)
            if n >= 3 and ch in (rec["dealer"], rec["sb_chair"]):
                before = []
                for a in pre:
                    if a["c"] == ch:
                        break
                    before.append(a)
                if not any(a["k"] in ("call", "raise") for a in before):
                    d["steal_opp"] += 1
                    if mine and mine[0]["k"] == "raise":
                        d["steal"] += 1
            # défense des blindes face à un vol
            if raises and n >= 3 and raises[0]["c"] in (rec["dealer"], rec["sb_chair"]) and ch in (rec["sb_chair"], rec["bb_chair"]) and raises[0]["c"] != ch:
                key = "bb" if ch == rec["bb_chair"] else "sb"
                after = [a for a in pre if a["c"] == ch and pre.index(a) > pre.index(raises[0])]
                if after:
                    d[key + "_steal_faced"] += 1
                    if after[0]["k"] == "fold":
                        d[key + "_steal_fold"] += 1
            folded_pre = any(a["k"] == "fold" and a["c"] == ch and a["r"] == 1 for a in acts)
            if board3 and not folded_pre:
                d["flop_seen"] += 1
                if rec["showdown"] and not s["folded"]:
                    d["sd"] += 1
                    if s["collected"] > 0:
                        d["sd_won"] += 1
            for r, k in STREET.items():
                post = [a for a in acts if a["r"] == r and a["c"] == ch]
                d["agg_" + k] += sum(1 for a in post if a["k"] in ("bet", "raise"))
                d["call_" + k] += sum(1 for a in post if a["k"] == "call")
            # c-bet et fold to c-bet
            last_r = raises[-1]["c"] if raises else None
            fl = [a for a in acts if a["r"] == 2]
            if fl:
                first_aggr = next((a for a in fl if a["k"] in ("bet", "raise")), None)
                if last_r == ch:
                    mf = [a for a in fl if a["c"] == ch]
                    if mf and (first_aggr is None or first_aggr is mf[0]):
                        d["cbet_opp"] += 1
                        if mf[0]["k"] in ("bet", "raise"):
                            d["cbet"] += 1
                elif first_aggr is not None and first_aggr["c"] == last_r and last_r is not None:
                    resp = [a for a in fl if a["c"] == ch and fl.index(a) > fl.index(first_aggr)]
                    if resp:
                        d["fcbet_faced"] += 1
                        if resp[0]["k"] == "fold":
                            d["fcbet_fold"] += 1

    # ------------------------------------------------------------------
    def stat(self, name, player):
        d = self.c.get(player)
        if not d or d["hands"] <= 0:
            return -1.0
        h = d["hands"]
        r = lambda a, b: (d[a] / d[b]) if d[b] else 0.0
        af = lambda k: (d["agg_" + k] / d["call_" + k]) if d["call_" + k] else float(d["agg_" + k])
        table = {
            "hands": h, "vpip": d["vpip"] / h, "pfr": d["pfr"] / h,
            "pfats": r("steal", "steal_opp"), "pf_cr": r("3b", "3b_opp"), "f3b": 0.0,
            "fbbts": r("bb_steal_fold", "bb_steal_faced"), "fsbts": r("sb_steal_fold", "sb_steal_faced"),
            "ffcbet": r("fcbet_fold", "fcbet_faced"), "cbet": r("cbet", "cbet_opp"),
            "wtsd": r("sd", "flop_seen"), "wsdp": r("sd_won", "sd"),
            "aggf": af("f"), "aggt": af("t"), "aggr": af("r"),
            "aggtot": (sum(d["agg_" + k] for k in "ftr") / sum(d["call_" + k] for k in "ftr")) if sum(d["call_" + k] for k in "ftr") else float(sum(d["agg_" + k] for k in "ftr")),
            "icon": 0.0, "iconlastr": 0.0,
        }
        table["aggp"] = d["pfr"] / max(1.0, d["vpip"] - d["pfr"])
        for k in list(table):
            pass
        return float(table[name]) if name in table else None
