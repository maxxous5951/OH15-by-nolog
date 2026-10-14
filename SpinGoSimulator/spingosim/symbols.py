"""Moteur de symboles OpenHoldem + cerveau (Brain) exécutant un profil .ohf.

Le contexte (:class:`SymbolContext`) expose à l'interpréteur (ohlang) :
``symbol(name)``, ``set_variable(name)``, ``has_function(name)``.
Les symboles natifs sont recalculés à partir de l'état exact de la table
(``engine.Table``) et du journal d'actions de la main (valeurs "vraies", pas
des estimations de scraping).
"""
import math
import sys
import os
import re
import random as _random

from . import cards as C
from .engine import (Brain, Decision, PREFLOP, FLOP, TURN, RIVER,
                     PA_UNDEFINED, PA_FOLD, PA_CHECK, PA_CALL, PA_RAISE, PA_BETSIZE, PA_ALLIN)
from .pt import PTTracker
from .handrank_data import HANDRANK_169, HANDRANK_2652
from .ohlang import FormulaCollection, MAIN_OPENPPL, default_function_for, parse_hand_list

# ----------------------------------------------------------------------------
#  Valeurs de main (pokerval, compatible OpenHoldem)
# ----------------------------------------------------------------------------
PV_ONEPAIR, PV_TWOPAIR, PV_TRIPS, PV_STRAIGHT = 0x01000000, 0x02000000, 0x04000000, 0x08000000
PV_FLUSH, PV_FULLHOUSE, PV_QUADS, PV_STFLUSH = 0x10000000, 0x20000000, 0x40000000, 0x80000000
PV_HICARD = 1
PV_ROYAL = 0x800EDCBA
HT = C  # alias


def hv(cards):
    """(type, rangs[5]) façon poker-eval, rangs 0..12 ; gère 1 à 7 cartes."""
    n = len(cards)
    if n >= 5:
        v = C.evaluate(cards)
        return C.hand_type(v), C.hand_ranks(v)
    counts = [0] * 13
    for c in cards:
        counts[c >> 2] += 1
    quads = [r for r in range(12, -1, -1) if counts[r] == 4]
    trips = [r for r in range(12, -1, -1) if counts[r] == 3]
    pairs = [r for r in range(12, -1, -1) if counts[r] == 2]
    singles = [r for r in range(12, -1, -1) if counts[r] == 1]
    if quads:
        return C.HT_FOUROFAKIND, ([quads[0]] + singles + [0] * 4)[:5]
    if trips:
        return C.HT_THREEOFAKIND, ([trips[0]] + pairs + singles + [0] * 4)[:5]
    if len(pairs) >= 2:
        return C.HT_TWOPAIR, (pairs[:2] + singles + [0] * 4)[:5]
    if pairs:
        return C.HT_PAIR, ([pairs[0]] + singles + [0] * 4)[:5]
    return C.HT_HIGHCARD, (singles + [0] * 5)[:5]


def calc_pokerval(cards, hole):
    """Port de CSymbolEnginePokerval::CalculatePokerval. Renvoie (pokerval, pcbits)."""
    n = len(cards)
    if n == 0:
        return 0, 0
    t, R = hv(cards)
    h0 = hole[0] >> 2 if len(hole) > 0 else -9
    h1 = hole[1] >> 2 if len(hole) > 1 else -9
    hs = [c & 3 for c in hole[:2]]
    flush_suit = -1
    if t in (C.HT_STRAIGHTFLUSH, C.HT_FLUSH):
        best = 0
        for s in (3, 2, 1, 0):
            k = sum(1 for c in cards if (c & 3) == s)
            if k > best:
                best, flush_suit = k, s

    def nib(vals):
        pv = 0
        for i, x in enumerate(vals):
            pv += (x + 2) << (16 - 4 * i)
        return pv

    def hole_has(rank, suit=None):
        r = False
        for i, rk in enumerate((h0, h1)):
            if rk == rank and (suit is None or (i < len(hs) and hs[i] == suit)):
                r = True
        return r

    bits = 0
    pv = 0
    if t == C.HT_STRAIGHTFLUSH or t == C.HT_STRAIGHT:
        pv = PV_STFLUSH if t == C.HT_STRAIGHTFLUSH else PV_STRAIGHT
        top = R[0]
        pv += nib([top - i for i in range(5)])
        suit = flush_suit if t == C.HT_STRAIGHTFLUSH else None
        for i in range(5):
            j, k = h0, h1
            if (hole_has(top - i, suit)):
                bits |= 1 << (4 - i)
            if j == 12 or k == 12:
                j = -1 if j == 12 else j
                k = -1 if k == 12 else k
                ok = False
                for idx, rk in enumerate((j, k)):
                    if rk == top - i and (suit is None or (idx < len(hs) and hs[idx] == suit)):
                        ok = True
                if ok:
                    bits |= 1 << (4 - i)
    elif t == C.HT_FOUROFAKIND:
        pv = PV_QUADS
        pv += ((R[0] + 2) << 16) + ((R[0] + 2) << 12) + ((R[0] + 2) << 8) + ((R[0] + 2) << 4)
        if n >= 5:
            pv += R[1] + 2
        a, b = h0 == R[0], h1 == R[0]
        if a != b:
            bits |= 1 << 4
        elif a and b:
            bits |= 0x18
        if n >= 5 and (h0 == R[1] or h1 == R[1]):
            bits |= 1
    elif t == C.HT_FULLHOUSE:
        pv = PV_FULLHOUSE
        pv += ((R[0] + 2) << 16) + ((R[0] + 2) << 12) + ((R[0] + 2) << 8) + ((R[1] + 2) << 4) + (R[1] + 2)
        a, b = h0 == R[0], h1 == R[0]
        if a != b:
            bits |= 1 << 4
        elif a and b:
            bits |= 0x18
        a, b = h0 == R[1], h1 == R[1]
        if a != b:
            bits |= 1 << 1
        elif a and b:
            bits |= 0x3
    elif t == C.HT_FLUSH:
        pv = PV_FLUSH + nib(R[:5])
        for i in range(5):
            if hole_has(R[i], flush_suit):
                bits |= 1 << (4 - i)
    elif t == C.HT_THREEOFAKIND:
        pv = PV_TRIPS + ((R[0] + 2) << 16) + ((R[0] + 2) << 12) + ((R[0] + 2) << 8)
        if n >= 4:
            pv += (R[1] + 2) << 4
        if n >= 5:
            pv += R[2] + 2
        a, b = h0 == R[0], h1 == R[0]
        if a != b:
            bits |= 1 << 4
        elif a and b:
            bits |= 0x18
        if n >= 4 and (h0 == R[1] or h1 == R[1]):
            bits |= 1 << 1
        if n >= 5 and (h0 == R[2] or h1 == R[2]):
            bits |= 1 << 1
    elif t == C.HT_TWOPAIR:
        pv = PV_TWOPAIR + ((R[0] + 2) << 16) + ((R[0] + 2) << 12) + ((R[1] + 2) << 8) + ((R[1] + 2) << 4)
        if n >= 5:
            pv += R[2] + 2
        a, b = h0 == R[0], h1 == R[0]
        if a != b:
            bits |= 1 << 4
        elif a and b:
            bits |= 0x18
        a, b = h0 == R[1], h1 == R[1]
        if a != b:
            bits |= 1 << 2
        elif a and b:
            bits |= 0x6
        if n >= 5 and (h0 == R[2] or h1 == R[2]):
            bits |= 1
    elif t == C.HT_PAIR:
        pv = PV_ONEPAIR + ((R[0] + 2) << 16) + ((R[0] + 2) << 12)
        if n >= 3:
            pv += (R[1] + 2) << 8
        if n >= 4:
            pv += (R[2] + 2) << 4
        if n >= 5:
            pv += R[3] + 2
        a, b = h0 == R[0], h1 == R[0]
        if a != b:
            bits |= 1 << 4
        elif a and b:
            bits |= 0x18
        if n >= 3 and (h0 == R[1] or h1 == R[1]):
            bits |= 1 << 2
        if n >= 4 and (h0 == R[2] or h1 == R[2]):
            bits |= 1 << 1
        if n >= 5 and (h0 == R[3] or h1 == R[3]):
            bits |= 1
    else:
        for i in range(min(n, 5)):
            pv += (R[i] + 2) << (16 - 4 * i)
        for i in range(min(n, 5)):
            if (h0 == R[i] or h1 == R[i]) and i < 3 or (i >= 3 and n >= 3 and (h0 == R[i] or h1 == R[i])):
                bits |= 1 << (4 - i)
    return pv, bits


def _rankbits(cards):
    b = 0
    for c in cards:
        r = c >> 2
        b |= 1 << (r + 2)
        if r == 12:
            b |= 2
    return b


def _bitcount(x):
    return bin(x).count("1")


def _straight_stats(strbits):
    """(nstraight, nstraightfill) comme CSymbolEngineCards (y compris ses particularités)."""
    ns, nf = 0, 5
    for i in range(10, 0, -1):
        w = (strbits >> i)
        if (w & 0x1f) == 0x1f:
            ns = max(ns, 5)
        elif (w & 0x1e) == 0x1e or (w & 0x0f) == 0x0f:
            ns = max(ns, 4)
        elif (w & 0x1c) == 0x1c or (w & 0x0e) == 0x0e or (w & 0x07) == 0x07:
            ns = max(ns, 3)
        elif (w & 0x18) == 0x18 or (w & 0x0c) == 0x0c or (w & 0x06) == 0x06 or (w & 3) == 3:
            ns = max(ns, 2)
        else:
            ns = max(ns, 1)
        nf = min(nf, 5 - _bitcount(w & 0x1f))
    return ns, nf


