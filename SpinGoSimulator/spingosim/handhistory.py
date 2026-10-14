"""Écriture de l'historique de mains (style PokerStars) et des fichiers par joueur."""
import csv
import os
import re
import time

from . import cards as C

HT_TEXT = {"highcard": "high card", "pair": "a pair", "twopair": "two pair", "threeofakind": "three of a kind",
           "straight": "a straight", "flush": "a flush", "fullhouse": "a full house",
           "fourofakind": "four of a kind", "straightflush": "a straight flush"}
RN = {1: "preflop", 2: "flop", 3: "turn", 4: "river"}


def cs(cards):
    return " ".join(C.card_to_str(c) for c in cards)


def safe_name(name):
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", name).strip("_") or "player"


def format_hand(rec, game_label="", only_chair=None, base_time=1760400000):
    """Retourne le texte d'une main. Toutes les cartes des joueurs sont visibles."""
    seats = {s["chair"]: s for s in rec["seats"]}
    nm = lambda c: seats[c]["name"]
    ts = time.strftime("%Y/%m/%d %H:%M:%S", time.gmtime(base_time + int(rec["time"])))
    L = []
    hid = "%d%04d" % (rec["game"], rec["hand"])
    L.append("SpinSim Hand #%s: Hold'em No Limit (%d/%d) - %s UTC  [level %d, %s, clock %ds, x%g]" % (
        hid, rec["sb"], rec["bb"], ts, rec["level"], game_label, rec["time"], rec["multiplier"]))
    L.append("Table 'SpinGo %d' %d-max Seat #%d is the button" % (rec["game"], max(3, len(seats)), rec["dealer"] + 1))
    for ch in sorted(seats):
        L.append("Seat %d: %s (%d in chips)" % (ch + 1, seats[ch]["name"], seats[ch]["stack"]))
    acts = rec["actions"]
    pre = [a for a in acts if a["r"] == 1]
    for a in pre:
        if a["k"] == "sb":
            L.append("%s: posts small blind %d%s" % (nm(a["c"]), a["amt"], " and is all-in" if a["allin"] else ""))
        elif a["k"] == "bb":
            L.append("%s: posts big blind %d%s" % (nm(a["c"]), a["amt"], " and is all-in" if a["allin"] else ""))
    L.append("*** HOLE CARDS ***")
    for ch in sorted(seats):
        L.append("Dealt to %s [%s]" % (seats[ch]["name"], cs(seats[ch]["cards"])))
    board = rec["board_full"]
    cur = {}
    for rnd in (1, 2, 3, 4):
        ra = [a for a in acts if a["r"] == rnd and a["k"] not in ("sb", "bb")]
        if rnd > 1:
            if len(rec["board"]) < (2 + rnd) and not ra and not rec["showdown"]:
                break
            if rnd == 2:
                L.append("*** FLOP *** [%s]" % cs(board[:3]))
            elif rnd == 3:
                L.append("*** TURN *** [%s] [%s]" % (cs(board[:3]), cs(board[3:4])))
            else:
                L.append("*** RIVER *** [%s] [%s]" % (cs(board[:4]), cs(board[4:5])))
        high = 0
        if rnd == 1:
            high = max([a["to"] for a in acts if a["r"] == 1 and a["k"] in ("sb", "bb")] + [0])
        for a in ra:
            n = nm(a["c"])
            ai = " and is all-in" if a["allin"] else ""
            k = a["k"]
            if k == "fold":
                L.append("%s: folds" % n)
            elif k == "check":
                L.append("%s: checks" % n)
            elif k == "call":
                L.append("%s: calls %d%s" % (n, a["amt"], ai))
            elif k == "bet":
                L.append("%s: bets %d%s" % (n, a["amt"], ai))
                high = a["to"]
            elif k == "raise":
                L.append("%s: raises %d to %d%s" % (n, a["to"] - high, a["to"], ai))
                high = a["to"]
    # showdown / résultats
    for p in rec["pots"]:
        if p.get("uncalled"):
            L.append("Uncalled bet (%d) returned to %s" % (p["amount"], nm(p["winners"][0])))
    if rec["showdown"]:
        L.append("*** SHOW DOWN ***")
        for ch in sorted(seats):
            s = seats[ch]
            if not s["folded"]:
                L.append("%s: shows [%s] (%s)" % (s["name"], cs(s["cards"]),
                                                  HT_TEXT.get(rec["hand_types"].get(ch, ""), "")))
    for ch in sorted(seats):
        if seats[ch]["collected"] > 0:
            L.append("%s collected %d from pot" % (seats[ch]["name"], seats[ch]["collected"]))
    L.append("*** SUMMARY ***")
    total = sum(s["committed"] for s in seats.values())
    L.append("Total pot %d | Board [%s]" % (total, cs(rec["board"]) if rec["board"] else ""))
    for ch in sorted(seats):
        s = seats[ch]
        tag = ""
        if ch == rec["dealer"]:
            tag = " (button)"
        if ch == rec["sb_chair"]:
            tag = " (small blind)"
        if ch == rec["bb_chair"]:
            tag = " (big blind)"
        if s["folded"]:
            res = "folded"
        elif s["collected"] > 0:
            res = "won (%d)" % s["collected"]
        else:
            res = "lost"
        L.append("Seat %d: %s%s [%s] %s, net %+d" % (ch + 1, s["name"], tag, cs(s["cards"]), res, s["net"]))
    return "\n".join(L) + "\n\n"


