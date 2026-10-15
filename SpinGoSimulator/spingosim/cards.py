"""Cartes, paquet et évaluateur de mains (Hold'em, 5 à 7 cartes).

Une carte est un entier 0..51 : ``rang * 4 + couleur`` avec rang 0 (=2) .. 12 (=As)
et couleur 0..3 (c, d, h, s).  Les valeurs de main sont des entiers comparables ;
le format ressemble à celui de poker-eval (type de main dans les bits hauts).
"""
import random
from itertools import combinations

RANKS = "23456789TJQKA"
SUITS = "cdhs"

HT_HIGHCARD, HT_PAIR, HT_TWOPAIR, HT_THREEOFAKIND, HT_STRAIGHT, HT_FLUSH, \
    HT_FULLHOUSE, HT_FOUROFAKIND, HT_STRAIGHTFLUSH = range(9)
HAND_TYPE_NAMES = ["highcard", "pair", "twopair", "threeofakind", "straight",
                   "flush", "fullhouse", "fourofakind", "straightflush"]


def card(rank, suit):
    return rank * 4 + suit


def rank_of(c):
    return c >> 2


def suit_of(c):
    return c & 3


def card_to_str(c):
    return RANKS[c >> 2] + SUITS[c & 3]


def str_to_card(s):
    return RANKS.index(s[0].upper()) * 4 + SUITS.index(s[1].lower())


def cards_to_str(cs):
    return " ".join(card_to_str(c) for c in cs)


def new_deck():
    return list(range(52))


def shuffled_deck(rng=None):
    d = new_deck()
    (rng or random).shuffle(d)
    return d


def _pack(htype, ranks):
    """Empaquette un type et jusqu'à 5 rangs (poids décroissant)."""
    v = htype
    for i in range(5):
        v = (v << 4) | (ranks[i] if i < len(ranks) else 0)
    return v


def _straight_high(rank_mask):
    """Retourne le rang le plus haut d'une quinte contenue dans le masque, ou -1."""
    # La roue (A-2-3-4-5) : l'As compte comme rang -1.
    m = rank_mask | ((rank_mask >> 12) & 1)  # bit 0 = As bas
    # Cherche de haut en bas : 5 bits consécutifs.
    # bit i (i=0..12) = rang i. Pour la roue, l'As (bit 12) agit en bit -1.
    for high in range(12, 3, -1):
        need = 0x1F << (high - 4)
        if rank_mask & need == need:
            return high
    # roue : A,2,3,4,5 -> rangs 12,0,1,2,3
    if (rank_mask & 0xF) == 0xF and (rank_mask >> 12) & 1:
        return 3
    return -1


def evaluate(cards):
    """Valeur comparable (plus grand = meilleur) de la meilleure main avec 5 à 7 cartes."""
    n = len(cards)
    rank_counts = [0] * 13
    suit_cards = [[], [], [], []]
    rank_mask = 0
    for c in cards:
        r = c >> 2
        rank_counts[r] += 1
        rank_mask |= 1 << r
        suit_cards[c & 3].append(r)
    # Couleur / quinte flush
    flush_ranks = None
    for s in range(4):
        if len(suit_cards[s]) >= 5:
            flush_ranks = suit_cards[s]
            break
    if flush_ranks is not None:
        fmask = 0
        for r in flush_ranks:
            fmask |= 1 << r
        sh = _straight_high(fmask)
        if sh >= 0:
            return _pack(HT_STRAIGHTFLUSH, [sh])
    quads = []
    trips = []
    pairs = []
    singles = []
    for r in range(12, -1, -1):
        k = rank_counts[r]
        if k == 4:
            quads.append(r)
        elif k == 3:
            trips.append(r)
        elif k == 2:
            pairs.append(r)
        elif k == 1:
            singles.append(r)
    if quads:
        rest = sorted(trips + pairs + singles, reverse=True)
        return _pack(HT_FOUROFAKIND, [quads[0], rest[0]])
    if trips and (len(trips) > 1 or pairs):
        pair_candidates = sorted(trips[1:] + pairs, reverse=True)
        return _pack(HT_FULLHOUSE, [trips[0], pair_candidates[0]])
    if flush_ranks is not None:
        top = sorted(flush_ranks, reverse=True)[:5]
        return _pack(HT_FLUSH, top)
    sh = _straight_high(rank_mask)
    if sh >= 0:
        return _pack(HT_STRAIGHT, [sh])
    if trips:
        kick = sorted(pairs + singles, reverse=True)[:2]
        return _pack(HT_THREEOFAKIND, [trips[0]] + kick)
    if len(pairs) >= 2:
        kick = sorted(pairs[2:] + singles, reverse=True)[:1]
        return _pack(HT_TWOPAIR, [pairs[0], pairs[1]] + kick)
    if len(pairs) == 1:
        return _pack(HT_PAIR, [pairs[0]] + singles[:3])
    return _pack(HT_HIGHCARD, singles[:5])