OH_SUIT = {0: 2, 1: 1, 2: 0, 3: 3}   # c,d,h,s  ->  clubs=2 diamonds=1 hearts=0 spades=3
PRIMES = [0, 0, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41]
RANK_CHARS = {"2": 2, "3": 3, "4": 4, "5": 5, "6": 6, "7": 7, "8": 8, "9": 9,
              "T": 10, "J": 11, "Q": 12, "K": 13, "A": 14}
MULTIPLE_OF_POT = {"allin": None, "bp21": 2.0, "bp11": 1.0, "bp34": 0.75, "bp23": 2.0 / 3,
                   "bp12": 0.5, "bp13": 1.0 / 3, "bp14": 0.25}
BACKUP_CHAIN = [("allin", "bp11"), ("bp11", "bp34"), ("bp34", "bp23"), ("bp23", "bp12"),
                ("bp12", "bp13"), ("bp13", "bp14"), ("bp14", "raise"), ("betsize", "raise"),
                ("raise", "call"), ("call", "check"), ("check", "fold")]
PRIORITY = ["allin", "bp21", "bp11", "bp34", "bp23", "bp12", "bp13", "bp14",
            "betsize", "raise", "call", "check", "fold"]
OH_ACTION_FUNCS = [("allin", "RaiseMax"), ("bp21", "RaiseTwoPot"), ("bp11", "RaisePot"),
                   ("bp34", "RaiseThreeFourthPot"), ("bp23", "RaiseTwoThirdPot"),
                   ("bp12", "RaiseHalfPot"), ("bp13", "RaiseThirdPot"), ("bp14", "RaiseFourthPot"),
                   ("raise", "Raise"), ("call", "Call"), ("check", "Check"), ("fold", "Fold"),
                   ("beep", "Beep")]


class FormulaError(Exception):
    pass