class OutputWriter:
    """Ouvre les fichiers de sortie d'un run et y écrit au fil de l'eau."""

    def __init__(self, out_dir, names, write_hh=True, write_players=True):
        self.dir = out_dir
        os.makedirs(out_dir, exist_ok=True)
        self.write_hh = write_hh
        self.write_players = write_players
        self.hh = open(os.path.join(out_dir, "handhistory_global.txt"), "w", encoding="utf-8") if write_hh else None
        self.players = {}
        self.pdir = os.path.join(out_dir, "players")
        if write_players:
            os.makedirs(self.pdir, exist_ok=True)
        self.games = open(os.path.join(out_dir, "games.csv"), "w", newline="", encoding="utf-8")
        self.gw = csv.writer(self.games)
        self.gw.writerow(["game", "multiplier", "prizepool", "hands", "seconds", "player", "chair", "place", "gain", "net"])
        self.opened = {}

    def _player_files(self, name):
        if name in self.opened:
            return self.opened[name]
        base = os.path.join(self.pdir, safe_name(name))
        txt = open(base + "_hands.txt", "w", encoding="utf-8")
        cf = open(base + "_hands.csv", "w", newline="", encoding="utf-8")
        w = csv.writer(cf)
        w.writerow(["game", "hand", "level", "sb", "bb", "chair", "position", "hole_cards", "board",
                    "stack_start", "stack_end", "net", "actions_preflop", "actions_flop", "actions_turn",
                    "actions_river", "showdown", "hand_type"])
        self.opened[name] = (txt, cf, w)
        return self.opened[name]

    def write_game(self, res):
        for h in res["hand_records"]:
            text = None
            if self.hh:
                text = format_hand(h, "game %d" % res["game"])
                self.hh.write(text)
            if self.write_players:
                for s in h["seats"]:
                    txt, cf, w = self._player_files(s["name"])
                    txt.write(text or format_hand(h, "game %d" % res["game"]))
                    pos = self._pos(h, s["chair"])
                    by = {r: [] for r in (1, 2, 3, 4)}
                    for a in h["actions"]:
                        if a["c"] == s["chair"] and a["k"] not in ("sb", "bb"):
                            by[a["r"]].append(a["k"] + ("" if a["k"] in ("fold", "check") else ":%d" % a["amt"]))
                    w.writerow([h["game"], h["hand"], h["level"], h["sb"], h["bb"], s["chair"] + 1, pos,
                                C.cards_to_str(s["cards"]), C.cards_to_str(h["board"]), s["stack"], s["end"],
                                s["net"], " ".join(by[1]), " ".join(by[2]), " ".join(by[3]), " ".join(by[4]),
                                int(h["showdown"] and not s["folded"]),
                                h["hand_types"].get(s["chair"], "")])
        for p in res["players"]:
            self.gw.writerow([res["game"], res["multiplier"], res["prizepool"], res["hands"], int(res["seconds"]),
                              p["name"], p["chair"] + 1, p["place"], "%.2f" % p["gain"], "%.2f" % p["net"]])

    @staticmethod
    def _pos(h, chair):
        n = len(h["seats"])
        if chair == h["sb_chair"] and chair == h["dealer"]:
            return "BTN/SB"
        if chair == h["dealer"]:
            return "BTN"
        if chair == h["sb_chair"]:
            return "SB"
        if chair == h["bb_chair"]:
            return "BB"
        return "-"

    def close(self):
        if self.hh:
            self.hh.close()
        self.games.close()
        for txt, cf, _ in self.opened.values():
            txt.close()
            cf.close()