def hand_type(value):
    return value >> 20


def hand_ranks(value):
    """Les 5 rangs stockés (poids décroissant)."""
    return [(value >> (4 * (4 - i))) & 0xF for i in range(5)]


def equity_vs_random(hole, board, n_opponents, iterations, rng=None, opp_prob=None):
    """Probabilité de gagner (win, tie, lose) contre n adversaires.

    Estimation Monte-Carlo ; ``iterations`` tirages.  Par défaut les mains adverses sont aléatoires ;
    avec ``opp_prob`` (table 52x52 de :func:`opp_prob_table`) elles sont tirées dans la plage pondérée
    (prwin pondéré d'OpenHoldem).  Les égalités sont retournées séparément.
    """
    rng = rng or random
    known = set(hole) | set(board)
    remaining = [c for c in range(52) if c not in known]
    need_board = 5 - len(board)
    need = need_board + 2 * n_opponents
    wins = ties = losses = 0
    for _ in range(iterations):
        if opp_prob is None:
            draw = rng.sample(remaining, need)
            full_board = list(board) + draw[:need_board]
            opps = [draw[need_board + 2 * o:need_board + 2 * o + 2] for o in range(n_opponents)]
        else:
            pool = list(remaining)
            opps = []
            for _o in range(n_opponents):
                for _try in range(400):
                    a, b = rng.sample(pool, 2)
                    p = opp_prob[a][b]
                    if p >= 1.0 or rng.random() < p:
                        break
                pool.remove(a)
                pool.remove(b)
                opps.append([a, b])
            full_board = list(board) + rng.sample(pool, need_board)
        mine = evaluate(list(hole) + full_board)
        best_opp = -1
        for o in opps:
            v = evaluate(o + full_board)
            if v > best_opp:
                best_opp = v
        if mine > best_opp:
            wins += 1
        elif mine == best_opp:
            ties += 1
        else:
            losses += 1
    t = float(iterations)
    return wins / t, ties / t, losses / t


def hand_class_169(c0, c1):
    """Nom de la main de départ (ex: 'AKs', 'QQo', 'T9o') — 'o' même pour les paires."""
    r0, r1 = c0 >> 2, c1 >> 2
    if r0 < r1:
        r0, r1 = r1, r0
        c0, c1 = c1, c0
    suited = (c0 & 3) == (c1 & 3)
    return RANKS[r0] + RANKS[r1] + ("s" if suited and r0 != r1 else "o")


def all_combinations(cards, k):
    return combinations(cards, k)