class SymbolContext:
    """Contexte d'évaluation pour un joueur (mémoire persistante pendant la partie)."""

    def __init__(self, fc, cfg, rng, name=""):
        self.fc = fc
        self.cfg = cfg
        self.rng = rng
        self.name = name
        self.fnlower = {}
        for k in fc.functions:
            self.fnlower.setdefault(k.lower(), k)
        self.memory = {}
        self.uservars = {}
        self.unknown = set()
        self.unknown_count = {}
        self.runtime_errors = {}
        self._cache = {}
        self._fn_cache = {}
        self._active = set()
        self.game = None
        self.chair = 0
        self.t = None
        self.me = None
        self._depth = 0
        self.hist_snap = {}      # {(symbole, tour): valeur}
        self.phandval = {}
        self.chandval = {}
        self._hand_rand = 0.0
        self._round_rand = {}
        self._beat_rand = 0.0
        self.fc_errors = 0

    # ------------------------------------------------------------------ cycle
    def new_game(self, game):
        self.game = game
        self.memory = {}
        self.uservars = {}
        self.hist_snap = {}
        self.max_opp_dummy = 0
        self._call_ini("f$ini_function_on_startup", game, None)
        self._call_ini("f$ini_function_on_connection", game, None)

    def _bind(self, game, chair):
        self.game = game
        self.t = game.t
        self.chair = chair
        self.me = game.t.seats[chair]
        self._cache = {}
        self._fn_cache = {}
        self._bits_cache = None
        self._hole = list(self.me.cards)
        self._board = list(game.t.board)
        self._prw = {}
        self._pv = None

    def _call_ini(self, fname, game, chair):
        if chair is None:
            chair = 0
            if game.t.seats:
                chair = 0
        self._bind(game, chair)
        self._eval_function(fname, silent=True)

    def new_hand(self, game, chair):
        self._bind(game, chair)
        self.uservars = {}
        self.hist_snap = {}
        self.phandval = {}
        self.chandval = {}
        self._hand_rand = self.rng.random()
        self._round_rand = {}
        self._rounds_seen = set()
        self._pending_after = False
        self._eval_function("UpdateMemorySymbolsOnHandReset", silent=True)
        self._eval_function("f$ini_function_on_handreset", silent=True)

    def prepare(self, game, chair):
        self._bind(game, chair)
        rnd = self.t.betround
        if getattr(self, "_pending_after", False):
            self._pending_after = False
            self._eval_function("UpdateMemorySymbolsAfterAutoplayerAction", silent=True)
            self._cache = {}
            self._fn_cache = {}
        if rnd not in self._rounds_seen:
            self._rounds_seen.add(rnd)
            self._round_rand[rnd] = self.rng.random()
            self._eval_function("UpdateMemorySymbolsOnNewRound", silent=True)
            self._eval_function("f$ini_function_on_new_round", silent=True)
            self._cache = {}
            self._fn_cache = {}
        self._beat_rand = self.rng.random()
        self._eval_function("f$ini_function_on_my_turn", silent=True)
        self._eval_function("f$ini_function_on_heartbeat", silent=True)
        self._fn_cache = {}
        self._cache = {}
        # historique des symboles hi_*
        self._snapshot_history()

    def _uses_hi_prwin(self):
        v = getattr(self.fc, "_uses_hi_prwin", None)
        if v is None:
            v = any(("hi_pr" in f.text) for f in self.fc.functions.values())
            self.fc._uses_hi_prwin = v
        return v

    def after_action(self):
        self._pending_after = True

    def _snapshot_history(self):
        r = self.t.betround
        names = ["balance"] + ["balance%d" % i for i in range(10)] + ["stack%d" % i for i in range(10)]
        for nm in names:
            self.hist_snap[(nm, r)] = self._native(nm) or 0.0
        if self._uses_hi_prwin():
            pw, pt, pl = self._prwin_triplet(self._prwin_opponents())
            self.hist_snap[("prwin", r)] = pw
            self.hist_snap[("prtie", r)] = pt
            self.hist_snap[("prlos", r)] = pl
        pvv = self._pokerval()[0]
        self.phandval[r] = pvv & 0xff000000
        self.chandval[r] = self._pokerval()[3] & 0xff000000

    # ------------------------------------------------------------------ interface ohlang
    def has_function(self, name):
        return name in self.fc.functions or name.lower() in self.fnlower

    def set_variable(self, name):
        self.uservars[name.lower()] = True
        return 0.0

    def symbol(self, name):
        fn = self.fc.functions.get(name)
        if fn is None:
            low = self.fnlower.get(name.lower())
            if low is not None:
                fn = self.fc.functions[low]
        if fn is not None:
            return self._call_function(fn)
        v = self._native(name)
        if v is None:
            self.unknown.add(name)
            self.unknown_count[name] = self.unknown_count.get(name, 0) + 1
            return 0.0
        return float(v)

    # ------------------------------------------------------------------ fonctions
    def _call_function(self, fn):
        key = fn.name
        if key in self._fn_cache:
            return self._fn_cache[key]
        if fn.is_list:
            return 0.0
        if not fn.parsed:
            self.fc.parse_one(fn)
        if fn.tree is None:
            self._fn_cache[key] = 0.0
            return 0.0
        if key in self._active or self._depth > 200:
            return 0.0
        self._active.add(key)
        self._depth += 1
        try:
            val = float(fn.tree.ev(self))
        except RecursionError:
            val = 0.0
        except (ZeroDivisionError, OverflowError, ValueError, TypeError, KeyError, IndexError):
            val = 0.0
            self.fc_errors += 1
            k = (fn.name, type(sys.exc_info()[1]).__name__)
            self.runtime_errors[k] = self.runtime_errors.get(k, 0) + 1
        finally:
            self._active.discard(key)
            self._depth -= 1
        self._fn_cache[key] = val
        return val

    def _eval_function(self, name, silent=False):
        fn = self.fc.functions.get(name)
        if fn is None or not fn.text.strip():
            return 0.0
        return self._call_function(fn)

    def evaluate_named(self, name):
        fn = self.fc.functions.get(name)
        if fn is None:
            return None
        return self._call_function(fn)

    # ------------------------------------------------------------------ état dérivé
    def _chairs(self):
        return range(self.t.nchairs)

    def _bit(self, c):
        return 1 << c

    def _playing(self):
        return [s.chair for s in self.t.seats if s.in_hand]

    def _dealt(self):
        return [s.chair for s in self.t.seats if s.dealt]

    def _bits(self, chairs):
        b = 0
        for c in chairs:
            b |= 1 << c
        return b

    def _unit(self):
        return float(self.t.bblind) if self.t.bblind > 0 else 1.0

    def _largest_bet(self):
        return float(self.t.largest_bet())

    def _call_amount(self):
        me = self.me
        call = self._largest_bet() - me.bet
        if call > me.stack and me.stack > 0:
            call = me.stack
        return max(call, 0.0)

    def _round_actions(self, rnd=None):
        rnd = rnd or self.t.betround
        return [a for a in self.t.actions if a["r"] == rnd and a["k"] not in ("sb", "bb")]

    def _aggr(self, rnd=None):
        return [a for a in self._round_actions(rnd) if a["k"] in ("bet", "raise")]

    def _dealpos(self, chair):
        n = 0
        d = self.t.dealer
        for i in range(1, self.t.nchairs + 1):
            c = (d + i) % self.t.nchairs
            if self.t.seats[c].dealt:
                n += 1
            if c == chair:
                return n
        return 0

    def _betpos(self, chair):
        n = 0
        d = self.t.dealer
        for i in range(1, self.t.nchairs + 1):
            c = (d + i) % self.t.nchairs
            if self.t.seats[c].in_hand:
                n += 1
            if c == chair:
                return n
        return 0

    def _chair_by_dealpos(self, p):
        for c in self._chairs():
            if self.t.seats[c].dealt and self._dealpos(c) == p:
                return c
        return -1

    def _logical_chair(self, offset):
        n = len(self._dealt())
        pos = n - offset
        if pos <= 2:
            return -1
        return self._chair_by_dealpos(pos)

    # ------------------------------------------------------------------ cartes
    def _pokerval(self):
        if self._pv is None:
            allc = self._hole + self._board
            pv, bits = calc_pokerval(allc, self._hole)
            pvp, _ = calc_pokerval(self._hole, [])
            pvc, _ = calc_pokerval(self._board, [])
            self._pv = (pv, bits, pvp, pvc)
        return self._pv

    def _hand_type_info(self):
        allc = self._hole + self._board
        if not allc:
            return 0, [0] * 5
        return hv(allc)

    def _rankbits_info(self):
        pl = _rankbits(self._hole)
        cm = _rankbits(self._board)
        return pl, cm

    def _tsuit_info(self, cards):
        cnt = {s: 0 for s in range(4)}
        for c in cards:
            cnt[c & 3] += 1
        # ordre OH : clubs, diamonds, hearts, spades (>=)
        n_c, n_d, n_h, n_s = cnt[0], cnt[1], cnt[2], cnt[3]
        if n_c >= n_d and n_c >= n_h and n_c >= n_s:
            return OH_SUIT[0], n_c, 0
        if n_d >= n_h and n_d >= n_s:
            return OH_SUIT[1], n_d, 1
        if n_h >= n_s:
            return OH_SUIT[2], n_h, 2
        return OH_SUIT[3], n_s, 3

    def _srankbits(self, cards, suit_idx):
        b = 0
        for c in cards:
            if (c & 3) == suit_idx:
                r = c >> 2
                b |= 1 << (r + 2)
                if r == 12:
                    b |= 2
        return b

    def _card_stats(self):
        key = "_cs"
        if key in self._cache:
            return self._cache[key]
        pl = self._hole + self._board
        cm = self._board
        d = {}
        d["tsuit"], d["nsuited"], tsi = self._tsuit_info(pl)
        d["tsuitcommon"], d["nsuitedcommon"], tsci = self._tsuit_info(cm)
        for nm, cards in (("", pl), ("common", cm)):
            cnt = [0] * 13
            for c in cards:
                cnt[c >> 2] += 1
            mx, tr = 0, 0
            for i in range(12, -1, -1):
                if cnt[i] > mx and cnt[i] > 0:
                    mx, tr = cnt[i], i + 2
            d["nranked" + nm], d["trank" + nm] = mx, tr
        d["nstraight"], d["nstraightfill"] = _straight_stats(_rankbits(pl))
        if self._board:
            d["nstraightcommon"], d["nstraightfillcommon"] = _straight_stats(_rankbits(cm))
        else:
            d["nstraightcommon"], d["nstraightfillcommon"] = 0, 5
        nsf, nff, nsfc, nffc = 0, 5, 0, 5
        for s in range(4):
            a, b = _straight_stats(_rankbits([c for c in pl if (c & 3) == s]))
            nsf = max(nsf, a)
            nff = min(nff, b)
            a, b = _straight_stats(_rankbits([c for c in cm if (c & 3) == s]))
            nsfc = max(nsfc, a)
            nffc = min(nffc, b)
        d["nstraightflush"], d["nstraightflushfill"] = nsf, nff
        d["nstraightflushcommon"], d["nstraightflushfillcommon"] = nsfc, nffc
        self._cache[key] = d
        return d

    def _nouts_betterc(self):
        key = "_nouts"
        if key in self._cache:
            return self._cache[key]
        known = set(self._hole) | set(self._board)
        base_pv = self._pokerval()[0]
        base_t = hv(self._hole + self._board)[0] if (self._hole + self._board) else 0
        nouts = better = 0
        for c in range(52):
            if c in known:
                continue
            allp = self._hole + self._board + [c]
            cm1 = self._board + [c]
            t1, _ = hv(allp)
            pv1, _ = calc_pokerval(allp, [])
            tc1, _ = hv(cm1)
            pvc1, _ = calc_pokerval(cm1, [])
            if self.t.betround < RIVER and t1 > base_t and pv1 > base_pv and t1 > tc1:
                nouts += 1
            if pvc1 > base_pv:
                better += 1
        self._cache[key] = (nouts, better)
        return nouts, better

    def _is_hand(self, name):
        s = name[1:]
        ranks, suited = [], 0
        for ch in s:
            u = ch.upper()
            if u in RANK_CHARS:
                ranks.append(RANK_CHARS[u])
            elif u == "X":
                ranks.append(-1)
            elif u == "S":
                suited = 1
            elif u == "O":
                suited = 2
            else:
                return 0.0
        while len(ranks) < 2:
            ranks.append(0)
        if len(self._hole) < 2:
            return 0.0
        if ranks[1] > ranks[0]:
            ranks[0], ranks[1] = ranks[1], ranks[0]
        pr = sorted([(c >> 2) + 2 for c in self._hole[:2]], reverse=True)
        plsuited = (self._hole[0] & 3) == (self._hole[1] & 3)
        if suited == 1 and not plsuited:
            return 0.0
        if suited == 2 and plsuited:
            return 0.0
        if ranks[0] == -1 and ranks[1] == -1:
            return 1.0
        if ranks[1] in (0, -1):
            return 1.0 if ranks[0] in pr else 0.0
        if ranks[0] != -1 and ranks[0] != pr[0]:
            return 0.0
        if ranks[1] != -1 and ranks[1] != pr[1]:
            return 0.0
        return 1.0

    def _hand_board_expr(self, name):
        if name.startswith("hand$"):
            expr, cards, is_hand = name[5:], self._hole, True
        else:
            expr, cards, is_hand = name[6:], self._board, False
        suited = False
        if expr.lower().endswith("suited"):
            suited = True
            expr = expr[:-6]
        avail = 1
        for c in cards:
            avail *= PRIMES[(c >> 2) + 2]
        want = 1
        ranks_wanted = []
        for ch in expr:
            u = ch.upper()
            if u in RANK_CHARS:
                want *= PRIMES[RANK_CHARS[u]]
                ranks_wanted.append(RANK_CHARS[u])
        if want == 0 or avail % want != 0:
            return 0.0
        if suited:
            if is_hand:
                if len(self._hole) < 2 or (self._hole[0] & 3) != (self._hole[1] & 3):
                    return 0.0
            else:
                _, _, tsci = self._tsuit_info(self._board)
                have = self._srankbits(self._board, tsci)
                need = 0
                for r in ranks_wanted:
                    need |= 1 << r
                    if r == 14:
                        need |= 2
                if need & have != need:
                    return 0.0
        for i in range(len(expr) - 1):
            if expr[i].upper() in RANK_CHARS and expr[i + 1].lower() in "cdhs":
                try:
                    cc = C.str_to_card(expr[i] + expr[i + 1])
                except ValueError:
                    continue
                if cc not in cards:
                    return 0.0
        return 1.0

    # ------------------------------------------------------------------ prwin
    def _prwin_opponents(self):
        v = self.evaluate_named("f$prwin_number_of_opponents")
        if v is None or v < 1:
            v = max(1, len(self._playing()) - 1)
        return int(min(v, 9))

    def _prwin_iterations(self):
        v = self.evaluate_named("f$prwin_number_of_iterations")
        if not v or v <= 0:
            v = self.cfg.prwin_iterations
        return int(max(20, min(v, self.cfg.prwin_max_iterations)))

    def _prwin_triplet(self, nopp, iterations=None):
        key = (nopp, len(self._board))
        if key in self._prw:
            return self._prw[key]
        if len(self._hole) < 2:
            res = (0.0, 0.0, 0.0)
        else:
            if not self._board:
                ck = (C.hand_class_169(self._hole[0], self._hole[1]), nopp)
                res = _PREFLOP_EQ.get(ck)
                if res is None:
                    rr = _random.Random(hash(ck) & 0xffff)
                    res = C.equity_vs_random(self._hole, [], nopp, 1500, rr)
                    _PREFLOP_EQ[ck] = res
            else:
                res = C.equity_vs_random(self._hole, self._board, nopp,
                                         iterations or self._prwin_iterations(), self.rng)
        self._prw[key] = res
        return res

    def _vs_list(self, listname, what):
        fn = self.fc.functions.get("list" + listname) or self.fc.functions.get(listname)
        if fn is None:
            return 0.0
        if fn.hand_list is None:
            self.fc.parse_one(fn)
        hands = fn.hand_list or set()
        if not hands or len(self._hole) < 2:
            return 0.0
        known = set(self._hole) | set(self._board)
        rest = [c for c in range(52) if c not in known]
        combos = []
        for i in range(len(rest)):
            for j in range(i + 1, len(rest)):
                if C.hand_class_169(rest[i], rest[j]) in hands:
                    combos.append((rest[i], rest[j]))
        if what == "nhands":
            return float(len(combos))
        if not combos:
            return 0.0
        iters = min(self._prwin_iterations(), 300)
        w = t = l = 0
        for _ in range(iters):
            o = self.rng.choice(combos)
            rem = [c for c in rest if c not in o]
            need = 5 - len(self._board)
            full = self._board + self.rng.sample(rem, need)
            a = C.evaluate(self._hole + full)
            b = C.evaluate(list(o) + full)
            if a > b:
                w += 1
            elif a == b:
                t += 1
            else:
                l += 1
        return {"prwin": w, "prtie": t, "prlos": l}.get(what, 0) / float(iters)

    def _nhands(self):
        if "_nh" in self._cache:
            return self._cache["_nh"]
        known = set(self._hole) | set(self._board)
        rest = [c for c in range(52) if c not in known]
        total = hi = ti = lo = 0
        if len(self._board) >= 3 and len(self._hole) == 2:
            mine = C.evaluate(self._hole + self._board)
            for i in range(len(rest)):
                for j in range(i + 1, len(rest)):
                    total += 1
                    o = C.evaluate([rest[i], rest[j]] + self._board)
                    if o > mine:
                        hi += 1
                    elif o == mine:
                        ti += 1
                    else:
                        lo += 1
        else:
            total = len(rest) * (len(rest) - 1) // 2
        r = (total, hi, ti, lo)
        self._cache["_nh"] = r
        return r

    # ------------------------------------------------------------------ mémoire
    def _memory(self, name):
        low = name
        parts = low.split("_")
        op = parts[1]
        key = lambda p: p.lower()
        def rhs_text():
            idx = low.find("_", low.find("_", low.find("_") + 1) + 1)
            return low[idx + 1:] if idx >= 0 else ""
        def rhs():
            txt = rhs_text()
            if not txt:
                return 0.0
            if txt[0].isdigit():
                try:
                    return float(txt.replace("_", "."))
                except ValueError:
                    return 0.0
            return self.symbol(txt)
        if len(parts) < 3:
            return 0.0
        var = parts[2].lower()
        if op == "st":
            self.memory[var] = rhs()
            return 0.0
        if op == "re":
            return self.memory.get(var, 0.0)
        if op == "inc":
            self.memory[var] = self.memory.get(var, 0.0) + 1
            return 0.0
        if op == "dec":
            self.memory[var] = self.memory.get(var, 0.0) - 1
            return 0.0
        if op == "add":
            self.memory[var] = self.memory.get(var, 0.0) + rhs()
            return 0.0
        if op == "sub":
            self.memory[var] = self.memory.get(var, 0.0) - rhs()
            return 0.0
        return None

    # ------------------------------------------------------------------ natifs
    _RE_DIGIT_SUFFIX = re.compile(r"^(.*?)(\d)$")

    def _native(self, name):
        low = name.lower()
        t = self.t
        # --- user variables
        if low.startswith("user") and not low.startswith("userchair"):
            return 1.0 if self.uservars.get(low) else 0.0
        if low.startswith("me_"):
            return self._memory(name)
        if name.startswith("hand$") or name.startswith("board$"):
            return self._hand_board_expr(name)
        if name.startswith("$$"):
            return self._card_code(name)
        if name.startswith("$"):
            return self._is_hand(name)
        if low.startswith("islist"):
            return self._is_list(name[6:])
        if low.startswith("vs$"):
            return self._vs_symbol(name)
        if low.startswith("hi_"):
            m = re.match(r"^hi_(.*?)(\d)$", low)
            if m:
                return self.hist_snap.get((m.group(1), int(m.group(2))), 0.0)
            return 0.0
        if low.startswith(("pt_", "ptt_")):
            return self._pt_symbol(low)
        if low.startswith(("dll$", "sitename$", "log$", "msgbox$", "mh_")):
            return 0.0
        fn = _NATIVE_TABLE.get(low)
        if fn is not None:
            return fn(self)
        m = re.match(r"^([a-z_]+?)(\d)$", low)
        if m:
            f2 = _NATIVE_DIGIT.get(m.group(1))
            if f2 is not None:
                return f2(self, int(m.group(2)))
        for pat, f3 in _NATIVE_REGEX:
            mm = pat.match(low)
            if mm:
                return f3(self, mm)
        return None

    _PT_ROLES = {"raischair": lambda c: c.raischair(), "headsupchair": lambda c: int(_headsup_chair(c)),
                 "smallblindchair": lambda c: c.t.sb_chair, "bigblindchair": lambda c: c.t.bb_chair,
                 "dealerchair": lambda c: c.t.dealer, "cutoffchair": lambda c: c._logical_chair(1),
                 "userchair": lambda c: c.chair, "firstraiserchair": lambda c: int(_first_raiser(c)),
                 "firstcallerchair": lambda c: int(_first_caller(c)), "lastcallerchair": lambda c: int(_last_caller(c))}

    def _pt_symbol(self, low):
        """Symboles PokerTracker simulés par les stats des mains déjà jouées dans la partie."""
        m = re.match(r"^ptt?_(?:r_)?([a-z_0-9]+?)(\d|_(?:%s))$" % "|".join(self._PT_ROLES), low)
        if not m:
            return -1.0
        if low.startswith("pt_r_"):
            return -1.0
        stat, suf = m.group(1), m.group(2)
        if suf.isdigit():
            chair = int(suf)
        else:
            chair = self._PT_ROLES[suf[1:]](self)
        if chair is None or chair < 0 or chair >= self.t.nchairs:
            return -1.0
        tr = getattr(self.game, "pt", None)
        if tr is None:
            return -1.0
        v = tr.stat(stat, self.t.seats[chair].name)
        return -1.0 if v is None else v

    def _card_code(self, name):
        m = re.match(r"^\$\$(p|c)(c|r|s)(\d)$", name)
        if not m:
            return None
        cards = self._hole if m.group(1) == "p" else self._board
        i = int(m.group(3))
        if i >= len(cards):
            return -1.0 if m.group(2) != "r" else 0.0
        c = cards[i]
        if m.group(2) == "r":
            return float((c >> 2) + 2)
        if m.group(2) == "s":
            return float(OH_SUIT[c & 3])
        return float(OH_SUIT[c & 3] * 13 + (c >> 2))

    def _is_list(self, num):
        fn = self.fc.functions.get("list" + num)
        if fn is None:
            return 0.0
        if fn.hand_list is None:
            self.fc.parse_one(fn)
        if len(self._hole) < 2 or not fn.hand_list:
            return 0.0
        return 1.0 if C.hand_class_169(self._hole[0], self._hole[1]) in fn.hand_list else 0.0

    def _vs_symbol(self, name):
        m = re.match(r"^vs\$([^$]+)\$(prwin|prtie|prlos|nhands)$", name)
        if m:
            return self._vs_list(m.group(1), m.group(2))
        return 0.0

    # ----- helpers de symboles (fonctions utilitaires) ----
    def s_call(self):
        return self._call_amount()

    def raisbits(self, rnd):
        b = 0
        for a in self._aggr(rnd):
            b |= 1 << a["c"]
        return b

    def callbits(self, rnd):
        b = 0
        for a in self._round_actions(rnd):
            if a["k"] == "call":
                b |= 1 << a["c"]
        return b

    def foldbits(self, rnd):
        b = 0
        for a in self._round_actions(rnd):
            if a["k"] == "fold":
                b |= 1 << a["c"]
        return b

    def raischair(self):
        acts = [a for a in self._aggr() if a["c"] != self.chair]
        return acts[-1]["c"] if acts else -1

    def n_truly_raising(self):
        acts = self._round_actions()
        last_mine = -1
        for i, a in enumerate(acts):
            if a["c"] == self.chair:
                last_mine = i
        return sum(1 for a in acts[last_mine + 1:] if a["k"] in ("bet", "raise") and a["c"] != self.chair)

    def n_calling(self):
        acts = self._round_actions()
        last_aggr = -1
        for i, a in enumerate(acts):
            if a["k"] in ("bet", "raise"):
                last_aggr = i
        return len({a["c"] for a in acts[last_aggr + 1:] if a["k"] == "call" and a["c"] != self.chair})

    def did(self, kind, rnd):
        acts = self.me.acts.get(rnd, [])
        code = {"chec": PA_CHECK, "call": PA_CALL, "rais": PA_RAISE, "swag": PA_BETSIZE,
                "fold": PA_FOLD, "alli": PA_ALLIN}[kind]
        return 1.0 if code in acts else 0.0

    def nplayersround(self, rnd):
        if rnd > self.t.betround:
            return 0.0
        n = len(self._dealt())
        for r in range(1, rnd):
            n -= sum(1 for a in self.t.actions if a["r"] == r and a["k"] == "fold")
        return float(n)

    def nbetsround(self, rnd):
        if rnd > self.t.betround:
            return 0.0
        mx = 0
        for a in self.t.actions:
            if a["r"] == rnd:
                mx = max(mx, a["to"])
        return mx / self._unit()

    def blind_bits(self):
        t = self.t
        chairs = [c for c in (t.sb_chair, t.bb_chair) if c >= 0]
        return self._bits(chairs)

    def balance_sorted(self, rank):
        vals = sorted([(s.bet + s.stack) for s in self.t.seats], reverse=True)
        vals += [0] * 10
        return float(vals[rank])

    def stack_sorted(self, rank):
        vals = sorted([s.stack if s.in_hand or s.dealt else 0 for s in self.t.seats], reverse=True)
        vals += [0] * 10
        return float(vals[rank])

    def alive_stacks(self):
        return sorted([s.stack + s.bet for s in self.t.seats if s.stack + s.bet > 0], reverse=True)


