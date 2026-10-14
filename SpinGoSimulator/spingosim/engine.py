"""Moteur de jeu : Hold'em No Limit, format Spin & Go (2 ou 3 joueurs).

Le moteur ne connaît pas les profils : il appelle ``brain.decide(table, chair)``
qui renvoie une :class:`Decision`.  Le moteur valide et ajuste chaque décision
(mise minimale, tapis, relance non réouverte, ...).
"""
import random
from dataclasses import dataclass

from . import cards as C
from .payouts import MultiplierTable, prize_pool, payouts

PREFLOP, FLOP, TURN, RIVER = 1, 2, 3, 4
ROUND_NAMES = {1: "preflop", 2: "flop", 3: "turn", 4: "river"}

# codes d'action pour 'prevaction' (cf. k_prevaction_*)
PA_UNDEFINED, PA_FOLD, PA_CHECK, PA_CALL, PA_RAISE, PA_BETSIZE, PA_ALLIN = -2, -1, 0, 1, 2, 3, 4


@dataclass
class Decision:
    kind: str               # fold | check | call | raise | allin
    to: float = 0.0         # montant total de la mise pour 'raise' (en jetons)
    code: int = PA_UNDEFINED  # code d'action OpenHoldem (pour l'historique du joueur)
    note: str = ""


class Seat:
    __slots__ = ("chair", "name", "brain", "stack", "bet", "committed", "cards",
                 "in_hand", "allin", "dealt", "start_stack", "acts", "prevaction",
                 "busted", "place", "profile_id", "hands_played", "rebuys")

    def __init__(self, chair, name, brain, stack, profile_id=None):
        self.chair = chair
        self.name = name
        self.brain = brain
        self.stack = stack
        self.bet = 0
        self.committed = 0
        self.cards = []
        self.in_hand = False
        self.allin = False
        self.dealt = False
        self.start_stack = stack
        self.acts = {r: [] for r in (1, 2, 3, 4)}
        self.prevaction = PA_UNDEFINED
        self.busted = False
        self.place = 0
        self.profile_id = profile_id
        self.hands_played = 0
        self.rebuys = 0


class Table:
    """État observable de la table (lu par le moteur de symboles)."""

    def __init__(self, nchairs, game_id=0):
        self.nchairs = nchairs
        self.seats = []
        self.game_id = game_id
        self.dealer = 0
        self.sb_chair = -1
        self.bb_chair = -1
        self.board = []
        self.betround = PREFLOP
        self.sblind = 0
        self.bblind = 0
        self.ante = 0
        self.actions = []          # journal de la main
        self.pot_prev = 0          # jetons des tours précédents
        self.hand_no = 0
        self.level = 0
        self.clock = 0.0
        self.multiplier = 0.0
        self.prizepool = 0.0
        self.buyin = 0.0
        self.starting_stack = 0
        self.current_bet = 0
        self.last_raise_size = 0
        self.hands_this_game = 0

    # --- accès
    def seat(self, chair):
        return self.seats[chair]

    def pot(self):
        return self.pot_prev + sum(s.bet for s in self.seats)

    def largest_bet(self):
        return max([s.bet for s in self.seats if s.in_hand] + [0])

    def n_in_hand(self):
        return sum(1 for s in self.seats if s.in_hand)

    def players_dealt(self):
        return [s for s in self.seats if s.dealt]

    def next_chair(self, chair):
        return (chair + 1) % self.nchairs


