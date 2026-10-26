//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: random sample hands, see demo_data.h
//
//******************************************************************************

#include "demo_data.h"

#include <stdio.h>
#include <time.h>
#include <algorithm>
#include <vector>

namespace explorer {

namespace {

// Small deterministic generator (the results must not depend on the platform)
class Rng {
 public:
  explicit Rng(unsigned seed) : state_(seed * 2654435761u + 12345u) {}
  unsigned Next() { state_ = state_ * 1664525u + 1013904223u; return state_ >> 8; }
  double Uniform() { return (Next() & 0xFFFFFF) / 16777216.0; }
  int Below(int n) { return (int)(Next() % (unsigned)n); }
 private:
  unsigned state_;
};

struct Style {
  const char *name;
  double vpip, pfr, three_bet, cbet, fold_to_bet, aggression;
};

const Style kStyles[] = {
  // name       vpip  pfr   3bet  cbet  fold  aggr
  { "Hero",     0.24, 0.19, 0.07, 0.65, 0.40, 0.20 },
  { "Nit",      0.12, 0.10, 0.03, 0.55, 0.60, 0.12 },
  { "Tag",      0.22, 0.18, 0.06, 0.65, 0.45, 0.18 },
  { "Lag",      0.32, 0.26, 0.11, 0.70, 0.35, 0.28 },
  { "Fish",     0.55, 0.10, 0.02, 0.35, 0.30, 0.08 },
  { "Maniac",   0.60, 0.45, 0.20, 0.85, 0.25, 0.40 },
  { "CallStn",  0.48, 0.06, 0.01, 0.25, 0.15, 0.05 },
  { "Reg42",    0.25, 0.20, 0.08, 0.60, 0.48, 0.19 },
};
const int kNumStyles = sizeof(kStyles) / sizeof(kStyles[0]);

const char kRanks[] = "23456789TJQKA";
const char kSuits[] = "shdc";

struct Deck {
  std::vector<std::string> cards;
  explicit Deck(Rng *rng) {
    for (int r = 0; r < 13; ++r) for (int s = 0; s < 4; ++s) {
      cards.push_back(std::string() + kRanks[r] + kSuits[s]);
    }
    for (int i = (int)cards.size() - 1; i > 0; --i) std::swap(cards[i], cards[rng->Below(i + 1)]);
  }
  std::string Draw() { std::string c = cards.back(); cards.pop_back(); return c; }
};

// One simulated hand
class Simulation {
 public:
  Simulation(Rng *rng, const std::vector<int> &styles, bool tournament, double bb)
      : rng_(rng), styles_(styles), bb_(bb) {
    hh::Hand &h = hand;
    h.nseats = 6;
    h.tournament = tournament;
    h.sblind = bb / 2;
    h.bblind = bb;
    for (int i = 0; i < 6; ++i) {
      h.in_hand[i] = true;
      h.start_stack[i] = bb * (60 + rng->Below(90));
      stack_[i] = h.start_stack[i];
      bet_[i] = 0;
      put_[i] = 0;
      folded_[i] = false;
    }
  }

  void Play(int dealer) {
    hh::Hand &h = hand;
    h.dealer = dealer;
    const int sb = (dealer + 1) % 6, bb = (dealer + 2) % 6;
    h.sb_seat = sb; h.bb_seat = bb;
    Post(sb, hh::kPostSmallBlind, bb_ / 2);
    Post(bb, hh::kPostBigBlind, bb_);
    current_ = bb_;
    Deck deck(rng_);
    for (int i = 0; i < 6; ++i) { hole_[i][0] = deck.Draw(); hole_[i][1] = deck.Draw(); }
    h.hero = 0;

    // Preflop: first to act is the seat after the big blind
    int order[6];
    for (int i = 0; i < 6; ++i) order[i] = (bb + 1 + i) % 6;
    BettingRound(1, order, true);
    int last_street = 1;
    int board = 0;
    for (int street = 2; street <= 4 && Alive() > 1 && !AllIn(); ++street) {
      int cards = street == 2 ? 3 : 1;
      for (int i = 0; i < cards; ++i) h.board[board++] = deck.Draw();
      h.board_count = board;
      for (int i = 0; i < 6; ++i) bet_[i] = 0;
      current_ = 0;
      raises_ = 0;
      int post_order[6];
      for (int i = 0; i < 6; ++i) post_order[i] = (dealer + 1 + i) % 6;
      BettingRound(street, post_order, false);
      last_street = street;
    }
    h.last_street = last_street;
    // Cards for the rest of the board on a showdown
    while (Alive() > 1 && board < 5) { h.board[board++] = deck.Draw(); }
    if (Alive() > 1) h.board_count = board;
    Finish();
  }