# ----------------------------------------------------------------------------
#  Tables de dispatch des symboles natifs
# ----------------------------------------------------------------------------
def _fl(x):
    return float(x)


def _cs(key):
    return lambda c: _fl(c._card_stats()[key])


def _pvfn(idx):
    return lambda c: _fl(c._pokerval()[idx])


def _type_is(code):
    return lambda c: _fl(c._hand_type_info()[0] == code and bool(c._hole + c._board))


def _rank_hi(bits_fn):
    def f(c):
        b = bits_fn(c)
        for i in range(14, 1, -1):
            if b >> i & 1:
                return _fl(i)
        return 0.0
    return f


def _rank_lo(bits_fn):
    def f(c):
        b = bits_fn(c)
        for i in range(2, 15):
            if b >> i & 1:
                return _fl(i)
        return 0.0
    return f


def _pokerbits(c):
    pv = c._pokerval()[0]
    b = 0
    for sh in (16, 12, 8, 4, 0):
        b |= 1 << ((pv >> sh) & 0xf)
    if b & 0x4000:
        b |= 2
    return b


def _spokerbits(c):
    pv = c._pokerval()[0]
    _, _, tsi = c._tsuit_info(c._hole + c._board)
    allc = set(c._hole + c._board)
    b = 0
    for sh in (16, 12, 8, 4, 0):
        r = (pv >> sh) & 0xf
        if r >= 2 and ((r - 2) * 4 + tsi) in allc:
            b |= 1 << r
    if b & 0x4000:
        b |= 2
    return b


