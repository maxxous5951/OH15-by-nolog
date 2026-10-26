//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: hand equity, see equity.h
//
//******************************************************************************

#include "equity.h"

#include <string.h>
#include <algorithm>

namespace explorer {

namespace {

int CardIndex(const std::string &card) {      // rank * 4 + suit, -1 if invalid
  if (card.size() < 2) return -1;
  const char *rank = strchr("23456789TJQKA", card[0]);
  const char *suit = strchr("cdhs", card[1]);
  if (!rank || !suit || card[0] == 0 || card[1] == 0) return -1;
  return (int)(rank - "23456789TJQKA") * 4 + (int)(suit - "cdhs");
}

// Highest card of a straight in the rank mask (bit 0 = deuce .. bit 12 = ace), -1 if none
int StraightHigh(int mask) {
  for (int high = 12; high >= 4; --high) {
    int need = 0x1F << (high - 4);
    if ((mask & need) == need) return high;
  }
  if ((mask & 0x100F) == 0x100F) return 3;     // A 2 3 4 5: five high
  return -1;
}

int TopBits(int mask, int count) {             // the `count` highest ranks, packed 4 bits each
  int packed = 0;
  for (int r = 12; r >= 0 && count > 0; --r) {
    if (mask & (1 << r)) { packed = (packed << 4) | r; --count; }
  }
  while (count-- > 0) packed <<= 4;
  return packed;
}

// cards: indices 0..51, n = 5..7
int Eval(const int *cards, int n) {
  int rank_count[13] = {0};
  int suit_mask[4] = {0};
  int suit_count[4] = {0};
  int all_mask = 0;
  for (int i = 0; i < n; ++i) {
    int r = cards[i] >> 2, s = cards[i] & 3;
    ++rank_count[r];
    suit_mask[s] |= 1 << r;
    ++suit_count[s];
    all_mask |= 1 << r;
  }
  for (int s = 0; s < 4; ++s) {
    if (suit_count[s] >= 5) {
      int sf = StraightHigh(suit_mask[s]);
      if (sf >= 0) return (8 << 20) | sf;
    }
  }
  int quad = -1, trips[2] = { -1, -1 }, pairs[3] = { -1, -1, -1 }, n_trips = 0, n_pairs = 0;
  for (int r = 12; r >= 0; --r) {
    if (rank_count[r] == 4) quad = r;
    else if (rank_count[r] == 3) { if (n_trips < 2) trips[n_trips++] = r; }
    else if (rank_count[r] == 2) { if (n_pairs < 3) pairs[n_pairs++] = r; }
  }
  if (quad >= 0) {
    int rest = all_mask & ~(1 << quad);
    return (7 << 20) | (quad << 4) | (TopBits(rest, 1) & 15);
  }
  if (n_trips >= 1 && (n_trips >= 2 || n_pairs >= 1)) {
    int pair = n_trips >= 2 ? trips[1] : pairs[0];
    if (n_trips >= 2 && n_pairs >= 1 && pairs[0] > trips[1]) pair = pairs[0];
    return (6 << 20) | (trips[0] << 4) | pair;
  }
  for (int s = 0; s < 4; ++s) {
    if (suit_count[s] >= 5) return (5 << 20) | TopBits(suit_mask[s], 5);
  }
  int straight = StraightHigh(all_mask);
  if (straight >= 0) return (4 << 20) | straight;
  if (n_trips >= 1) {
    int rest = all_mask & ~(1 << trips[0]);
    return (3 << 20) | (trips[0] << 8) | TopBits(rest, 2);
  }
  if (n_pairs >= 2) {
    int rest = all_mask & ~(1 << pairs[0]) & ~(1 << pairs[1]);
    return (2 << 20) | (pairs[0] << 8) | (pairs[1] << 4) | (TopBits(rest, 1) & 15);
  }
  if (n_pairs == 1) {
    int rest = all_mask & ~(1 << pairs[0]);
    return (1 << 20) | (pairs[0] << 12) | TopBits(rest, 3);
  }
  return TopBits(all_mask, 5);
}

class Rng {
 public:
  Rng() : state_(88172645u) {}
  unsigned Next() { state_ = state_ * 1664525u + 1013904223u; return state_ >> 8; }
 private:
  unsigned state_;
};

}  // namespace

int EvaluateCards(const std::vector<std::string> &cards) {
  int idx[7];
  if (cards.size() < 5 || cards.size() > 7) return -1;
  for (size_t i = 0; i < cards.size(); ++i) {
    idx[i] = CardIndex(cards[i]);
    if (idx[i] < 0) return -1;
  }
  return Eval(idx, (int)cards.size());
}

double PotOdds(double pot, double to_call) {
  if (to_call <= 0) return 0;
  return to_call / (pot + to_call);
}

bool ComputeEquity(const std::vector<PlayerCards> &players, const std::vector<std::string> &board_in,
                   int iterations, std::vector<double> *equity, bool *exact) {
  const int n = (int)players.size();
  equity->assign(n, 0.0);
  if (exact) *exact = false;
  if (n < 1 || board_in.size() > 5) return false;
  bool used[52] = { false };
  std::vector<int> hole(2 * n, -1);
  std::vector<int> board;
  for (size_t i = 0; i < board_in.size(); ++i) {
    int c = CardIndex(board_in[i]);
    if (c < 0 || used[c]) return false;
    used[c] = true;
    board.push_back(c);
  }
  int unknown = 0;
  for (int p = 0; p < n; ++p) {
    if (!players[p].known) { ++unknown; continue; }
    int a = CardIndex(players[p].card1), b = CardIndex(players[p].card2);
    if (a < 0 || b < 0 || a == b || used[a] || used[b]) return false;
    used[a] = used[b] = true;
    hole[2 * p] = a; hole[2 * p + 1] = b;
  }
  std::vector<int> deck;
  for (int c = 0; c < 52; ++c) if (!used[c]) deck.push_back(c);
  const int missing = 5 - (int)board.size();
  if ((int)deck.size() < missing + 2 * unknown) return false;

  // number of boards when every hand is known
  double combos = 1;
  for (int k = 0; k < missing; ++k) combos = combos * (double)((int)deck.size() - k) / (double)(k + 1);
  const bool enumerate = (unknown == 0) && combos <= 200000.0;
  if (exact) *exact = enumerate;

  std::vector<double> total(n, 0.0);
  double samples = 0;
  int seven[7];

  struct Scorer {
    const std::vector<int> &hole; const std::vector<int> &board; int n;
    void Score(const std::vector<int> &full_board, const std::vector<int> &h, std::vector<double> *total) const {
      int best = -1, winners = 0;
      std::vector<int> score(n);
      for (int p = 0; p < n; ++p) {
        int cards[7], m = 0;
        cards[m++] = h[2 * p]; cards[m++] = h[2 * p + 1];
        for (size_t i = 0; i < full_board.size(); ++i) cards[m++] = full_board[i];
        score[p] = Eval(cards, m);
        if (score[p] > best) { best = score[p]; winners = 1; }
        else if (score[p] == best) ++winners;
      }
      for (int p = 0; p < n; ++p) if (score[p] == best) (*total)[p] += 1.0 / winners;
    }
  } scorer = { hole, board, n };
  (void)seven;

  if (enumerate) {
    std::vector<int> pick(missing, 0);
    // iterate over all combinations of `missing` cards of the deck
    std::vector<int> full = board;
    full.resize(5);
    const int m = (int)deck.size();
    std::vector<int> idx(missing);
    for (int i = 0; i < missing; ++i) idx[i] = i;
    while (true) {
      for (int i = 0; i < missing; ++i) full[board.size() + i] = deck[idx[i]];
      scorer.Score(full, hole, &total);
      samples += 1;
      int i = missing - 1;
      while (i >= 0 && idx[i] == m - missing + i) --i;
      if (i < 0) break;
      ++idx[i];
      for (int j = i + 1; j < missing; ++j) idx[j] = idx[j - 1] + 1;
    }
  } else {
    Rng rng;
    std::vector<int> d = deck;
    std::vector<int> full = board;
    full.resize(5);
    std::vector<int> h = hole;
    const int need = missing + 2 * unknown;
    for (int it = 0; it < iterations; ++it) {
      // partial shuffle: the first `need` cards are random
      for (int i = 0; i < need; ++i) {
        int j = i + (int)(rng.Next() % (unsigned)(d.size() - i));
        std::swap(d[i], d[j]);
      }
      int k = 0;
      for (int i = 0; i < missing; ++i) full[board.size() + i] = d[k++];
      for (int p = 0; p < n; ++p) {
        if (players[p].known) continue;
        h[2 * p] = d[k++]; h[2 * p + 1] = d[k++];
      }
      scorer.Score(full, h, &total);
      samples += 1;
    }
  }
  if (samples <= 0) return false;
  for (int p = 0; p < n; ++p) (*equity)[p] = total[p] / samples;
  return true;
}

}  // namespace explorer
