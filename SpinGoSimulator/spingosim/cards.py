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


def equity_vs_random(hole, board, n_opponents, iterations, rng=None):
    """Probabilité de gagner (win, tie, lose) contre n adversaires aux mains aléatoires.

    Estimation Monte-Carlo ; ``iterations`` tirages.  Les égalités comptent pour
    ``1/k`` victoire (k = nombre de joueurs à égalité) dans ``win + tie/2`` côté
    appelant ; ici on retourne les trois fréquences brutes.
    """
    rng = rng or random
    known = set(hole) | set(board)
    remaining = [c for c in range(52) if c not in known]
    need_board = 5 - len(board)
    need = need_board + 2 * n_opponents
    wins = ties = losses = 0
    for _ in range(iterations):
        draw = rng.sample(remaining, need)
        full_board = list(board) + draw[:need_board]
        mine = evaluate(list(hole) + full_board)
        best_opp = -1
        for o in range(n_opponents):
            i = need_board + 2 * o
            v = evaluate(draw[i:i + 2] + full_board)
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