def _srank_player(c):
    _, _, tsi = c._tsuit_info(c._hole + c._board)
    return c._srankbits(c._hole, tsi)


def _srank_common(c):
    _, _, tsci = c._tsuit_info(c._board)
    return c._srankbits(c._board, tsci)


def _srank_all(c):
    _, _, tsi = c._tsuit_info(c._hole + c._board)
    return c._srankbits(c._hole, tsi) | c._srankbits(c._board, tsi)


def _rb_player(c):
    return c._rankbits_info()[0]


def _rb_common(c):
    return c._rankbits_info()[1]


def _rb_all(c):
    a, b = c._rankbits_info()
    return a | b


def _higher_straight_possible(c, top):
    if top == 12:
        return False
    rb = _rankbits(c._board)
    shift = 10
    for k in range(14, top + 2, -1):
        if _bitcount((rb >> shift) & 0x1f) >= 3:
            return True
        shift -= 1
    return False


def _pair_class(c, which):
    t, R = c._hand_type_info()
    allc = c._hole + c._board
    if t != C.HT_PAIR or len(allc) < 2:
        return 0.0
    n = len(allc)
    rbc = _rankbits(c._board)
    hi_c = _rank_hi(lambda _: rbc)(c)
    lo_c = _rank_lo(lambda _: rbc)(c)
    top = R[0]
    if n == 2:
        return 1.0 if which == "hi" else 0.0
    if n >= 5:
        if top >= hi_c - 2:
            return 1.0 if which == "hi" else 0.0
        if lo_c - 2 < top < hi_c - 2:
            return 1.0 if which == "mid" else 0.0
        return 1.0 if which == "lo" else 0.0
    return 0.0


def _is_hi_straight(c):
    t, R = c._hand_type_info()
    return _fl(t == C.HT_STRAIGHT and not _higher_straight_possible(c, R[0]))


def _is_hi_flush(c):
    t, R = c._hand_type_info()
    if t != C.HT_FLUSH:
        return 0.0
    bits = c._pokerval()[1]
    for i, need in enumerate((12, 11, 10, 9)):
        if R[i] == need and bits & (1 << (4 - i)):
            return 1.0
    return 0.0


def _is_royal(c):
    t, R = c._hand_type_info()
    return _fl(t == C.HT_STRAIGHTFLUSH and R[0] == 12)


def _is_handup(c):
    r = c.t.betround
    if r <= 1:
        return 0.0
    cur = c._pokerval()[0] & 0xff000000
    prev = c.phandval.get(r - 1, 0)
    return _fl(cur > prev)


def _is_handup_common(c):
    r = c.t.betround
    if r <= 1:
        return 0.0
    cur = c._pokerval()[3] & 0xff000000
    prev = c.chandval.get(r - 1, 0)
    return _fl(cur > prev)


def _handrank_idx(c):
    if len(c._hole) < 2:
        return 0, 0, 1
    n = max(1, min(9, c._prwin_opponents()))
    cls = C.hand_class_169(c._hole[0], c._hole[1])
    row = HANDRANK_169[n - 1]
    try:
        i = row.index(cls)
    except ValueError:
        return 0, 0, n
    return i + 1, HANDRANK_2652[n - 1][i], n


def _hr169(c):
    return _fl(_handrank_idx(c)[0])


def _hr2652(c):
    return _fl(_handrank_idx(c)[1])


