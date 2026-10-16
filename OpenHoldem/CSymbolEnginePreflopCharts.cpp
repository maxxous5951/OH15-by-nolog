//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: see CSymbolEnginePreflopCharts.h
//
//******************************************************************************

#include "stdafx.h"
#include "CSymbolEnginePreflopCharts.h"

#include <assert.h>
#include <time.h>
#include <string>
#include <vector>
#include "CBetroundCalculator.h"
#include "CEngineContainer.h"
#include "CPokerTrackerThread.h"
#include "CSymbolEngineActiveDealtPlaying.h"
#include "CSymbolEngineChipAmounts.h"
#include "CSymbolEngineIsOmaha.h"
#include "CSymbolEnginePokerAction.h"
#include "CSymbolEngineTableLimits.h"
#include "CSymbolEngineUserchair.h"
#include "CTableState.h"
#include "..\CTablemap\CTablemap.h"
#include "..\ExploitEV\DecisionLog.h"
#include "..\ExploitEV\PreflopAdjust.h"
#include "..\ExploitEV\PreflopCharts.h"

namespace {

// PokerTracker stats are only trusted with at least that many hands per opponent.
const double kMinHandsForStats = 40.0;
// An all-in is made instead of a raise when the raise would commit this share of our stack.
const double kCommitShare = 0.40;

double BigBlind() {
  return p_engine_container->symbol_engine_tablelimits()->bblind();
}

double LargestBetOnTable() {
  double largest = 0.0;
  for (int chair = 0; chair < p_tablemap->nchairs(); ++chair) {
    double bet = p_table_state->Player(chair)->_bet.GetValue();
    if (bet > largest) largest = bet;
  }
  return largest;
}

// Hands that continue against heavy aggression when we have no chart for the spot.
// Returns call / raise frequencies.
void PremiumResponse(int hand_class, double stack_bb, double *call, double *raise) {
  *call = 0.0; *raise = 0.0;
  const int aa = 12 * 13 + 12, kk = 11 * 13 + 11, qq = 10 * 13 + 10, jj = 9 * 13 + 9;
  const int tt = 8 * 13 + 8;
  const int aks = 12 * 13 + 11, ako = 11 * 13 + 12, aqs = 12 * 13 + 10;
  if (hand_class == aa || hand_class == kk) { *raise = 1.0; return; }
  if (hand_class == qq || hand_class == aks) { *call = 1.0; return; }
  if (hand_class == ako || hand_class == jj) { *call = (stack_bb <= 50.0) ? 1.0 : 0.5; return; }
  if (hand_class == aqs || hand_class == tt) { *call = (stack_bb <= 30.0) ? 1.0 : 0.0; return; }
}

}  // namespace

CSymbolEnginePreflopCharts::CSymbolEnginePreflopCharts() {
  // The values of some symbol-engines depend on other engines.
  // As the engines get later called in the order of initialization
  // we assure correct ordering by checking if they are initialized.
  assert(p_engine_container->symbol_engine_active_dealt_playing() != NULL);
  assert(p_engine_container->symbol_engine_chip_amounts() != NULL);
  assert(p_engine_container->symbol_engine_dealerchair() != NULL);
  assert(p_engine_container->symbol_engine_poker_action() != NULL);
  assert(p_engine_container->symbol_engine_tablelimits() != NULL);
  assert(p_engine_container->symbol_engine_userchair() != NULL);
  _rng_state = 0x9E3779B97F4A7C15ULL ^ (unsigned long long)time(NULL);
  _stats_reliable = false;
  _roll = 0.5;
  _roll_key = -1;
  _last_logged_key = -1;
  _eq_key = -1; _eq_value = -1.0;
  UpdateOnHandreset();
}

CSymbolEnginePreflopCharts::~CSymbolEnginePreflopCharts() {
}

void CSymbolEnginePreflopCharts::InitOnStartup() {
  UpdateOnHandreset();
}

void CSymbolEnginePreflopCharts::UpdateOnConnection() {
  UpdateOnHandreset();
}

void CSymbolEnginePreflopCharts::UpdateOnHandreset() {
  _raises = 0;
  for (int i = 0; i <= kMaxTrackedRaises; ++i) _raiser_chair[i] = kUndefined;
  _previous_largest_bet = -1.0;
  for (int i = 0; i < 10; ++i) {
    _previous_bet[i] = 0.0;
    _preflop_bet[i] = 0.0;
  }
  _roll_key = -1;
  ResetDecision();
}