# --- pondération de prwin (f$prwin_willplay / wontplay / mustplay / topclip) ---------------
# Classement Sklansky-Chubukov utilisé par OpenHoldem (CIteratorThread.cpp, prwin_handrank_table_169)
PRWIN_HANDRANK_169 = ['AAo', 'KKo', 'AKs', 'QQo', 'AKo', 'JJo', 'AQs', 'TTo', 'AQo', '99o', 'AJs', '88o', 'ATs', 'AJo', '77o', '66o', 'ATo', 'A9s', '55o', 'A8s', 'KQs', '44o', 'A9o', 'A7s', 'KJs', 'A5s', 'A8o', 'A6s', 'A4s', '33o', 'KTs', 'A7o', 'A3s', 'KQo', 'A2s', 'A5o', 'A6o', 'A4o', 'KJo', 'QJs', 'A3o', '22o', 'K9s', 'A2o', 'KTo', 'QTs', 'K8s', 'K7s', 'JTs', 'K9o', 'K6s', 'QJo', 'Q9s', 'K5s', 'K8o', 'K4s', 'QTo', 'K7o', 'K3s', 'K2s', 'Q8s', 'K6o', 'J9s', 'K5o', 'Q9o', 'JTo', 'K4o', 'Q7s', 'T9s', 'Q6s', 'K3o', 'J8s', 'Q5s', 'K2o', 'Q8o', 'Q4s', 'J9o', 'Q3s', 'T8s', 'J7s', 'Q7o', 'Q2s', 'Q6o', '98s', 'Q5o', 'J8o', 'T9o', 'J6s', 'T7s', 'J5s', 'Q4o', 'J4s', 'J7o', 'Q3o', '97s', 'T8o', 'J3s', 'T6s', 'Q2o', 'J2s', '87s', 'J6o', '98o', 'T7o', '96s', 'J5o', 'T5s', 'T4s', '86s', 'J4o', 'T6o', '97o', 'T3s', '76s', '95s', 'J3o', 'T2s', '87o', '85s', '96o', 'T5o', 'J2o', '75s', '94s', 'T4o', '65s', '86o', '93s', '84s', '95o', 'T3o', '76o', '92s', '74s', '54s', 'T2o', '85o', '64s', '83s', '94o', '75o', '82s', '73s', '93o', '65o', '53s', '63s', '84o', '92o', '43s', '74o', '72s', '54o', '64o', '52s', '62s', '83o', '42s', '82o', '73o', '53o', '63o', '32s', '43o', '72o', '52o', '62o', '42o', '32o']
_PRWIN_RANK = {n: i + 1 for i, n in enumerate(PRWIN_HANDRANK_169)}
_OPP_PROB_CACHE = {}


def prwin_range_probability(hrank, willplay, wontplay, topclip, mustplay):
    """Probabilité qu'un adversaire joue une main de rang ``hrank`` (IsHandInWeightedRange d'OpenHoldem)."""
    if hrank >= wontplay:
        return 0.0
    if hrank <= mustplay:
        return 1.0
    if topclip < hrank <= willplay:
        return 1.0
    if hrank <= topclip:
        # OpenHoldem : topclip * (1 - rand()) est négatif -> toujours vrai
        return 1.0
    d = wontplay - willplay
    if d < 3:
        return 1.0
    return max(0, d - 1 - (hrank - willplay)) / float(d)


def opp_prob_table(willplay, wontplay, topclip, mustplay, nopp):
    """Table 52x52 des probabilités de jeu d'un adversaire, ou None si la pondération est désactivée."""
    willplay, wontplay, topclip, mustplay = int(willplay), int(wontplay), int(topclip), int(mustplay)
    if not willplay:
        return None
    if willplay < 2 * nopp + 1:
        willplay = 2 * nopp + 1
    if wontplay < willplay:
        wontplay = willplay
    key = (willplay, wontplay, topclip, mustplay)
    t = _OPP_PROB_CACHE.get(key)
    if t is None:
        probs = {n: prwin_range_probability(r, willplay, wontplay, topclip, mustplay) for n, r in _PRWIN_RANK.items()}
        t = [[0.0] * 52 for _ in range(52)]
        for a in range(52):
            for b in range(52):
                if a != b:
                    t[a][b] = probs[hand_class_169(a, b)]
        _OPP_PROB_CACHE[key] = t
    return t