def _hr1326(c):
    return _fl(_handrank_idx(c)[1] // 2)


def _hr1000(c):
    return _fl(1000 * _handrank_idx(c)[1] // 2652)


def _hrp(c):
    return 2652.0 / (1.0 + _handrank_idx(c)[2])


def _prw(idx):
    return lambda c: c._prwin_triplet(c._prwin_opponents())[idx]


def _prw_now(idx):
    return lambda c: c._prwin_triplet(max(1, len(c._playing()) - 1))[idx]


def _ncomm(c):
    return _fl(len(c._board))


def _ntotal_known(c):
    return _fl(len(c._hole) + len(c._board))


def _pot(c):
    return _fl(c.t.pot())


def _potplayer(c):
    return _fl(sum(s.bet for s in c.t.seats))


def _potcommon(c):
    return _fl(c.t.pot_prev)


def _bet(c):
    return c._unit()


def _ncallbets(c):
    return c._largest_bet() / c._unit()


def _nbetstocall(c):
    return c._call_amount() / c._unit()


def _randomfn(c):
    return c.rng.random()


def _n_playing(c):
    return len(c._playing())


def _n_dealt(c):
    return len(c._dealt())


def _n_allin(c):
    return sum(1 for s in c.t.seats if s.in_hand and s.allin)


def _me_in(lst, c):
    return 1 if c.chair in lst else 0


def _blind_players(c):
    return [x for x in (c.t.sb_chair, c.t.bb_chair) if x >= 0]


def _missing_sb(c):
    return 0.0


def _mtt_rank(c):
    st = c.alive_stacks()
    mine = c.me.stack + c.me.bet
    return _fl(1 + sum(1 for x in st if x > mine))


def _lim(c):
    return 0.0


def _smallstack_chair(c):
    best, res = 10 ** 12, -1
    for s in c.t.seats:
        if s.chair == c.chair or not s.in_hand:
            continue
        if s.stack < best:
            best, res = s.stack, s.chair
    return _fl(res)


def _bigstack_chair(c):
    best, res = -1, -1
    for s in c.t.seats:
        if s.chair == c.chair or not s.in_hand:
            continue
        if s.stack > best:
            best, res = s.stack, s.chair
    return _fl(res)


def _headsup_chair(c):
    opp = [x for x in c._playing() if x != c.chair]
    return _fl(opp[0]) if len(opp) == 1 else -1.0


def _first_raiser(c):
    acts = c._aggr()
    return _fl(acts[0]["c"]) if acts else -1.0


def _first_caller(c):
    cs = [a["c"] for a in c._round_actions() if a["k"] == "call" and a["c"] != c.chair]
    return _fl(cs[0]) if cs else -1.0


def _last_caller(c):
    cs = [a["c"] for a in c._round_actions() if a["k"] == "call" and a["c"] != c.chair]
    return _fl(cs[-1]) if cs else -1.0


def _elapsed_hand(c):
    n = len([a for a in c.t.actions if a["k"] not in ("sb", "bb")])
    return c.cfg.hand_base_seconds + c.cfg.action_seconds * n


def _myturnbits(c):
    me = c.me
    call = c._largest_bet() - me.bet
    b = 0
    if call > 0:
        b |= 1 | 2
    else:
        b |= 4
    if me.stack + me.bet > c.t.current_bet:
        b |= 8 | 16
    else:
        b |= 16
    return _fl(b)



_AC_PRE = {10: [1, 2, 3, 3, 3, 4, 4, 5, 5, 6], 9: [1, 2, 3, 3, 4, 4, 5, 5, 6], 8: [1, 2, 3, 4, 4, 5, 5, 6],
           7: [1, 2, 4, 4, 5, 5, 6], 6: [1, 2, 4, 5, 5, 6], 5: [1, 2, 5, 5, 6], 4: [1, 2, 5, 6],
           3: [1, 2, 6], 2: [2, 6]}
_AC_POST = {10: [1, 2, 2, 2, 3, 3, 3, 4, 4, 5], 9: [1, 2, 2, 3, 3, 3, 4, 4, 5], 8: [1, 2, 2, 3, 3, 4, 4, 5],
            7: [1, 2, 3, 3, 4, 4, 5], 6: [1, 2, 3, 4, 4, 5], 5: [1, 2, 3, 4, 5], 4: [1, 3, 4, 5],
            3: [1, 4, 5], 2: [1, 5]}


def _ac_lookup(table, n, pos):
    row = table.get(n)
    if not row or pos < 1 or pos > len(row):
        return 0.0
    return _fl(row[pos - 1])


def _ac_first_into_pot(c):
    pp = sum(s.bet for s in c.t.seats)
    if c.t.betround == 1:
        return _fl(pp <= c.t.sblind + c.t.bblind)
    return _fl(pp == 0)


def _ac_agchair_after(c):
    rc = c.raischair()
    return _fl(rc >= 0 and c._betpos(rc) > c._betpos(c.chair))


_NATIVE_TABLE = {
    # constantes
    "ace": lambda c: 14.0, "king": lambda c: 13.0, "queen": lambda c: 12.0, "jack": lambda c: 11.0,
    "ten": lambda c: 10.0, "preflop": lambda c: 1.0, "flop": lambda c: 2.0, "turn": lambda c: 3.0,
    "river": lambda c: 4.0, "clubs": lambda c: 2.0, "diamonds": lambda c: 1.0, "hearts": lambda c: 0.0,
    "spades": lambda c: 3.0, "true": lambda c: 1.0, "false": lambda c: 0.0,
    # table / blinds
    "betround": lambda c: _fl(c.t.betround), "nchairs": lambda c: _fl(c.t.nchairs),
    "bblind": lambda c: _fl(c.t.bblind), "sblind": lambda c: _fl(c.t.sblind),
    "ante": lambda c: 0.0, "bet": _bet, "buyin": lambda c: _fl(c.t.buyin),
    "prizepool": lambda c: _fl(c.t.prizepool), "prizepoolmultiplier": lambda c: _fl(c.t.multiplier),
    "isnl": lambda c: 1.0, "ispl": lambda c: 0.0, "isfl": lambda c: 0.0, "lim": _lim,
    "isomaha": lambda c: 0.0, "isrush": lambda c: 0.0,
    "istournament": lambda c: 0.0 if c.cfg.is_cash else 1.0, "issng": lambda c: 0.0 if c.cfg.is_cash else 1.0, "isspin": lambda c: 0.0 if c.cfg.is_cash else 1.0,
    "ismtt": lambda c: 0.0, "isdon": lambda c: 0.0, "istripleup": lambda c: 0.0,
    "isshootout": lambda c: 0.0, "isfreeroll": lambda c: 0.0, "isknockout": lambda c: 0.0,
    "isrebuy": lambda c: 0.0, "issatelitte": lambda c: 0.0, "isturbo": lambda c: 0.0,
    "issemiturbo": lambda c: 0.0, "issuperturbo": lambda c: 0.0, "ishyperturbo": lambda c: 1.0 if (c.cfg.level_seconds <= 60 and not c.cfg.is_cash) else 0.0,
    "isultraturbo": lambda c: 0.0, "isfinaltable": lambda c: 0.0 if c.cfg.is_cash else 1.0,
    "ismyturn": lambda c: 1.0, "isfinalanswer": lambda c: 1.0, "issittingin": lambda c: 1.0,
    "issittingout": lambda c: 0.0, "isautopost": lambda c: 0.0, "myturnbits": _myturnbits,
    "userchair": lambda c: _fl(c.chair), "dealerchair": lambda c: _fl(c.t.dealer),
    "elapsed": lambda c: _fl(c.t.clock), "elapsedhand": _elapsed_hand,
    "elapsedauto": lambda c: _fl(c.t.clock), "elapsedstart": lambda c: _fl(c.t.clock),
    "elapsedtoday": lambda c: _fl(c.t.clock),
    "random": _randomfn, "randomheartbeat": lambda c: c._beat_rand,
    "randomhand": lambda c: c._hand_rand,
    "randomround": lambda c: c._round_rand.get(c.t.betround, 0.0),
    "handsplayed": lambda c: _fl(c.me.hands_played), "nhandsplayed": lambda c: _fl(c.me.hands_played),
    # mtt
    "mtt_number_entrants": lambda c: _fl(c.t.nchairs),
    "mtt_players_remaining": lambda c: _fl(len(c.alive_stacks())),
    "mtt_paid_places": lambda c: _fl(len([x for x in c.game.cfg.payout_split if x > 0])),
    "mtt_my_rank": _mtt_rank,
    "mtt_largest_stack": lambda c: _fl(c.alive_stacks()[0]) if c.alive_stacks() else 0.0,
    "mtt_smallest_stack": lambda c: _fl(c.alive_stacks()[-1]) if c.alive_stacks() else 0.0,
    "mtt_average_stack": lambda c: (sum(c.alive_stacks()) / float(len(c.alive_stacks()))) if c.alive_stacks() else 0.0,
    # chips
    "pot": _pot, "potplayer": _potplayer, "potcommon": _potcommon,
    "balance": lambda c: _fl(c.me.stack), "maxbalance": lambda c: _fl(c.game.cfg.starting_stack * c.t.nchairs),
    "balanceatstartofsession": lambda c: _fl(c.game.cfg.starting_stack),
    "currentbet": lambda c: _fl(c.me.bet), "call": lambda c: c._call_amount(),
    "nbetstocall": _nbetstocall, "nbetstorais": lambda c: _nbetstocall(c) + 1.0,
    "ncurrentbets": lambda c: c.me.bet / c._unit(), "ncallbets": _ncallbets,
    "nraisbets": lambda c: _ncallbets(c) + 1.0,
    # joueurs
    "nplayersplaying": lambda c: _fl(_n_playing(c)), "nplayersdealt": lambda c: _fl(_n_dealt(c)),
    "nplayersseated": lambda c: _fl(_n_dealt(c)), "nplayersactive": lambda c: _fl(_n_dealt(c)),
    "nplayersallin": lambda c: _fl(_n_allin(c)),
    "nopponentsplaying": lambda c: _fl(_n_playing(c) - _me_in(c._playing(), c)),
    "nopponentsdealt": lambda c: _fl(_n_dealt(c) - _me_in(c._dealt(), c)),
    "nopponentsseated": lambda c: _fl(_n_dealt(c) - _me_in(c._dealt(), c)),
    "nopponentsactive": lambda c: _fl(_n_dealt(c) - _me_in(c._dealt(), c)),
    "nopponentsallin": lambda c: _fl(sum(1 for s in c.t.seats if s.in_hand and s.allin and s.chair != c.chair)),
    "playersplayingbits": lambda c: _fl(c._bits(c._playing())),
    "playersdealtbits": lambda c: _fl(c._bits(c._dealt())),
    "playersseatedbits": lambda c: _fl(c._bits(c._dealt())),
    "playersactivebits": lambda c: _fl(c._bits(c._dealt())),
    "playersallinbits": lambda c: _fl(c._bits([s.chair for s in c.t.seats if s.in_hand and s.allin])),
    "opponentsplayingbits": lambda c: _fl(c._bits(c._playing()) & ~(1 << c.chair)),
    "opponentsdealtbits": lambda c: _fl(c._bits(c._dealt()) & ~(1 << c.chair)),
    "opponentsseatedbits": lambda c: _fl(c._bits(c._dealt()) & ~(1 << c.chair)),
    "opponentsactivebits": lambda c: _fl(c._bits(c._dealt()) & ~(1 << c.chair)),
    "opponentsallinbits": lambda c: _fl(c._bits([s.chair for s in c.t.seats if s.in_hand and s.allin]) & ~(1 << c.chair)),
    "maxnplayersdealt": lambda c: _fl(c.t.nchairs),
    "nopponentsbetting": lambda c: _fl(sum(1 for s in c.t.seats if s.chair != c.chair and s.bet > 0)),
    "nopponentschecking": lambda c: _fl(sum(1 for s in c.t.seats if s.chair != c.chair and s.in_hand and s.bet == 0)),
    "nopponentsfolded": lambda c: _fl(sum(1 for s in c.t.seats if s.chair != c.chair and s.dealt and not s.in_hand)),
    "nplayerscallshort": lambda c: _fl(sum(1 for s in c.t.seats if s.in_hand and s.bet < c._largest_bet())),
    "nopponentstruelyraising": lambda c: _fl(c.n_truly_raising()),
    "nopponentscalling": lambda c: _fl(c.n_calling()),
    "raischair": lambda c: _fl(c.raischair()), "lastraiserchair": lambda c: _fl(c.raischair()),
    "firstraiserchair": _first_raiser, "firstcallerchair": _first_caller, "lastcallerchair": _last_caller,
    "nopponentsblind": lambda c: _fl(sum(1 for x in _blind_players(c) if x != c.chair)),
    "nplayersblind": lambda c: _fl(len(_blind_players(c))),
    "playersblindbits": lambda c: _fl(c.blind_bits()),
    "opponentsblindbits": lambda c: _fl(c.blind_bits() & ~(1 << c.chair)),
    "bblindbits": lambda c: _fl(1 << c.t.bb_chair) if c.t.bb_chair >= 0 else 0.0,
    # positions
    "dealposition": lambda c: _fl(c._dealpos(c.chair)),
    "betposition": lambda c: _fl(c._betpos(c.chair)),
    "callposition": lambda c: _fl(((c._betpos(c.chair) + max(1, _n_playing(c)) - c._betpos(c.raischair() if c.raischair() >= 0 else c.t.dealer)) % max(1, _n_playing(c)))),
    "dealpositionrais": lambda c: _fl(c._dealpos(c.raischair())) if c.raischair() >= 0 else 0.0,
    "betpositionrais": lambda c: _fl(c._betpos(c.raischair())) if c.raischair() >= 0 else 0.0,
    "nchairsdealtright": lambda c: _fl(sum(1 for i in range(1, c.t.nchairs) if c.t.seats[(c.t.dealer + i) % c.t.nchairs].dealt and (c.t.dealer + i) % c.t.nchairs != c.chair and c._dealpos((c.t.dealer + i) % c.t.nchairs) < c._dealpos(c.chair))) ,
    "nchairsdealtleft": lambda c: _fl(sum(1 for s in c.t.seats if s.dealt and s.chair != c.chair and c._dealpos(s.chair) > c._dealpos(c.chair))),
    "buttonchair": lambda c: _fl(c.t.dealer), "_dealerchair": lambda c: _fl(c.t.dealer), "br": lambda c: _fl(c.t.betround),
    "nopponentsraising": lambda c: _fl(c.n_truly_raising()),
    "ac_preflop_pos": lambda c: _ac_lookup(_AC_PRE, _n_dealt(c), c._dealpos(c.chair)),
    "ac_prefloprais_pos": lambda c: _ac_lookup(_AC_PRE, _n_dealt(c), c._dealpos(c.raischair())) if c.raischair() >= 0 else 0.0,
    "ac_postflop_pos": lambda c: _ac_lookup(_AC_POST, _n_playing(c), c._betpos(c.chair)),
    "ac_first_into_pot": _ac_first_into_pot, "ac_agchair_after": _ac_agchair_after,
    "ac_aggressor": lambda c: _fl(c.raischair()),
    "nplayersround_currentround": lambda c: c.nplayersround(c.t.betround),
    "smallblindchair": lambda c: _fl(c.t.sb_chair), "bigblindchair": lambda c: _fl(c.t.bb_chair),
    "headsupchair": _headsup_chair,
    "cutoffchair": lambda c: _fl(c._logical_chair(1)), "mp3chair": lambda c: _fl(c._logical_chair(2)),
    "mp2chair": lambda c: _fl(c._logical_chair(3)), "mp1chair": lambda c: _fl(c._logical_chair(4)),
    "ep3chair": lambda c: _fl(c._logical_chair(5)), "ep2chair": lambda c: _fl(c._logical_chair(6)),
    "ep1chair": lambda c: _fl(c._logical_chair(7)),
    "utgchair": lambda c: _fl(c._chair_by_dealpos(3)) if _n_dealt(c) > 2 else -1.0,
    "missingsmallblind": _missing_sb, "smallstackchair": _smallstack_chair, "bigstackchair": _bigstack_chair,
    # historique
    "prevaction": lambda c: _fl(c.me.prevaction),
    "didchec": lambda c: c.did("chec", c.t.betround), "didcall": lambda c: c.did("call", c.t.betround),
    "didrais": lambda c: c.did("rais", c.t.betround), "didbetsize": lambda c: c.did("swag", c.t.betround),
    "didswag": lambda c: c.did("swag", c.t.betround),
    "didfold": lambda c: c.did("fold", c.t.betround), "didalli": lambda c: c.did("alli", c.t.betround),
    "nplayersround": lambda c: c.nplayersround(c.t.betround), "nbetsround": lambda c: c.nbetsround(c.t.betround),
    # cartes
    "ncardsknown": _ntotal_known, "ncardsunknown": lambda c: 52.0 - _ntotal_known(c),
    "ncommoncardsknown": _ncomm,
    "ncardsbetter": lambda c: _fl(c._nouts_betterc()[1]), "nouts": lambda c: _fl(c._nouts_betterc()[0]),
    "ispair": lambda c: _fl(len(c._hole) == 2 and (c._hole[0] >> 2) == (c._hole[1] >> 2)),
    "issuited": lambda c: _fl(len(c._hole) == 2 and (c._hole[0] & 3) == (c._hole[1] & 3)),
    "isconnector": lambda c: _fl(len(c._hole) == 2 and abs((c._hole[0] >> 2) - (c._hole[1] >> 2)) == 1),
    "pokerval": _pvfn(0), "pcbits": _pvfn(1), "pokervalplayer": _pvfn(2), "pokervalcommon": _pvfn(3),
    "npcbits": lambda c: _fl(_bitcount(c._pokerval()[1])),
    "hicard": lambda c: _fl(PV_HICARD), "onepair": lambda c: _fl(PV_ONEPAIR), "twopair": lambda c: _fl(PV_TWOPAIR),
    "threeofakind": lambda c: _fl(PV_TRIPS), "straight": lambda c: _fl(PV_STRAIGHT), "flush": lambda c: _fl(PV_FLUSH),
    "fullhouse": lambda c: _fl(PV_FULLHOUSE), "fourofakind": lambda c: _fl(PV_QUADS),
    "straightflush": lambda c: _fl(PV_STFLUSH), "royalflush": lambda c: _fl(PV_ROYAL),
    "ishicard": _type_is(C.HT_HIGHCARD), "isonepair": _type_is(C.HT_PAIR), "istwopair": _type_is(C.HT_TWOPAIR),
    "isthreeofakind": _type_is(C.HT_THREEOFAKIND), "isstraight": _type_is(C.HT_STRAIGHT),
    "isflush": _type_is(C.HT_FLUSH), "isfullhouse": _type_is(C.HT_FULLHOUSE),
    "isfourofakind": _type_is(C.HT_FOUROFAKIND), "isstraightflush": _type_is(C.HT_STRAIGHTFLUSH),
    "isfiveofakind": lambda c: 0.0,
    "isroyalflush": _is_royal, "ishandup": _is_handup, "ishandupcommon": _is_handup_common,
    "ishipair": lambda c: _pair_class(c, "hi"), "ismidpair": lambda c: _pair_class(c, "mid"),
    "islopair": lambda c: _pair_class(c, "lo"), "ishistraight": _is_hi_straight, "ishiflush": _is_hi_flush,
    "rankbits": lambda c: _fl(_rb_all(c)), "rankbitsplayer": lambda c: _fl(_rb_player(c)),
    "rankbitscommon": lambda c: _fl(_rb_common(c)), "rankbitspoker": lambda c: _fl(_pokerbits(c)),
    "srankbits": lambda c: _fl(_srank_all(c)), "srankbitsplayer": lambda c: _fl(_srank_player(c)),
    "srankbitscommon": lambda c: _fl(_srank_common(c)), "srankbitspoker": lambda c: _fl(_spokerbits(c)),
    "rankhi": _rank_hi(_rb_all), "rankhiplayer": _rank_hi(_rb_player), "rankhicommon": _rank_hi(_rb_common),
    "rankhipoker": _rank_hi(_pokerbits), "srankhi": _rank_hi(_srank_all),
    "srankhiplayer": _rank_hi(_srank_player), "srankhicommon": _rank_hi(_srank_common),
    "srankhipoker": _rank_hi(_spokerbits),
    "ranklo": _rank_lo(_rb_all), "rankloplayer": _rank_lo(_rb_player), "ranklocommon": _rank_lo(_rb_common),
    "ranklopoker": _rank_lo(_pokerbits), "sranklo": _rank_lo(_srank_all),
    "srankloplayer": _rank_lo(_srank_player), "sranklocommon": _rank_lo(_srank_common),
    "sranklopoker": _rank_lo(_spokerbits),
    "handrank169": _hr169, "handrank2652": _hr2652, "handrank1326": _hr1326, "handrank1000": _hr1000,
    "handrankp": _hrp,
    "prwin": _prw(0), "prtie": _prw(1), "prlos": _prw(2),
    "prwinnow": _prw_now(0), "prlosnow": _prw_now(2),
    "nhands": lambda c: _fl(c._nhands()[0]), "nhandshi": lambda c: _fl(c._nhands()[1]),
    "nhandsti": lambda c: _fl(c._nhands()[2]), "nhandslo": lambda c: _fl(c._nhands()[3]),
}
for _k in ("nsuited", "nsuitedcommon", "tsuit", "tsuitcommon", "nranked", "nrankedcommon", "trank",
           "trankcommon", "nstraight", "nstraightcommon", "nstraightfill", "nstraightfillcommon",
           "nstraightflush", "nstraightflushcommon", "nstraightflushfill", "nstraightflushfillcommon"):
    _NATIVE_TABLE[_k] = _cs(_k)

_NATIVE_DIGIT = {
    "balance": lambda c, i: _fl(c.t.seats[i].stack) if i < c.t.nchairs else 0.0,
    "stack": lambda c, i: c.stack_sorted(i),
    "currentbet": lambda c, i: _fl(c.t.seats[i].bet) if i < c.t.nchairs else 0.0,
    "balance_rank": lambda c, i: c.balance_sorted(i),
    "bet": lambda c, i: c._unit(),
    "raisbits": lambda c, i: _fl(c.raisbits(i)), "callbits": lambda c, i: _fl(c.callbits(i)),
    "foldbits": lambda c, i: _fl(c.foldbits(i)),
    "lastraised": lambda c, i: _fl(([a["c"] for a in c._aggr(i)] or [-1])[-1]),
    "didchecround": lambda c, i: c.did("chec", i), "didcallround": lambda c, i: c.did("call", i),
    "didraisround": lambda c, i: c.did("rais", i), "didbetsizeround": lambda c, i: c.did("swag", i),
    "didswaground": lambda c, i: c.did("swag", i),
    "didfoldround": lambda c, i: c.did("fold", i), "didalliround": lambda c, i: c.did("alli", i),
    "nplayersround": lambda c, i: c.nplayersround(i), "nbetsround": lambda c, i: c.nbetsround(i),
    "suitbitsplayer": lambda c, i: _fl(c._srankbits(c._hole, {0: 2, 1: 1, 2: 0, 3: 3}[i]) if False else
                                      c._srankbits(c._hole, _inv_suit(i))),
    "suitbitscommon": lambda c, i: _fl(c._srankbits(c._board, _inv_suit(i))),
    "randomround": lambda c, i: c._round_rand.get(i, 0.0),
    "ac_betpos": lambda c, i: _fl(c._betpos(i)) if i < c.t.nchairs and c.t.seats[i].in_hand else 0.0,
    "ac_dealpos": lambda c, i: _fl(c._dealpos(i)) if i < c.t.nchairs and c.t.seats[i].dealt else 0.0,
}


def _inv_suit(oh):
    return {2: 0, 1: 1, 0: 2, 3: 3}[oh]


_ROUNDS = {"preflop": 1, "flop": 2, "turn": 3, "river": 4}


def _role_chair(c, role):
    if role in ("dealer", "button"):
        return c.t.dealer
    if role == "user":
        return c.chair
    f = _NATIVE_TABLE.get(role + "chair")
    return int(f(c)) if f else -1


def _chair_stat(c, m):
    base, role = m.group(1), m.group(2)
    ch = _role_chair(c, role)
    if ch < 0 or ch >= c.t.nchairs:
        return 0.0
    s = c.t.seats[ch]
    if base == "balance":
        return _fl(s.stack)
    if base == "currentbet":
        return _fl(s.bet)
    if base in ("stacksize", "startingstacksize"):
        v = s.stack if base == "stacksize" else s.start_stack
        return v / c._unit()
    return None


def _round_name(c, m):
    base, rn = m.group(1), _ROUNDS[m.group(2)]
    f = _NATIVE_DIGIT.get(base)
    if f is None:
        return None
    return f(c, rn)


_NATIVE_REGEX = [
    (re.compile(r"^(balance|currentbet|stacksize|startingstacksize)_(bigblind|smallblind|dealer|button|cutoff|ep1|ep2|ep3|mp1|mp2|mp3|utg|lastraiser|firstraiser|bigstack|smallstack|headsup|user)chair$"), _chair_stat),
    (re.compile(r"^([a-z]+)_(preflop|flop|turn|river)$"), _round_name),
    (re.compile(r"^suitbits(player|common)_(clubs|diamonds|hearts|spades)$"),
     lambda c, m: _fl(c._srankbits(c._hole if m.group(1) == "player" else c._board,
                                   {"clubs": 0, "diamonds": 1, "hearts": 2, "spades": 3}[m.group(2)]))),
    (re.compile(r"^(didcallround|didchecround|didraisround|didbetsizeround|didswaground|didfoldround|didalliround|nplayersround|nbetsround|raisbits|callbits|foldbits)_?previousround$"),
     lambda c, m: _NATIVE_DIGIT[m.group(1)](c, max(1, c.t.betround - 1)) if c.t.betround > 1 else 0.0),
    (re.compile(r"^raisbits_?(flop|turn|preflop|river)$"), lambda c, m: _fl(c.raisbits({"preflop": 1, "flop": 2, "turn": 3, "river": 4}[m.group(1)]))),
]


# ----------------------------------------------------------------------------
#  Chargement de profils
# ----------------------------------------------------------------------------
_LIB_CACHE = {}
_PT_BASE = {}


def load_pt_baseline(path):
    """Base de stats persistante (JSON {nom: compteurs}), lue une fois par processus."""
    if not path:
        return None
    try:
        mt = os.path.getmtime(path)
    except OSError:
        return None
    k = (path, mt)
    if k not in _PT_BASE:
        import json
        with open(path, encoding="utf-8") as f:
            _PT_BASE[k] = json.load(f)
    return _PT_BASE[k]
_PREFLOP_EQ = {}


def find_repo_root(start=None):
    p = os.path.abspath(start or os.path.dirname(__file__))
    for _ in range(8):
        if os.path.isdir(os.path.join(p, "OpenPPL", "OpenPPL_Library")):
            return p
        p = os.path.dirname(p)
    return None


def library_dir(cfg=None):
    if cfg is not None and getattr(cfg, "library_dir", ""):
        return cfg.library_dir
    root = find_repo_root()
    if root:
        return os.path.join(root, "OpenPPL", "OpenPPL_Library")
    local = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "library")
    return local


def default_bot_dir(cfg=None):
    root = find_repo_root()
    if root:
        d = os.path.join(root, "##_OpenHoldem_Release_Directory_##", "bot_logic", "DefaultBot")
        if os.path.isdir(d):
            return d
    return os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "library", "DefaultBot")