void CSymbolEnginePreflopCharts::UpdateOnNewRound() {
  ResetDecision();
}

void CSymbolEnginePreflopCharts::UpdateOnMyTurn() {
  Calculate();
}

void CSymbolEnginePreflopCharts::UpdateOnHeartbeat() {
  if (p_betround_calculator->betround() == kBetroundPreflop) {
    TrackPreflopBets();
  }
}

void CSymbolEnginePreflopCharts::ResetDecision() {
  _valid = false;
  _regime = 0;
  _situation = 0;
  _fold = 1.0;
  _call = 0.0;
  _raise = 0.0;
  _raiseto = 0.0;
  _allin = false;
  _action = 0;
  _stack_bb = 0.0;
  _position = kUndefined;
  _players = 0;
}

// ---------------------------------------------------------------------------
// Preflop history: count the raises by watching the largest bet grow.
// ---------------------------------------------------------------------------
void CSymbolEnginePreflopCharts::TrackPreflopBets() {
  const double bb = BigBlind();
  if (bb <= 0.0) return;
  const double largest = LargestBetOnTable();
  const int nchairs = p_tablemap->nchairs();
  const double raise_threshold = bb * 1.01;

  if (_previous_largest_bet < 0.0) {
    // First observation in this hand. We might have joined late (bets are already on the
    // table), so estimate the number of raises from the size.
    if (largest > raise_threshold) {
      _raises = (largest <= 4.5 * bb) ? 1 : ((largest <= 14.0 * bb) ? 2 : 3);
      for (int chair = 0; chair < nchairs; ++chair) {
        if (p_table_state->Player(chair)->_bet.GetValue() >= largest) {
          _raiser_chair[_raises] = chair;
          break;
        }
      }
    }
    _previous_largest_bet = largest;
  } else if (largest > _previous_largest_bet * 1.0001 && largest > raise_threshold) {
    if (_raises < kMaxTrackedRaises) ++_raises;
    // The raiser is the chair that now holds the largest bet and has just increased it
    int raiser = kUndefined;
    for (int chair = 0; chair < nchairs; ++chair) {
      double bet = p_table_state->Player(chair)->_bet.GetValue();
      if (bet >= largest && bet > _previous_bet[chair]) { raiser = chair; break; }
    }
    if (raiser == kUndefined) {
      for (int chair = 0; chair < nchairs; ++chair) {
        if (p_table_state->Player(chair)->_bet.GetValue() >= largest) { raiser = chair; break; }
      }
    }
    _raiser_chair[_raises] = raiser;
    _previous_largest_bet = largest;
  } else if (largest > _previous_largest_bet) {
    _previous_largest_bet = largest;      // blinds still being posted
  }
  for (int chair = 0; chair < nchairs && chair < 10; ++chair) {
    double bet = p_table_state->Player(chair)->_bet.GetValue();
    _previous_bet[chair] = bet;
    if (bet > _preflop_bet[chair]) _preflop_bet[chair] = bet;
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
int CSymbolEnginePreflopCharts::ActionIndexOfChair(int chair) {
  if (chair < 0 || chair > 9) return kUndefined;
  const int players = p_engine_container->symbol_engine_active_dealt_playing()->nplayersdealt();
  const int deal_position = p_engine_container->symbol_engine_poker_action()->DealPosition(chair);
  if (players < 2 || deal_position < 1) return kUndefined;
  // OH: dealposition 1 = SB, 2 = BB, 3 = UTG ... players = button.
  // Heads-up the button (= SB) has dealposition 2 and the BB has 1.
  if (players == 2) return (deal_position == 2) ? 0 : 1;
  if (deal_position >= 3) return deal_position - 3;
  return players - 3 + deal_position;       // SB: players-2, BB: players-1
}

double CSymbolEnginePreflopCharts::ReadPokerTrackerHands(int chair) {
  if (p_pokertracker_thread == NULL) return -1.0;
  if (!p_pokertracker_thread->IsConnected()) return -1.0;
  CString symbol;
  symbol.Format("pt_hands%d", chair);
  double value = kUndefined;
  if (!p_engine_container->EvaluateSymbol(symbol, &value, false)) return -1.0;
  return (value < 0.0) ? -1.0 : value;
}

double CSymbolEnginePreflopCharts::ReadStat(const char *name, int chair) {
  if (!StatsReliableForChair(chair)) return -1.0;
  CString symbol;
  symbol.Format("pt_%s%d", name, chair);
  double value = kUndefined;
  if (!p_engine_container->EvaluateSymbol(symbol, &value, false)) return -1.0;
  if (value < 0.0 || value > 100.0) return -1.0;
  return (value > 1.0) ? value / 100.0 : value;
}

bool CSymbolEnginePreflopCharts::StatsReliableForChair(int chair) {
  return ReadPokerTrackerHands(chair) >= kMinHandsForStats;
}

bool CSymbolEnginePreflopCharts::UpdateStatsReliable() {
  const int opponents =
    p_engine_container->symbol_engine_active_dealt_playing()->opponentsplayingbits();
  int counted = 0;
  for (int chair = 0; chair < p_tablemap->nchairs(); ++chair) {
    if (!(opponents & (1 << chair))) continue;
    ++counted;
    if (!StatsReliableForChair(chair)) return false;
  }
  return counted > 0;
}

// ---------------------------------------------------------------------------
// The decision
// ---------------------------------------------------------------------------
void CSymbolEnginePreflopCharts::Calculate() {
  ResetDecision();
  if (p_betround_calculator->betround() != kBetroundPreflop) return;
  if (!p_engine_container->symbol_engine_userchair()->userchair_confirmed()) return;
  if (NumberOfCardsPerPlayer() != 2) return;                       // Hold'em only
  if (!p_table_state->User()->HasKnownCards()) return;

  CSymbolEngineActiveDealtPlaying *adp = p_engine_container->symbol_engine_active_dealt_playing();
  const int players = adp->nplayersdealt();
  if (players < 2 || players > 9) return;
  const int hero_chair = p_engine_container->symbol_engine_userchair()->userchair();
  const int hero_idx = ActionIndexOfChair(hero_chair);
  if (hero_idx == kUndefined) return;
  const double bb = BigBlind();
  if (bb <= 0.0) return;

  const int card_a = p_table_state->User()->hole_cards(0)->GetValue();
  const int card_b = p_table_state->User()->hole_cards(1)->GetValue();
  const int hand_class = ExploitEV::HandClass(card_a, card_b);

  // --- stacks: effective stack against the deepest opponent still in the hand ---
  const double hero_bet = p_table_state->User()->_bet.GetValue();
  const double hero_total = p_table_state->User()->_balance.GetValue() + hero_bet;
  double deepest_opponent = 0.0;
  int callers = 0;                                  // opponents who already put in the largest bet
  int limpers = 0;                                  // opponents who limped (nobody raised)
  const int opponents = adp->opponentsplayingbits();
  const double largest = LargestBetOnTable();
  for (int chair = 0; chair < p_tablemap->nchairs(); ++chair) {
    if (!(opponents & (1 << chair))) continue;
    double bet = p_table_state->Player(chair)->_bet.GetValue();
    double total = p_table_state->Player(chair)->_balance.GetValue() + bet;
    if (total > deepest_opponent) deepest_opponent = total;
    if (_raises > 0) {
      bool is_raiser = (chair == _raiser_chair[_raises]);
      if (!is_raiser && bet >= largest * 0.999 && largest > 0.0) ++callers;
    } else if (ActionIndexOfChair(chair) != players - 1 && bet >= bb * 0.99) {
      ++limpers;
    }
  }
  if (deepest_opponent <= 0.0 || hero_total <= 0.0) return;
  const double effective = (hero_total < deepest_opponent) ? hero_total : deepest_opponent;
  _stack_bb = effective / bb;
  _players = players;
  _position = hero_idx;
  const double to_call = p_engine_container->symbol_engine_chip_amounts()->call();
  _stats_reliable = UpdateStatsReliable();

  double call = 0.0, raise = 0.0, raiseto = 0.0;
  const unsigned char* call_table = 0;   // chart of our calling range in this spot
  double standard_bet = 0.0;     // size the chart assumes for the bet we face (chips)
  bool allin = false;
  bool ev_checked = false;       // the EV check replaced the size heuristic

  if (_stack_bb <= ExploitEV::PushFoldMaxStack(players)) {
    // ---------------- push / fold ----------------
    _regime = 1;
    if (_raises == 0) {
      _situation = 1;
      if (to_call <= 0.0) {
        call = 1.0;                                              // BB option: check
      } else {
        raise = ExploitEV::PushFoldPush(players, hero_idx, _stack_bb, hand_class);
        allin = true;
      }
    } else {
      _situation = (_raises == 1) ? 2 : 5;
      int pusher_chair = _raiser_chair[_raises];
      int pusher_idx = ActionIndexOfChair(pusher_chair);
      if (to_call <= 0.0) {
        call = 1.0;                                              // already the biggest bet
      } else if (pusher_idx != kUndefined) {
        call = ExploitEV::PushFoldCall(players, pusher_idx, hero_idx, callers > 0,
                                       _stack_bb, hand_class);
      }
    }
  } else {
    // ---------------- deep-stack charts ----------------
    _regime = 2;
    ExploitEV::ChartSeat seat = ExploitEV::MapSeat(players, hero_idx);
    const ExploitEV::ChartFormat format = seat.format;
    const int pos = seat.position;
    if (_raises == 0) {
      _situation = 1;
      if (to_call <= 0.0) {
        // BB option after limps: raise with the strong part of a late-position range
        const int sb = ExploitEV::ChartNumPositions(format) - 2;
        raise = ExploitEV::ChartFrequency(ExploitEV::ChartRfi(format, sb), hand_class);
        call = 1.0 - raise;
        raiseto = (ExploitEV::ChartOpenSize(format, sb) + limpers) * bb;
      } else {
        const unsigned char* rfi = ExploitEV::ChartRfi(format, pos);
        raise = ExploitEV::ChartFrequency(rfi, hand_class);
        raiseto = (ExploitEV::ChartOpenSize(format, pos) + limpers) * bb;
        // exploit: how often does the big blind fold to a steal?
        const int n = ExploitEV::ChartNumPositions(format);
        if (pos >= n - 4 && limpers == 0) {
          int bb_chair = kUndefined;
          for (int ch = 0; ch < p_tablemap->nchairs(); ++ch) {
            if (ActionIndexOfChair(ch) == players - 1) bb_chair = ch;
          }
          const double factor = ExploitEV::StealRangeFactor(
            bb_chair == kUndefined ? -1.0 : ReadStat("bb_fold_to_steal", bb_chair));
          if (factor > 1.0 && raise < 0.5 && pos + 1 <= n - 2) {
            const double wider = ExploitEV::ChartFrequency(ExploitEV::ChartRfi(format, pos + 1), hand_class);
            if (wider > raise) raise = wider;
          } else if (factor < 1.0 && raise >= 0.5 && ExploitEV::ChartKeepRank(rfi, hand_class) > factor) {
            raise = 0.0;
          }
        }
      }
    } else if (_raises == 1) {
      _situation = 2;
      const int opener = ExploitEV::MapSeat(players, ActionIndexOfChair(_raiser_chair[1])).position;
      if (to_call <= 0.0) {
        call = 1.0;
      } else if (pos > opener) {
        call_table = ExploitEV::ChartVsOpenCall(format, pos, opener);
        const unsigned char* table_3bet = ExploitEV::ChartVsOpen3bet(format, pos, opener);
        call = ExploitEV::ChartFrequency(call_table, hand_class);
        raise = ExploitEV::ChartFrequency(table_3bet, hand_class);
        if (callers > 0) call *= 0.7;                           // squeeze spot: flat less
        const int opener_chair = _raiser_chair[1];
        const int n = ExploitEV::ChartNumPositions(format);
        if (raise < 0.5) {
          // (1) stack depth: shallower stacks, fewer speculative calls
          if (_stack_bb < 60.0 && call > 0.0
              && ExploitEV::ChartKeepRank(call_table, hand_class) > ExploitEV::DepthKeepShare(_stack_bb)) {
            call = 0.0;
          }
          // (2) EV check: equity against his real opening range vs the pot odds
          if (callers == 0 && opener_chair != kUndefined) {
            const double pfr = ReadStat("pfr", opener_chair);
            const unsigned char* rfi = ExploitEV::ChartRfi(format, opener);
            const long long key = (long long)hand_class * 1000003LL + opener_chair * 1009LL
                                + (long long)((pfr < 0.0 ? -1.0 : pfr) * 1000.0 + 1.0);
            if (key != _eq_key) {
              ExploitEV::Range range;
              if (pfr >= 0.0) {
                double fraction = ExploitEV::ShrinkStat(pfr, 50, ExploitEV::ChartPercent(rfi) / 100.0);
                double percent = fraction * 100.0;
                if (percent < 5.0) percent = 5.0;
                if (percent > 60.0) percent = 60.0;
                range = ExploitEV::Range::FromTopPercent(percent);
              } else {
                range = ExploitEV::ChartToRange(rfi);
              }
              bool dead[ExploitEV::kNumCards] = {false};
              dead[card_a] = true; dead[card_b] = true;
              range.RemoveDeadCards(dead);
              std::vector<const ExploitEV::Range*> opponents(1, &range);
              const int hero[2] = {card_a, card_b};
              _eq_value = ExploitEV::Equity(hero, 0, 0, opponents, 3000, 4242);
              _eq_key = key;
            }
            const int hi = hand_class / 13, lo = hand_class % 13;
            const bool in_position = (n >= 3 && pos > opener && pos < n - 2);
            const double realization = ExploitEV::PreflopRealization(
              in_position, pos == n - 1, hi > lo, hi == lo);
            const ExploitEV::CallVerdict verdict = ExploitEV::EvCallVerdict(
              _eq_value, realization, p_engine_container->symbol_engine_chip_amounts()->pot(),
              to_call, call >= 0.5);
            if (verdict == ExploitEV::kForceFold) call = 0.0;
            else if (verdict == ExploitEV::kForceCall) call = 1.0;
            ev_checked = (_eq_value >= 0.0);
          }
          // (3) exploit: he folds a lot / never folds to 3bets
          if (callers == 0 && opener_chair != kUndefined) {
            const ExploitEV::ThreeBetAdjust adjusted = ExploitEV::AdjustThreeBet(
              call, raise, ExploitEV::ChartKeepRank(call_table, hand_class),
              ExploitEV::ChartKeepRank(table_3bet, hand_class), ReadStat("fold_to_3bet", opener_chair));
            call = adjusted.call; raise = adjusted.raise;
          }
        }
        standard_bet = (ExploitEV::ChartOpenSize(format, opener) + limpers) * bb;
        raiseto = (ExploitEV::ChartThreeBetSize(format, pos, opener) + callers * ExploitEV::ChartOpenSize(format, opener)) * bb;
      } else {
        PremiumResponse(hand_class, _stack_bb, &call, &raise);  // we limped and face a raise
        allin = (raise > 0.0);
      }
    } else if (_raises == 2 && _raiser_chair[1] == hero_chair) {
      _situation = 3;
      const int three_bettor = ExploitEV::MapSeat(players, ActionIndexOfChair(_raiser_chair[2])).position;
      if (to_call <= 0.0) {
        call = 1.0;
      } else {
        call_table = ExploitEV::ChartVs3betCall(format, pos, three_bettor);
        call = ExploitEV::ChartFrequency(call_table, hand_class);
        raise = ExploitEV::ChartFrequency(ExploitEV::ChartVs3bet4bet(format, pos, three_bettor), hand_class);
        raiseto = ExploitEV::ChartFourBetSize(format, pos, three_bettor) * bb;
        standard_bet = ExploitEV::ChartThreeBetSize(format, three_bettor, pos) * bb;
      }
    } else if (_raises == 3 && _raiser_chair[2] == hero_chair) {
      _situation = 4;
      const int four_bettor = ExploitEV::MapSeat(players, ActionIndexOfChair(_raiser_chair[3])).position;
      if (to_call <= 0.0) {
        call = 1.0;
      } else if (pos > four_bettor) {
        call_table = ExploitEV::ChartVs4betCall(format, pos, four_bettor);
        call = ExploitEV::ChartFrequency(call_table, hand_class);
        raise = ExploitEV::ChartFrequency(ExploitEV::ChartVs4bet5bet(format, pos, four_bettor), hand_class);
        standard_bet = ExploitEV::ChartFourBetSize(format, pos, four_bettor) * bb;
        allin = true;
      } else {
        PremiumResponse(hand_class, _stack_bb, &call, &raise);
        allin = (raise > 0.0);
      }
    } else {
      _situation = 5;
      if (to_call <= 0.0) call = 1.0;
      else {
        PremiumResponse(hand_class, _stack_bb, &call, &raise);
        allin = (raise > 0.0);
      }
    }
  }

  // --- adapt to the real size of the bet we face (charts assume a standard size) ---
  // bigger than standard: continue less with marginal hands, smaller: continue more.
  // Pure decisions (frequency ~100%) are kept, so premiums never fold to a big bet.
  if (_regime == 2 && !ev_checked && standard_bet > 0.0 && largest > 0.0 && to_call > 0.0) {
    double ratio = largest / standard_bet;
    double factor = (ratio >= 1.0) ? 1.0 / (1.0 + 0.6 * (ratio - 1.0)) : 1.0 + 0.5 * (1.0 - ratio);
    if (factor < 0.35) factor = 0.35;
    if (factor > 1.4) factor = 1.4;
    if (factor < 1.0 && call_table != 0 && call > 0.0 && raise < 0.5) {
      // bigger bet: drop the weakest part of the calling range (keep the top 'factor' share)
      if (ExploitEV::ChartKeepRank(call_table, hand_class) > factor) call = 0.0;
    }
    if (call < 0.99) call = (call * factor < 0.99) ? call * factor : 0.99;
    if (raise < 0.99) raise = (raise * factor < 0.99) ? raise * factor : 0.99;
  }

  // --- normalise the frequencies ---
  if (call < 0.0) call = 0.0;
  if (raise < 0.0) raise = 0.0;
  if (call + raise > 1.0) {
    double scale = 1.0 / (call + raise);
    call *= scale; raise *= scale;
  }
  // --- bet size: all-in if the raise commits a large part of the stack or is not affordable ---
  if (raise > 0.0) {
    if (_regime == 2 && _raises >= 1 && _stack_bb <= ExploitEV::kShoveOverRaiseStack) allin = true;
    if (raiseto > 0.0) {
      // at least a minimum raise
      double minimum = (largest > 0.0) ? 2.0 * largest : 2.0 * bb;
      if (raiseto < minimum) raiseto = minimum;
      if (raiseto >= kCommitShare * hero_total || raiseto >= hero_total) allin = true;
    }
    if (allin) raiseto = hero_total;
  }
  _call = call;
  _raise = raise;
  _fold = 1.0 - call - raise;
  if (_fold < 0.0) _fold = 0.0;
  _raiseto = raiseto;
  _allin = allin && (raise > 0.0);

  // --- draw the action; keep the draw while the situation does not change ---
  const int key = _raises * 100 + _situation * 10 + _regime;
  if (key != _roll_key) {
    _rng_state ^= _rng_state >> 12; _rng_state ^= _rng_state << 25; _rng_state ^= _rng_state >> 27;
    _roll = ((_rng_state * 2685821657736338717ULL) >> 11) * (1.0 / 9007199254740992.0);
    _roll_key = key;
  }
  if (_roll < _fold) _action = 0;
  else if (_roll < _fold + _call) _action = 1;
  else _action = 2;
  _valid = true;
  LogDecision(hand_class, to_call, p_engine_container->symbol_engine_chip_amounts()->pot());
  write_log(Preferences()->debug_symbolengine(),
    "[CSymbolEnginePreflopCharts] regime %d situation %d pos %d/%d stack %.1fbb fold %.2f call %.2f raise %.2f to %.2f allin %d action %d\n",
    _regime, _situation, _position, _players, _stack_bb, _fold, _call, _raise, _raiseto, _allin ? 1 : 0, _action);
}

void CSymbolEnginePreflopCharts::LogDecision(int hand_class, double to_call, double pot) {
  const long long key = ((long long)_raises * 1000 + _situation * 100 + _regime * 10 + hand_class) * 100003LL
                      + (long long)(to_call * 100.0);
  if (key == _last_logged_key) return;                    // log every decision once
  _last_logged_key = key;
  CString line;
  line.Format("pre,%s%s,,%d,%d,%.1f,%.2f,%.2f,%d,%d,%.3f,%.3f,%.3f,%.2f,%d,%d,%d",
    ExploitEV::CardName(p_table_state->User()->hole_cards(0)->GetValue()).c_str(),
    ExploitEV::CardName(p_table_state->User()->hole_cards(1)->GetValue()).c_str(),
    _players, _position, _stack_bb, pot, to_call, _regime, _situation,
    _fold, _call, _raise, _raiseto, _allin ? 1 : 0, _action, _stats_reliable ? 1 : 0);
  ExploitEV::AppendDecisionLog(std::string((const char*)line));
}

// ---------------------------------------------------------------------------
// Opponent ranges derived from the preflop action (used by the postflop EV engine)
// ---------------------------------------------------------------------------
bool CSymbolEnginePreflopCharts::OpponentRange(int chair, ExploitEV::Range *range, double *percent_of_hands) {
  if (range == NULL || chair < 0 || chair > 9 || _raises <= 0) return false;
  const int players = p_engine_container->symbol_engine_active_dealt_playing()->nplayersdealt();
  if (players < 2 || players > 9) return false;
  const int idx = ActionIndexOfChair(chair);
  if (idx == kUndefined) return false;
  const ExploitEV::ChartSeat seat = ExploitEV::MapSeat(players, idx);
  const ExploitEV::ChartFormat format = seat.format;
  const int pos = seat.position;
  auto position_of_raise = [&](int n) -> int {
    if (n < 1 || n > kMaxTrackedRaises || _raiser_chair[n] == kUndefined) return kUndefined;
    int i = ActionIndexOfChair(_raiser_chair[n]);
    return (i == kUndefined) ? kUndefined : ExploitEV::MapSeat(players, i).position;
  };
  // highest raise made by this chair
  int k = 0;
  for (int n = 1; n <= _raises && n <= kMaxTrackedRaises; ++n) if (_raiser_chair[n] == chair) k = n;

  const unsigned char *table = NULL;
  const int opener = position_of_raise(1);
  const int three_bettor = position_of_raise(2);
  const int four_bettor = position_of_raise(3);
  if (k == 1) {
    if (_raises == 1) table = ExploitEV::ChartRfi(format, pos);
    else if (three_bettor != kUndefined) table = ExploitEV::ChartVs3betCall(format, pos, three_bettor);
  } else if (k == 2) {
    if (_raises == 2 && opener != kUndefined) table = ExploitEV::ChartVsOpen3bet(format, pos, opener);
    else if (four_bettor != kUndefined) table = ExploitEV::ChartVs4betCall(format, pos, four_bettor);
  } else if (k == 3) {
    if (three_bettor != kUndefined) table = ExploitEV::ChartVs3bet4bet(format, pos, three_bettor);
  } else if (k >= 4) {
    if (opener != kUndefined) table = ExploitEV::ChartVs4bet5bet(format, pos, opener);
  } else if (_raises == 1 && opener != kUndefined && _preflop_bet[chair] > 0.0) {
    table = ExploitEV::ChartVsOpenCall(format, pos, opener);      // called the open
  }
  if (table == NULL) return false;
  const double percent = ExploitEV::ChartPercent(table);
  if (percent <= 0.0) return false;
  *range = ExploitEV::ChartToRange(table);
  if (percent_of_hands != NULL) *percent_of_hands = percent;
  return true;
}

// ---------------------------------------------------------------------------
bool CSymbolEnginePreflopCharts::EvaluateSymbol(const CString name, double *result, bool log /* = false */) {
  FAST_EXIT_ON_OPENPPL_SYMBOLS(name);
  // Fast exit on other symbols
  if (memcmp(name, "pf_", 3) != 0) return false;
  if (name == "pf_valid")               *result = _valid ? 1.0 : 0.0;
  else if (name == "pf_regime")         *result = _regime;
  else if (name == "pf_situation")      *result = _situation;
  else if (name == "pf_fold")           *result = _fold;
  else if (name == "pf_call")           *result = _call;
  else if (name == "pf_raise")          *result = _raise;
  else if (name == "pf_raiseto")        *result = _raiseto;
  else if (name == "pf_allin")          *result = _allin ? 1.0 : 0.0;
  else if (name == "pf_action")         *result = _action;
  else if (name == "pf_stackbb")        *result = _stack_bb;
  else if (name == "pf_position")       *result = _position;
  else if (name == "pf_players")        *result = _players;
  else if (name == "pf_raises")         *result = _raises;
  else if (name == "pf_statsreliable")  *result = _stats_reliable ? 1.0 : 0.0;
  else if (name == "pf_minhands")       *result = kMinHandsForStats;
  else return false;
  return true;
}

CString CSymbolEnginePreflopCharts::SymbolsProvided() {
  return "pf_valid pf_regime pf_situation pf_fold pf_call pf_raise pf_raiseto pf_allin "
         "pf_action pf_stackbb pf_position pf_players pf_raises pf_statsreliable pf_minhands ";
}
