//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Core of the OpenHoldem stats database (replacement for PokerTracker).
//
//   Takes a reconstructed hand (hh::Hand, see HandHistoryCore.h) and computes,
//   for every player of the hand, a set of counters:
//     * "xxx"      the player did xxx (e.g. 3-bet)
//     * "xxx_opp"  the player had the opportunity to do xxx
//   A statistic is a ratio of sums of such counters over many hands, e.g.
//     3bet = SUM(three_bet) / SUM(three_bet_opp)
//   (the definitions live in StatsDb.cpp, they are evaluated by SQL).
//
//   This header uses the C++ standard library only (no MFC / OpenHoldem),
//   so it can be unit-tested anywhere.
//
//   Simplifications compared to PokerTracker (on purpose, documented):
//     * only hold'em, no stake / stack-size / tournament-type filters
//     * positions: EP (everything else), CO, BTN, SB, BB
//     * "preflop aggressor" = last player who raised preflop
//
//******************************************************************************

#ifndef INC_OHSTATS_CORE_H
#define INC_OHSTATS_CORE_H

#include <string>
#include <vector>
#include "HandHistoryCore.h"

namespace ohstats {

// All counters. The names are the column names of the database table,
// so keep them valid SQL identifiers.
#define OHSTATS_COUNTERS(X) \
  X(vpip) X(pfr) X(pfr_opp) \
  X(rfi) X(rfi_opp) \
  X(steal) X(steal_opp) \
  X(bb_fold_steal) X(bb_steal_opp) X(sb_fold_steal) X(sb_steal_opp) \
  X(three_bet_steal) X(three_bet_steal_opp) \
  X(bb_3bet_steal) X(bb_3bet_steal_opp) X(sb_3bet_steal) X(sb_3bet_steal_opp) \
  X(called_raise) X(called_raise_opp) \
  X(three_bet) X(three_bet_opp) X(fold_3bet) X(fold_3bet_opp) \
  X(fold_resteal) X(fold_resteal_opp) \
  X(four_bet) X(four_bet_opp) X(fold_4bet) X(fold_4bet_opp) \
  X(squeeze) X(squeeze_opp) \
  X(seen_flop) X(seen_turn) X(seen_river) X(wtsd) X(wssd) \
  X(flop_br) X(flop_call) X(flop_check) X(flop_fold) \
  X(turn_br) X(turn_call) X(turn_check) X(turn_fold) \
  X(river_br) X(river_call) X(river_check) X(river_fold) \
  X(flop_cbet) X(flop_cbet_opp) X(flop_cbet_faced) X(flop_fold_cbet) X(flop_raise_cbet) \
  X(turn_cbet) X(turn_cbet_opp) X(turn_cbet_faced) X(turn_fold_cbet) \
  X(river_cbet_faced) X(river_fold_cbet) \
  X(flop_cr) X(flop_cr_opp) X(turn_cr) X(turn_cr_opp) X(turn_cc) \
  X(flop_donk) X(flop_donk_opp) \
  X(river_bet) X(river_bet_opp)

enum Counter {
#define OHSTATS_X(name) c_##name,
  OHSTATS_COUNTERS(OHSTATS_X)
#undef OHSTATS_X
  kNumCounters
};

inline const char *CounterName(int counter) {
  static const char *const names[] = {
#define OHSTATS_X(name) #name,
    OHSTATS_COUNTERS(OHSTATS_X)
#undef OHSTATS_X
  };
  return names[counter];
}

// Positions, stored as numbers in the database.
// Same priority as in the PokerTracker-DLL: BTN overrides CO overrides SB/BB.
enum Position {
  kPositionEP = 0,   // everything that is not one of the others
  kPositionCO = 1,
  kPositionBTN = 2,
  kPositionSB = 3,
  kPositionBB = 4,
  kPositionUnknown = -1
};

inline const char *PositionName(int position) {
  switch (position) {
    case kPositionEP:  return "ep";
    case kPositionCO:  return "co";
    case kPositionBTN: return "btn";
    case kPositionSB:  return "sb";
    case kPositionBB:  return "bb";
  }
  return "?";
}

struct PlayerStats {
  int         seat;
  std::string name;
  int         position;
  int         counters[kNumCounters];
  PlayerStats() : seat(-1), position(kPositionEP) {
    for (int i = 0; i < kNumCounters; ++i) counters[i] = 0;
  }
};

namespace detail {

inline bool IsPost(hh::ActionType t) {
  return t == hh::kPostAnte || t == hh::kPostSmallBlind || t == hh::kPostBigBlind;
}
inline bool IsAggressive(hh::ActionType t) {
  return t == hh::kBet || t == hh::kRaise;
}

}  // namespace detail

// Position of every seat of the hand (index = seat, kPositionUnknown if not dealt in)
inline std::vector<int> ComputePositions(const hh::Hand &h) {
  std::vector<int> pos(hh::kMaxSeats, kPositionUnknown);
  int n = h.nseats;
  if (n <= 0 || n > hh::kMaxSeats) n = hh::kMaxSeats;
  for (int i = 0; i < n; ++i) {
    if (h.in_hand[i]) pos[i] = kPositionEP;
  }
  if (h.sb_seat >= 0 && h.sb_seat < n && h.in_hand[h.sb_seat]) pos[h.sb_seat] = kPositionSB;
  if (h.bb_seat >= 0 && h.bb_seat < n && h.in_hand[h.bb_seat]) pos[h.bb_seat] = kPositionBB;
  // Cut-off: first player before the button that is neither button nor blind
  if (h.dealer >= 0 && h.dealer < n) {
    for (int k = 1; k < n; ++k) {
      int seat = ((h.dealer - k) % n + n) % n;
      if (!h.in_hand[seat]) continue;
      if (seat == h.sb_seat || seat == h.bb_seat) continue;
      pos[seat] = kPositionCO;
      break;
    }
    if (h.in_hand[h.dealer]) pos[h.dealer] = kPositionBTN;
  }
  return pos;
}

// Computes the counters of all players that were dealt in and have a name.
inline std::vector<PlayerStats> Analyze(const hh::Hand &h) {
  using namespace detail;
  std::vector<PlayerStats> result;
  int n = h.nseats;
  if (n <= 0 || n > hh::kMaxSeats) n = hh::kMaxSeats;
  std::vector<int> positions = ComputePositions(h);
  int index_of[hh::kMaxSeats];
  for (int i = 0; i < hh::kMaxSeats; ++i) index_of[i] = -1;
  for (int i = 0; i < n; ++i) {
    if (!h.in_hand[i] || h.name[i].empty()) continue;
    PlayerStats p;
    p.seat = i;
    p.name = h.name[i];
    p.position = positions[i];
    index_of[i] = (int)result.size();
    result.push_back(p);
  }
  if (result.empty()) return result;
  #define CNT(seat, name) (result[index_of[seat]].counters[c_##name])

  // Street at which the player folded (0 = did not fold)
  int fold_street[hh::kMaxSeats];
  for (int i = 0; i < hh::kMaxSeats; ++i) fold_street[i] = 0;
  for (size_t a = 0; a < h.actions.size(); ++a) {
    const hh::Action &act = h.actions[a];
    if (act.type == hh::kFold && act.seat >= 0 && act.seat < hh::kMaxSeats && fold_street[act.seat] == 0) {
      fold_street[act.seat] = act.street;
    }
  }

  //----------------------------------------------------------------------------
  // Preflop
  //----------------------------------------------------------------------------
  int  raises = 0, limpers = 0, callers_since_raise = 0;
  int  last_raiser = -1;
  bool first_raiser_steal = false;
  bool acted[hh::kMaxSeats], raise_was_steal[hh::kMaxSeats];
  int  last_raise_index[hh::kMaxSeats];
  for (int i = 0; i < hh::kMaxSeats; ++i) { acted[i] = false; raise_was_steal[i] = false; last_raise_index[i] = 0; }

  for (size_t a = 0; a < h.actions.size(); ++a) {
    const hh::Action &act = h.actions[a];
    if (act.street != 1 || IsPost(act.type)) continue;
    const int s = act.seat;
    if (s < 0 || s >= hh::kMaxSeats || index_of[s] < 0) continue;
    const int  pos   = positions[s];
    const bool raise = IsAggressive(act.type);
    const bool call  = (act.type == hh::kCall);
    const bool fold  = (act.type == hh::kFold);
    const bool steal_position = (pos == kPositionCO || pos == kPositionBTN || pos == kPositionSB);
    bool this_raise_is_steal = false;

    if (!acted[s]) {
      CNT(s, pfr_opp) = 1;
      if (raises == 0) {
        if (limpers == 0) {
          CNT(s, rfi_opp) = 1;
          if (raise) CNT(s, rfi) = 1;
          if (steal_position) {
            CNT(s, steal_opp) = 1;
            if (raise) { CNT(s, steal) = 1; this_raise_is_steal = true; }
          }
        }
      } else {
        CNT(s, called_raise_opp) = 1;
        if (call) CNT(s, called_raise) = 1;
        if (raises == 1) {
          CNT(s, three_bet_opp) = 1;
          if (raise) CNT(s, three_bet) = 1;
          if (first_raiser_steal) {
            CNT(s, three_bet_steal_opp) = 1;
            if (raise) CNT(s, three_bet_steal) = 1;
            if (pos == kPositionBB) {
              CNT(s, bb_steal_opp) = 1;
              if (fold) CNT(s, bb_fold_steal) = 1;
              CNT(s, bb_3bet_steal_opp) = 1;
              if (raise) CNT(s, bb_3bet_steal) = 1;
            } else if (pos == kPositionSB) {
              CNT(s, sb_steal_opp) = 1;
              if (fold) CNT(s, sb_fold_steal) = 1;
              CNT(s, sb_3bet_steal_opp) = 1;
              if (raise) CNT(s, sb_3bet_steal) = 1;
            }
          }
          if (callers_since_raise >= 1) {
            CNT(s, squeeze_opp) = 1;
            if (raise) CNT(s, squeeze) = 1;
          }
        } else if (raises == 2) {
          CNT(s, four_bet_opp) = 1;
          if (raise) CNT(s, four_bet) = 1;
        }
      }
      acted[s] = true;
    } else {
      // Later decision of the same player: facing a re-raise
      if (last_raise_index[s] == 1 && raises == 2) {
        CNT(s, fold_3bet_opp) = 1;
        if (fold) CNT(s, fold_3bet) = 1;
        if (raise_was_steal[s]) {
          CNT(s, fold_resteal_opp) = 1;
          if (fold) CNT(s, fold_resteal) = 1;
        }
      } else if (last_raise_index[s] == 2 && raises == 3) {
        CNT(s, fold_4bet_opp) = 1;
        if (fold) CNT(s, fold_4bet) = 1;
      }
    }
    if (call || raise) CNT(s, vpip) = 1;
    if (raise) CNT(s, pfr) = 1;

    // Update the state of the betting round
    if (raise) {
      ++raises;
      last_raise_index[s] = raises;
      last_raiser = s;
      callers_since_raise = 0;
      if (raises == 1) first_raiser_steal = this_raise_is_steal;
      raise_was_steal[s] = this_raise_is_steal;
    } else if (call) {
      if (raises == 0) ++limpers; else ++callers_since_raise;
    }
  }
  const int aggressor = last_raiser;   // preflop aggressor, -1 if nobody raised

  //----------------------------------------------------------------------------
  // Streets seen, showdown
  //----------------------------------------------------------------------------
  for (size_t i = 0; i < result.size(); ++i) {
    PlayerStats &p = result[i];
    const int s = p.seat;
    const int needed_cards[3] = { 3, 4, 5 };   // flop, turn, river
    bool saw[3];
    for (int street = 0; street < 3; ++street) {
      // street 0 = flop (betting round 2), ...
      saw[street] = (h.board_count >= needed_cards[street]) &&
                    (fold_street[s] == 0 || fold_street[s] >= street + 2);
    }
    if (saw[0]) p.counters[c_seen_flop] = 1;
    if (saw[1]) p.counters[c_seen_turn] = 1;
    if (saw[2]) p.counters[c_seen_river] = 1;
    if (saw[0] && h.showdown && !h.folded[s]) {
      p.counters[c_wtsd] = 1;
      if (h.collected[s] > hh::kEps) p.counters[c_wssd] = 1;
    }
  }

  //----------------------------------------------------------------------------
  // Postflop: flop = betting round 2, turn = 3, river = 4
  //----------------------------------------------------------------------------
  bool cbet_made[5] = { false, false, false, false, false };   // index = betting round
  for (int street = 2; street <= 4; ++street) {
    int  bets = 0, first_bettor = -1;
    bool acted_here[hh::kMaxSeats], checked[hh::kMaxSeats];
    bool responded_cbet[hh::kMaxSeats], responded_cr[hh::kMaxSeats];
    for (int i = 0; i < hh::kMaxSeats; ++i) {
      acted_here[i] = checked[i] = responded_cbet[i] = responded_cr[i] = false;
    }
    const bool cbet_chain_ok = (street == 2) || cbet_made[street - 1];
    for (size_t a = 0; a < h.actions.size(); ++a) {
      const hh::Action &act = h.actions[a];
      if (act.street != street || IsPost(act.type)) continue;
      const int s = act.seat;
      if (s < 0 || s >= hh::kMaxSeats || index_of[s] < 0) continue;
      const bool bet   = IsAggressive(act.type);
      const bool raise = (act.type == hh::kRaise);
      const bool call  = (act.type == hh::kCall);
      const bool check = (act.type == hh::kCheck);
      const bool fold  = (act.type == hh::kFold);
      const bool first_action = !acted_here[s];

      // Aggression counters
      int c_br, c_call, c_check, c_fold;
      if (street == 2)      { c_br = c_flop_br;  c_call = c_flop_call;  c_check = c_flop_check;  c_fold = c_flop_fold; }
      else if (street == 3) { c_br = c_turn_br;  c_call = c_turn_call;  c_check = c_turn_check;  c_fold = c_turn_fold; }
      else                  { c_br = c_river_br; c_call = c_river_call; c_check = c_river_check; c_fold = c_river_fold; }
      int *cnt = result[index_of[s]].counters;
      if (bet) cnt[c_br] = cnt[c_br] + 1;
      else if (call) cnt[c_call] = cnt[c_call] + 1;
      else if (check) cnt[c_check] = cnt[c_check] + 1;
      else if (fold) cnt[c_fold] = cnt[c_fold] + 1;

      // Chance to open the betting
      if (bets == 0 && first_action && (check || bet)) {
        if (street == 4) {
          cnt[c_river_bet_opp] = 1;
          if (bet) cnt[c_river_bet] = 1;
        }
        if (s == aggressor && cbet_chain_ok) {
          if (street == 2) { cnt[c_flop_cbet_opp] = 1; if (bet) cnt[c_flop_cbet] = 1; }
          if (street == 3) { cnt[c_turn_cbet_opp] = 1; if (bet) cnt[c_turn_cbet] = 1; }
          if (bet) cbet_made[street] = true;
        }
        if (street == 2 && aggressor >= 0 && s != aggressor
            && index_of[aggressor] >= 0 && !acted_here[aggressor]
            && (fold_street[aggressor] == 0 || fold_street[aggressor] >= 2)) {
          // The aggressor has still to act: a bet now is a donk bet
          cnt[c_flop_donk_opp] = 1;
          if (bet) cnt[c_flop_donk] = 1;
        }
      }

      // Facing a bet
      if (bets >= 1 && (fold || call || raise)) {
        // ... a continuation bet
        if (bets == 1 && first_bettor == aggressor && aggressor >= 0 && cbet_chain_ok
            && s != aggressor && !responded_cbet[s]) {
          if (street == 2) {
            cnt[c_flop_cbet_faced] = 1;
            if (fold) cnt[c_flop_fold_cbet] = 1;
            if (raise) cnt[c_flop_raise_cbet] = 1;
          } else if (street == 3) {
            cnt[c_turn_cbet_faced] = 1;
            if (fold) cnt[c_turn_fold_cbet] = 1;
          } else {
            cnt[c_river_cbet_faced] = 1;
            if (fold) cnt[c_river_fold_cbet] = 1;
          }
          responded_cbet[s] = true;
        }
        // ... after having checked: check-raise / check-call
        if (checked[s] && !responded_cr[s] && (street == 2 || street == 3)) {
          if (street == 2) {
            cnt[c_flop_cr_opp] = 1;
            if (raise) cnt[c_flop_cr] = 1;
          } else {
            cnt[c_turn_cr_opp] = 1;
            if (raise) cnt[c_turn_cr] = 1;
            if (call) cnt[c_turn_cc] = 1;
          }
          responded_cr[s] = true;
        }
      }

      if (first_action && check) checked[s] = true;
      acted_here[s] = true;
      if (bet) {
        ++bets;
        if (bets == 1) first_bettor = s;
        // A raise cancels the "continuation bet" status of the street
      }
    }
  }
  #undef CNT
  return result;
}

}  // namespace ohstats

#endif  // INC_OHSTATS_CORE_H