  hh::Hand hand;

 private:
  int Alive() const { int n = 0; for (int i = 0; i < 6; ++i) if (!folded_[i]) ++n; return n; }
  bool AllIn() const {
    int able = 0;
    for (int i = 0; i < 6; ++i) if (!folded_[i] && stack_[i] > 0.001) ++able;
    return able <= 1;
  }

  void Add(int seat, hh::ActionType type, int street, double amount, double to, double raise_by) {
    hh::Action a;
    a.seat = seat; a.type = type; a.street = street; a.amount = amount; a.to = to;
    a.raise_by = raise_by;
    a.allin = stack_[seat] - amount < 0.001 && amount > 0;
    hand.actions.push_back(a);
    stack_[seat] -= amount;
    put_[seat] += amount;
    bet_[seat] += amount;
  }

  void Post(int seat, hh::ActionType type, double amount) { Add(seat, type, 1, amount, amount, 0); }

  void BettingRound(int street, const int *order, bool preflop) {
    std::vector<int> queue;
    for (int i = 0; i < 6; ++i) if (!folded_[order[i]] && stack_[order[i]] > 0.001) queue.push_back(order[i]);
    raises_ = preflop ? 1 : 0;       // the big blind counts as the first bet
    bool opened_postflop = false;
    size_t index = 0;
    int guard = 0;
    while (index < queue.size() && Alive() > 1 && guard++ < 60) {
      int seat = queue[index++];
      if (folded_[seat] || stack_[seat] < 0.001) continue;
      const Style &st = kStyles[styles_[seat]];
      const double to_call = std::min(current_ - bet_[seat], stack_[seat]);
      double roll = rng_->Uniform();
      hh::ActionType type = hh::kCheck;
      double target = 0;   // total bet after the action

      if (preflop) {
        if (raises_ == 1 && current_ <= bb_ + 0.001) {            // nobody raised yet
          if (roll < st.pfr) { type = hh::kRaise; double sizes[3] = { 2, 2.5, 3 };
                               target = bb_ * sizes[rng_->Below(3)]; }
          else if (to_call > 0.001) { type = (roll < st.vpip) ? hh::kCall : hh::kFold; }
          else type = hh::kCheck;
        } else if (raises_ == 2) {                                 // facing an open
          if (roll < st.three_bet) { type = hh::kRaise; target = current_ * (2.8 + rng_->Uniform()); }
          else if (roll < st.vpip * 0.55) type = hh::kCall;
          else type = hh::kFold;
        } else {                                                   // facing a 3-bet or more
          if (raises_ < 4 && roll < 0.12) { type = hh::kRaise; target = current_ * 2.3; }
          else if (roll < 0.45) type = hh::kCall;
          else type = hh::kFold;
        }
      } else {
        if (current_ < 0.001) {                                    // first to act
          double chance = (last_raiser_ == seat) ? st.cbet : st.aggression;
          if (roll < chance) { type = hh::kBet; target = Pot() * (0.4 + 0.4 * rng_->Uniform()); }
          else type = hh::kCheck;
        } else {
          if (roll < st.fold_to_bet) type = hh::kFold;
          else if (roll < st.fold_to_bet + 0.08 && raises_ < 2) {
            type = hh::kRaise; target = current_ * 2.6;
          } else type = hh::kCall;
        }
      }

      if (type == hh::kRaise || type == hh::kBet) {
        if (target > bet_[seat] + stack_[seat]) target = bet_[seat] + stack_[seat];   // all-in
        if (target < current_ + 0.001) type = (to_call > 0.001) ? hh::kCall : hh::kCheck;
      }
      if (type == hh::kCall && to_call < 0.001) type = hh::kCheck;
      if (type == hh::kFold && to_call < 0.001) type = hh::kCheck;

      // Round to cents (chips for tournaments)
      if (type == hh::kRaise || type == hh::kBet) {
        target = (double)(long long)(target * 100 + 0.5) / 100.0;
        if (hand.tournament) target = (double)(long long)(target + 0.5);
      }
      switch (type) {
        case hh::kFold:  Add(seat, hh::kFold, street, 0, bet_[seat], 0); folded_[seat] = true; break;
        case hh::kCheck: Add(seat, hh::kCheck, street, 0, bet_[seat], 0); break;
        case hh::kCall:  Add(seat, hh::kCall, street, to_call, bet_[seat] + to_call, 0); break;
        case hh::kBet:
        case hh::kRaise: {
          double amount = target - bet_[seat];
          double raise_by = target - current_;
          Add(seat, type, street, amount, target, type == hh::kRaise ? raise_by : 0);
          current_ = target;
          ++raises_;
          last_raiser_ = seat;
          opened_postflop = true;
          // everybody else acts again
          queue.resize(index);
          for (int k = 1; k < 6; ++k) {
            int other = (seat + k) % 6;
            if (!folded_[other] && stack_[other] > 0.001) queue.push_back(other);
          }
          break;
        }
        default: break;
      }
    }
    (void)opened_postflop;
  }