class Game:
    """Une partie complète de Spin & Go."""

    def __init__(self, cfg, brains, names, game_id, rng, multiplier=None, profile_ids=None):
        self.cfg = cfg
        self.rng = rng
        self.game_id = game_id
        n = len(brains)
        self.t = Table(n, game_id)
        self.cash = cfg.is_cash
        self.t.starting_stack = cfg.starting_stack
        self.t.buyin = cfg.buyin
        self.total_rake = 0
        for i in range(n):
            pid = profile_ids[i] if profile_ids else i
            self.t.seats.append(Seat(i, names[i], brains[i], cfg.starting_stack, pid))
        if self.cash:
            self.t.multiplier = 0.0
            self.t.prizepool = 0.0
        else:
            mt = MultiplierTable(cfg.multipliers)
            self.t.multiplier = multiplier if multiplier is not None else mt.draw(rng)
            self.t.prizepool = prize_pool(self.t.multiplier, cfg.buyin, n, cfg.prize_pool_mode)
        self.hands = []
        self.finish_order = []   # chairs éliminées dans l'ordre (la première = dernière place)
        self.t.dealer = rng.randrange(n)
        self.prev_dealer = (self.t.dealer - 1) % n
        self.levels = [tuple(x) for x in cfg.blind_levels]
        self.level_seconds = cfg.level_seconds

    # ------------------------------------------------------------------
    def blinds_for_clock(self, clock):
        if self.cash:
            return 0, self.cfg.cash_sb, self.cfg.cash_bb
        lvl = int(clock // self.level_seconds)
        if lvl >= len(self.levels):
            lvl = len(self.levels) - 1
        sb, bb = self.levels[lvl]
        return lvl, sb, bb

    def alive(self):
        return [s for s in self.t.seats if s.stack > 0]

    # ------------------------------------------------------------------
    def play_cash(self):
        """Session de cash game : N mains, blinds fixes, recave automatique, rake."""
        t = self.t
        cfg = self.cfg
        for s in t.seats:
            s.brain.new_game(self)
        for _ in range(cfg.cash_hands):
            self.play_hand()
        chip = cfg.cash_chip_eur
        net = {s.chair: 0 for s in t.seats}
        for h in self.hands:
            for st in h["seats"]:
                net[st["chair"]] += st["net"]
        ranking = sorted(t.seats, key=lambda s: -net[s.chair])
        result = {"mode": "cash", "game": self.game_id, "multiplier": 0.0, "prizepool": 0.0,
                  "hands": len(self.hands), "seconds": t.clock, "rake": self.total_rake,
                  "players": []}
        for rank, s in enumerate(ranking, 1):
            n_h = max(1, s.hands_played)
            result["players"].append({
                "chair": s.chair, "name": s.name, "profile_id": s.profile_id, "place": rank,
                "net_chips": net[s.chair], "gain": net[s.chair] * chip, "net": net[s.chair] * chip,
                "rebuys": s.rebuys, "hands": s.hands_played,
                "bb100": 100.0 * net[s.chair] / cfg.cash_bb / n_h,
            })
        result["hand_records"] = self.hands
        return result

    def play(self):
        if self.cash:
            return self.play_cash()
        t = self.t
        for s in t.seats:
            s.brain.new_game(self)
        while len(self.alive()) > 1 and len(self.hands) < self.cfg.max_hands_per_game:
            self.play_hand()
        # fin de partie : places
        alive = self.alive()
        order = list(self.finish_order)  # 3ème d'abord
        if len(alive) > 1:
            # limite de mains atteinte : classement au nombre de jetons
            alive.sort(key=lambda s: s.stack)
            for s in alive[:-1]:
                s.busted = True
                order.append(s.chair)
        places = {}
        n = len(t.seats)
        winner = alive[-1] if alive else None
        if winner is not None:
            places[winner.chair] = 1
        pos = n
        for ch in order:
            places[ch] = pos
            pos -= 1
        pays = payouts(t.prizepool, self.cfg.payout_split, n)
        result = {
            "game": self.game_id,
            "multiplier": t.multiplier,
            "prizepool": t.prizepool,
            "hands": len(self.hands),
            "seconds": t.clock,
            "players": [],
        }
        for s in t.seats:
            place = places.get(s.chair, n)
            gain = pays[place - 1] if place - 1 < len(pays) else 0.0
            result["players"].append({
                "chair": s.chair, "name": s.name, "profile_id": s.profile_id,
                "place": place, "gain": gain, "net": gain - self.cfg.buyin,
            })
        result["hand_records"] = self.hands
        return result

    # ------------------------------------------------------------------
    def _log(self, rnd, chair, kind, amt, to, code=PA_UNDEFINED, allin=False):
        self.t.actions.append({"r": rnd, "c": chair, "k": kind, "amt": amt, "to": to,
                               "code": code, "allin": allin})

    def _post(self, seat, amount, kind):
        amount = min(amount, seat.stack)
        seat.stack -= amount
        seat.bet += amount
        seat.committed += amount
        if seat.stack == 0:
            seat.allin = True
        self._log(PREFLOP, seat.chair, kind, amount, seat.bet, allin=seat.allin)

    def play_hand(self):
        t = self.t
        cfg = self.cfg
        rng = self.rng
        lvl, sb, bb = self.blinds_for_clock(t.clock)
        self._hand_start_clock = t.clock
        t.level, t.sblind, t.bblind, t.ante = lvl, sb, bb, 0
        t.hand_no = len(self.hands) + 1
        t.hands_this_game = t.hand_no
        t.actions = []
        t.board = []
        t.betround = PREFLOP
        t.pot_prev = 0
        for s in t.seats:
            s.bet = 0
            s.committed = 0
            s.cards = []
            s.in_hand = False
            s.allin = False
            s.dealt = False
            s.acts = {r: [] for r in (1, 2, 3, 4)}
            s.prevaction = PA_UNDEFINED
        if self.cash:
            for s in t.seats:
                if self.cfg.cash_stack_mode == "reset":
                    s.stack = cfg.cash_stack
                elif s.stack <= 0:
                    s.stack = cfg.cash_stack
                    s.rebuys += 1
        self._flop_seen = False
        alive = self.alive()
        n = len(alive)
        # bouton
        d = t.next_chair(self.prev_dealer)
        while t.seats[d].stack <= 0:
            d = t.next_chair(d)
        t.dealer = d
        self.prev_dealer = d
        order = []   # joueurs vivants à partir du bouton +1
        c = d
        for _ in range(t.nchairs):
            c = t.next_chair(c)
            if t.seats[c].stack > 0:
                order.append(c)
        # order = [SB, BB, ..., dealer] pour n>=3 ; [BB, dealer(SB)] pour n==2
        if n == 2:
            t.sb_chair, t.bb_chair = d, order[0]
        else:
            t.sb_chair, t.bb_chair = order[0], order[1]
        for s in alive:
            s.dealt = True
            s.in_hand = True
            s.start_stack = s.stack
            s.hands_played += 1
        # distribution
        deck = C.shuffled_deck(rng)
        for s in alive:
            s.cards = [deck.pop(), deck.pop()]
        board_cards = [deck.pop() for _ in range(5)]
        for s in alive:
            s.brain.new_hand(self)
        # blinds
        self._post(t.seats[t.sb_chair], sb, "sb")
        self._post(t.seats[t.bb_chair], bb, "bb")
        t.current_bet = max(x.bet for x in t.seats)
        t.last_raise_size = bb
        seconds = cfg.hand_base_seconds
        record_start = {s.chair: s.start_stack for s in alive}
        # tours d'enchères
        showdown = False
        for rnd in (PREFLOP, FLOP, TURN, RIVER):
            t.betround = rnd
            if rnd == FLOP:
                t.board = board_cards[:3]
            elif rnd == TURN:
                t.board = board_cards[:4]
            elif rnd == RIVER:
                t.board = board_cards[:5]
            if rnd > PREFLOP:
                # collecte des mises
                t.pot_prev += sum(s.bet for s in t.seats)
                for s in t.seats:
                    s.bet = 0
                t.current_bet = 0
                t.last_raise_size = bb
            if t.n_in_hand() <= 1:
                break
            if rnd == FLOP:
                self._flop_seen = True
            if rnd == PREFLOP:
                if n >= 3:
                    first = order[2:] + order[:2]      # UTG ... bouton, SB, BB
                else:
                    first = [d, order[0]]              # heads-up : le bouton (SB) parle en premier
            else:
                first = list(order)                    # premier joueur après le bouton
            actions_before = len(t.actions)
            self._betting_round(rnd, first)
            seconds += cfg.action_seconds * (len(t.actions) - actions_before)
            if t.n_in_hand() <= 1:
                break
        # règlement
        results = self._settle(board_cards)
        showdown = results["showdown"]
        if showdown:
            seconds += cfg.showdown_seconds
        t.clock += seconds
        # éliminations
        busted_now = [] if self.cash else [s for s in alive if s.stack <= 0]
        busted_now.sort(key=lambda s: (record_start[s.chair], s.chair))
        for s in busted_now:
            s.busted = True
            self.finish_order.append(s.chair)
        rec = self._build_record(record_start, board_cards, results, lvl, sb, bb)
        self.hands.append(rec)
        for s in alive:
            s.brain.end_hand(self, rec)

    # ------------------------------------------------------------------
    def _betting_round(self, rnd, order):
        t = self.t
        bb = t.bblind
        cfg_order = [c for c in order if t.seats[c].in_hand]
        actors = [c for c in cfg_order if not t.seats[c].allin]
        if len(actors) == 0:
            return
        # un seul joueur peut agir et il n'y a rien à suivre : pas d'enchères
        if len(actors) == 1:
            s = t.seats[actors[0]]
            if s.bet >= t.largest_bet():
                return
        # ordre cyclique complet de la table pour les relances
        pending = list(actors)
        acted = set()
        guard = 0
        while pending:
            guard += 1
            if guard > 500:
                break
            c = pending.pop(0)
            s = t.seats[c]
            if not s.in_hand or s.allin:
                continue
            if t.n_in_hand() <= 1:
                return
            others_can_act = [x for x in t.seats if x.in_hand and not x.allin and x.chair != c]
            to_call = t.current_bet - s.bet
            if to_call <= 0 and not others_can_act:
                break
            can_raise = (c not in acted) and bool(others_can_act or to_call < 0)
            # si tous les adversaires sont all-in et qu'on a déjà égalisé : plus d'action
            dec = s.brain.decide(self, c)
            dec = self._legalize(s, dec, to_call, can_raise)
            self._apply(s, dec, rnd, acted)
            if dec._aggr:
                # nouvelle relance : tous les autres joueurs avec une mise inférieure doivent réagir
                cyc = []
                cc = c
                for _ in range(t.nchairs - 1):
                    cc = t.next_chair(cc)
                    x = t.seats[cc]
                    if x.in_hand and not x.allin and x.bet < t.current_bet:
                        cyc.append(cc)
                pending = cyc
            if t.n_in_hand() <= 1:
                return

    def _legalize(self, s, dec, to_call, can_raise):
        """Rend la décision légale. Renvoie une Decision (avec attribut privé _aggr)."""
        t = self.t
        total = s.bet + s.stack
        kind = dec.kind
        out = Decision(kind, dec.to, dec.code, dec.note)
        out._aggr = False
        if kind == "fold":
            if to_call <= 0:
                out.kind = "check"      # on ne se couche pas gratuitement
                out.code = PA_CHECK
            return out
        if kind == "check":
            if to_call > 0:
                out.kind = "fold"
                out.code = PA_FOLD
            return out
        if kind == "call":
            if to_call <= 0:
                out.kind = "check"
                out.code = PA_CHECK
            return out
        # raise / allin
        if kind == "allin":
            target = total
        else:
            target = int(round(dec.to))
        if target >= total:
            target = total
        if not can_raise or total <= t.current_bet:
            # impossible de relancer : on suit
            out.kind = "call" if to_call > 0 else "check"
            if to_call > 0 and total <= t.current_bet:
                out.kind = "call"
            out.code = PA_CALL if to_call > 0 else PA_CHECK
            return out
        min_to = t.current_bet + t.last_raise_size
        if target < min_to and total > min_to:
            target = min_to
        if target <= t.current_bet:
            out.kind = "call" if to_call > 0 else "check"
            out.code = PA_CALL if to_call > 0 else PA_CHECK
            return out
        out.kind = "raise"
        out.to = target
        out._aggr = True
        if target >= total:
            out.kind = "raise"
            out.note = "allin"
        return out

    def _apply(self, s, dec, rnd, acted):
        t = self.t
        to_call = t.current_bet - s.bet
        if dec.kind == "fold":
            s.in_hand = False
            s.acts[rnd].append(PA_FOLD)
            s.prevaction = PA_FOLD
            self._log(rnd, s.chair, "fold", 0, s.bet, PA_FOLD)
            acted.add(s.chair)
            return
        if dec.kind == "check":
            s.acts[rnd].append(PA_CHECK)
            s.prevaction = PA_CHECK
            self._log(rnd, s.chair, "check", 0, s.bet, PA_CHECK)
            acted.add(s.chair)
            return
        if dec.kind == "call":
            amt = min(to_call, s.stack)
            s.stack -= amt
            s.bet += amt
            s.committed += amt
            if s.stack == 0:
                s.allin = True
            s.acts[rnd].append(PA_CALL)
            s.prevaction = PA_CALL
            self._log(rnd, s.chair, "call", amt, s.bet, PA_CALL, s.allin)
            acted.add(s.chair)
            return
        # raise
        target = dec.to
        amt = target - s.bet
        amt = min(amt, s.stack)
        s.stack -= amt
        s.bet += amt
        s.committed += amt
        if s.stack == 0:
            s.allin = True
        increment = s.bet - t.current_bet
        kind = "bet" if t.current_bet == 0 else "raise"
        full = increment >= t.last_raise_size
        if full:
            t.last_raise_size = increment
            acted.clear()
        t.current_bet = s.bet
        code = dec.code if dec.code in (PA_RAISE, PA_BETSIZE, PA_ALLIN) else PA_BETSIZE
        if s.allin:
            code = PA_ALLIN if dec.code == PA_UNDEFINED else dec.code
        s.acts[rnd].append(code)
        s.prevaction = code
        self._log(rnd, s.chair, kind, amt, s.bet, code, s.allin)
        acted.add(s.chair)

    # ------------------------------------------------------------------
    def _rake_for(self, base):
        """Rake d'une main (en jetons entiers) : % du pot disputé, plafonné, sauf No Flop No Drop."""
        cfg = self.cfg
        if not self.cash or cfg.rake_pct <= 0 or base <= 0:
            return 0
        if cfg.no_flop_no_drop and not self._flop_seen:
            return 0
        rake = int(base * cfg.rake_pct / 100.0)
        if cfg.rake_cap > 0:
            rake = min(rake, int(cfg.rake_cap))
        return rake

    def _settle(self, board_cards):
        t = self.t
        contenders = [s for s in t.seats if s.in_hand]
        # tout ce qui est devant les joueurs va au pot
        t.pot_prev += sum(s.bet for s in t.seats)
        for s in t.seats:
            s.bet = 0
        showdown = len(contenders) > 1
        collected = {s.chair: 0 for s in t.seats}
        pots = []
        committed = {s.chair: s.committed for s in t.seats if s.dealt}
        if not showdown:
            winner = contenders[0]
            total = sum(committed.values())
            others = [v for ch, v in committed.items() if ch != winner.chair]
            uncalled = max(0, committed[winner.chair] - max(others + [0]))
            rake = self._rake_for(total - uncalled)
            winner.stack += total - rake
            collected[winner.chair] = total - rake
            self.total_rake += rake
            pots.append({"amount": total - rake, "winners": [winner.chair], "eligible": [winner.chair]})
            return {"showdown": False, "collected": collected, "pots": pots, "values": {}, "rake": rake}
        # tapis complété : toutes les cartes du board sont dévoilées
        t.board = list(board_cards)
        values = {s.chair: C.evaluate(s.cards + board_cards) for s in contenders}
        levels = sorted(set(v for v in committed.values() if v > 0))
        prev = 0
        layers = []
        for lv in levels:
            amount = sum(min(v, lv) - min(v, prev) for v in committed.values())
            eligible = [s.chair for s in contenders if committed[s.chair] >= lv]
            prev = lv
            if amount <= 0:
                continue
            if not eligible:
                # ne devrait pas arriver : on attribue au contributeur restant le plus engagé
                eligible = [max(contenders, key=lambda x: committed[x.chair]).chair]
            layers.append([amount, eligible])
        # rake prélevé sur les pots disputés (pot principal d'abord) ; les mises non suivies sont rendues
        rake = self._rake_for(sum(a for a, e in layers if len(e) > 1))
        left = rake
        for layer in layers:
            if len(layer[1]) > 1 and left > 0:
                take = min(left, layer[0])
                layer[0] -= take
                left -= take
        self.total_rake += rake
        for amount, eligible in layers:
            if len(eligible) == 1:
                ch = eligible[0]
                t.seats[ch].stack += amount
                collected[ch] += amount
                pots.append({"amount": amount, "winners": [ch], "eligible": eligible,
                             "uncalled": True})
                continue
            if amount <= 0:
                continue
            best = max(values[ch] for ch in eligible)
            winners = [ch for ch in eligible if values[ch] == best]
            share = amount // len(winners)
            rem = amount - share * len(winners)
            # le surplus va au premier gagnant à gauche du bouton
            ordered = []
            c = t.dealer
            for _ in range(t.nchairs):
                c = t.next_chair(c)
                if c in winners:
                    ordered.append(c)
            for i, ch in enumerate(ordered):
                got = share + (1 if i < rem else 0)
                t.seats[ch].stack += got
                collected[ch] += got
            pots.append({"amount": amount, "winners": winners, "eligible": eligible})
        return {"showdown": True, "collected": collected, "pots": pots, "values": values, "rake": rake}

    def _build_record(self, record_start, board_cards, results, lvl, sb, bb):
        t = self.t
        seats = []
        for s in t.seats:
            if not s.dealt:
                continue
            col = results["collected"].get(s.chair, 0)
            seats.append({
                "chair": s.chair, "name": s.name, "profile_id": s.profile_id,
                "stack": record_start[s.chair], "cards": list(s.cards),
                "end": s.stack, "committed": s.committed, "collected": col,
                "net": col - s.committed,
                "folded": not s.in_hand,
            })
        vals = results.get("values", {})
        return {
            "game": self.game_id, "hand": t.hand_no, "level": lvl + 1,
            "sb": sb, "bb": bb, "ante": 0, "time": getattr(self, "_hand_start_clock", t.clock),
            "dealer": t.dealer, "sb_chair": t.sb_chair, "bb_chair": t.bb_chair,
            "seats": seats, "board": list(t.board) if results["showdown"] or len(t.board) else [],
            "board_full": list(board_cards),
            "actions": list(t.actions),
            "showdown": results["showdown"],
            "pots": results["pots"],
            "hand_types": {ch: C.HAND_TYPE_NAMES[C.hand_type(v)] for ch, v in vals.items()},
            "multiplier": t.multiplier, "prizepool": t.prizepool,
            "rake": results.get("rake", 0), "mode": "cash" if self.cash else "spin",
            "flop_seen": bool(getattr(self, "_flop_seen", False)),
        }


# ----------------------------------------------------------------------
#  Cerveaux de test
# ----------------------------------------------------------------------
class Brain:
    """Interface minimale d'un joueur."""
    name = "brain"

    def new_game(self, game):
        pass

    def new_hand(self, game):
        pass

    def end_hand(self, game, record):
        pass

    def decide(self, game, chair):  # pragma: no cover
        raise NotImplementedError


class CallStationBrain(Brain):
    name = "CallStation"

    def decide(self, game, chair):
        return Decision("call", code=PA_CALL)


class RandomBrain(Brain):
    name = "Random"

    def __init__(self, rng):
        self.rng = rng

    def decide(self, game, chair):
        t = game.t
        s = t.seats[chair]
        r = self.rng.random()
        if r < 0.2:
            return Decision("fold", code=PA_FOLD)
        if r < 0.7:
            return Decision("call", code=PA_CALL)
        if r < 0.9:
            return Decision("raise", to=t.current_bet + t.bblind * self.rng.randint(2, 6), code=PA_BETSIZE)
        return Decision("allin", code=PA_ALLIN)
