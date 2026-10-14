"""Tirage du multiplicateur Spin & Go et répartition de la cagnotte."""
import bisect


class MultiplierTable:
    def __init__(self, entries):
        """entries : liste de (multiplicateur, poids)."""
        self.entries = [(float(m), int(c)) for m, c in entries if int(c) > 0]
        self.total = sum(c for _, c in self.entries)
        self._cum = []
        run = 0
        for _, c in self.entries:
            run += c
            self._cum.append(run)

    def draw(self, rng):
        x = rng.randrange(self.total)
        i = bisect.bisect_right(self._cum, x)
        return self.entries[i][0]

    def expected(self):
        return sum(m * c for m, c in self.entries) / self.total

    def probability(self, mult):
        for m, c in self.entries:
            if m == mult:
                return c / self.total
        return 0.0


def prize_pool(multiplier, buyin, num_players, mode):
    if mode == "multiplier_x_total":
        return multiplier * buyin * num_players
    return multiplier * buyin


def payouts(pool, split, num_players):
    """Gains en € par place (index 0 = 1er)."""
    s = list(split)[:num_players]
    tot = sum(s)
    return [pool * x / tot for x in s]