  double Pot() const { double p = 0; for (int i = 0; i < 6; ++i) p += put_[i]; return p; }

  void Finish() {
    hh::Hand &h = hand;
    // Uncalled part of the highest bet of the last round
    double highest = 0, second = 0; int top = -1;
    for (int i = 0; i < 6; ++i) {
      if (bet_[i] > highest) { second = highest; highest = bet_[i]; top = i; }
      else if (bet_[i] > second) second = bet_[i];
    }
    if (top >= 0 && highest - second > 0.001) {
      h.uncalled = highest - second;
      h.uncalled_seat = top;
    }
    double pot = Pot() - h.uncalled;
    h.total_pot = pot;
    h.rake = 0;
    // Winner
    std::vector<int> alive;
    for (int i = 0; i < 6; ++i) if (!folded_[i]) alive.push_back(i);
    h.showdown = alive.size() > 1;
    int winner = alive[rng_->Below((int)alive.size())];
    for (int i = 0; i < 6; ++i) {
      h.folded[i] = folded_[i];
      h.contrib[i] = put_[i];
      h.collected[i] = (i == winner) ? pot : 0;
      h.end_stack[i] = h.start_stack[i] - put_[i] + (i == winner ? pot : 0) +
                       (i == h.uncalled_seat ? h.uncalled : 0);
      if (i == h.hero || (h.showdown && !folded_[i])) {
        h.hole[i][0] = hole_[i][0];
        h.hole[i][1] = hole_[i][1];
      }
    }
  }

  Rng *rng_;
  std::vector<int> styles_;
  double bb_;
  double stack_[6], bet_[6], put_[6];
  bool folded_[6];
  std::string hole_[6][2];
  double current_;
  int raises_;
  int last_raiser_ = -1;
};

}  // namespace

bool AddDemoHands(ohstats::Db *db, int hands, unsigned seed, std::string *error) {
  Rng rng(seed);
  const long long now = (long long)time(NULL);
  int added = 0;
  for (int n = 0; n < hands; ++n) {
    const bool tournament = (n % 12 == 11);
    // Table: hero + 5 of the 7 opponents
    std::vector<int> pool;
    for (int i = 1; i < kNumStyles; ++i) pool.push_back(i);
    for (int i = (int)pool.size() - 1; i > 0; --i) std::swap(pool[i], pool[rng.Below(i + 1)]);
    std::vector<int> styles(6);
    styles[0] = 0;
    for (int i = 1; i < 6; ++i) styles[i] = pool[i - 1];

    Simulation sim(&rng, styles, tournament, tournament ? 100 : 1.0);
    sim.hand.hand_id = "demo-" + std::to_string(seed) + "-" + std::to_string(n);
    sim.hand.table_name = tournament ? "Demo Tournament 1" : "Demo Table";
    sim.hand.start_time = now - (long long)(hands - n) * 60;
    sim.hand.timestamp = ohstats::FormatTimestamp(sim.hand.start_time);
    if (tournament) {
      sim.hand.tournament_id = "1234567";
      sim.hand.level = 1 + n / 60;
    }
    for (int i = 0; i < 6; ++i) sim.hand.name[i] = kStyles[styles[i]].name;
    sim.Play(n % 6);
    std::string message;
    if (db->InsertHand(sim.hand, (n % 5 == 4) ? "party" : "stars", &message)) ++added;
    else if (message != "duplicate hand") { if (error) *error = message; return false; }
  }
  return true;
}

bool GenerateDemoDb(const std::string &path, int hands, unsigned seed, std::string *error) {
  ohstats::Db db;
  if (!db.Open(path, error)) return false;
  return AddDemoHands(&db, hands, seed, error);
}

}  // namespace explorer