def load_library(cfg=None):
    """Bibliothèque OpenPPL + DefaultBot (analysés une fois, partagés)."""
    ld = library_dir(cfg)
    if ld in _LIB_CACHE:
        return _LIB_CACHE[ld]
    fc = FormulaCollection()
    if os.path.isdir(ld):
        for fn in sorted(os.listdir(ld)):
            if fn.lower().endswith(".ohf"):
                fc.add_file(os.path.join(ld, fn), read_only=True)
    ddir = default_bot_dir(cfg)
    for fn in ("DefaultBot.ohf", "Termita_SNG.ohf", "Gecko_NL_6Max_FR_BSS.ohf"):
        p = os.path.join(ddir, fn)
        if os.path.exists(p):
            fc.add_file(p, read_only=True)
    fc.parse_all()
    _LIB_CACHE[ld] = fc
    return fc


def build_profile(path, cfg=None):
    """Collection de fonctions pour un profil (bibliothèque + fichier utilisateur)."""
    lib = load_library(cfg)
    fc = FormulaCollection()
    fc.functions = dict(lib.functions)
    fc.parse_errors = []
    user = FormulaCollection()
    if path and path != "DefaultBot":
        user.add_file(path)
    names = []
    for k, f in user.functions.items():
        if k.startswith("f$debug") or k.lower() == "f$debug":
            continue
        fc.functions[k] = f
        names.append(k)
    for k in names:
        fc.parse_one(fc.functions[k])
    fc.profile_path = path
    fc.profile_name = os.path.splitext(os.path.basename(path))[0] if path else "DefaultBot"
    return fc


_PROFILE_CACHE = {}


def get_profile(path, cfg=None):
    """build_profile avec cache (clé : chemin + date de modification)."""
    try:
        mt = os.path.getmtime(path) if path and path != "DefaultBot" else 0
    except OSError:
        mt = 0
    key = (path, mt)
    fc = _PROFILE_CACHE.get(key)
    if fc is None:
        fc = build_profile(path, cfg)
        _PROFILE_CACHE[key] = fc
    return fc


def profile_errors(fc):
    return [(f.name, f.error) for f in fc.functions.values() if f.error and not f.read_only]


def is_ohscript(fc):
    f = fc.functions.get("f$call")
    return f is not None and len(f.text.strip()) > 0


# ----------------------------------------------------------------------------
#  Cerveau
# ----------------------------------------------------------------------------
class OHBrain(Brain):
    def __init__(self, fc, cfg, rng, name=None):
        self.fc = fc
        self.cfg = cfg
        self.name = name or fc.profile_name
        self.ctx = SymbolContext(fc, cfg, rng, self.name)
        self.script = is_ohscript(fc)
        self.decisions = 0
        self.last_trace = None

    def new_game(self, game):
        self.ctx.new_game(game)

    def new_hand(self, game):
        pass  # fait dans decide (besoin du siège)

    def end_hand(self, game, record):
        tr = getattr(game, "pt", None)
        if tr is None:
            tr = game.pt = PTTracker(load_pt_baseline(getattr(self.cfg, "pt_db_path", "")))
        tr.update(record)

    # --------------------------------------------------------------
    def decide(self, game, chair):
        ctx = self.ctx
        t = game.t
        if ctx.game is not game or not hasattr(ctx, "_rounds_seen") or ctx._hand_no != t.hand_no:
            ctx.new_hand(game, chair)
            ctx._hand_no = t.hand_no
        ctx.prepare(game, chair)
        flags = self._flags_ohscript(ctx) if self.script else self._flags_openppl(ctx)
        dec = self._to_decision(ctx, flags)
        ctx.after_action()
        self.decisions += 1
        return dec

    # --- OH-script
    def _flags_ohscript(self, ctx):
        flags = {}
        fc = ctx.fc.functions

        def ev(*names):
            for n in names:
                if n in fc:
                    return ctx.evaluate_named(n) or 0.0
            return 0.0
        flags["allin"] = ev("f$allin", "f$alli")
        flags["bp21"] = ev("f$betpot_2_1")
        flags["bp11"] = ev("f$betpot_1_1")
        flags["bp34"] = ev("f$betpot_3_4")
        flags["bp23"] = ev("f$betpot_2_3")
        flags["bp12"] = ev("f$betpot_1_2")
        flags["bp13"] = ev("f$betpot_1_3")
        flags["bp14"] = ev("f$betpot_1_4")
        flags["betsize"] = ev("f$betsize")
        flags["raise"] = ev("f$raise", "f$rais")
        flags["call"] = ev("f$call")
        flags["check"] = ev("f$check")
        flags["fold"] = ev("f$fold")
        return flags

    # --- OpenPPL
    def _flags_openppl(self, ctx):
        t = ctx.t
        name = MAIN_OPENPPL[t.betround - 1]
        f = ctx.fc.functions.get(name)
        if f is None or not f.text.strip():
            dname = default_function_for(name)
            decision = ctx.evaluate_named(dname) if dname else 0.0
        else:
            decision = ctx.evaluate_named(name)
        decision = decision or 0.0
        flags = {k: 0.0 for k in PRIORITY}
        if decision > 0:
            flags["betsize"] = decision * ctx._unit()
        elif -1000 < decision < 0:
            call = ctx._call_amount()
            flags["betsize"] = ctx.me.bet + call + (-decision) * (t.pot() + call)
        elif decision < -1000:
            for key, fname in OH_ACTION_FUNCS:
                if key == "beep":
                    continue
                v = ctx.evaluate_named(fname)
                if v is not None and decision == v:
                    flags[key] = 1.0
        else:
            flags["check"] = 1.0
            flags["fold"] = 1.0
        # chaîne de repli
        for a, b in BACKUP_CHAIN:
            if flags.get(a):
                flags[b] = flags.get(b) or 1.0
        if ctx.me.stack + ctx.me.bet <= t.current_bet and flags.get("call"):
            flags["allin"] = flags.get("allin") or 1.0
        return flags

    # --- traduction en décision du moteur
    def _to_decision(self, ctx, flags):
        t = ctx.t
        me = ctx.me
        call = ctx._call_amount()
        pot = t.pot()
        for key in PRIORITY:
            v = flags.get(key)
            if not v:
                continue
            if key == "allin":
                return Decision("allin", code=PA_ALLIN, note="allin")
            if key in MULTIPLE_OF_POT:
                frac = MULTIPLE_OF_POT[key]
                to = me.bet + call + frac * (pot + call)
                return Decision("raise", to=to, code=PA_BETSIZE, note=key)
            if key == "betsize":
                return Decision("raise", to=v, code=PA_BETSIZE, note="betsize")
            if key == "raise":
                return Decision("raise", to=t.current_bet + max(t.last_raise_size, t.bblind),
                                code=PA_RAISE, note="raise")
            if key == "call":
                return Decision("call", code=PA_CALL)
            if key == "check":
                return Decision("check", code=PA_CHECK)
            if key == "fold":
                return Decision("fold", code=PA_FOLD)
        # rien d'actif : check/fold
        return Decision("fold", code=PA_FOLD)


def make_brain(path, cfg, rng, name=None):
    fc = get_profile(path, cfg)
    b = OHBrain(fc, cfg, rng, name)
    b.ctx._hand_no = -1
    return b
