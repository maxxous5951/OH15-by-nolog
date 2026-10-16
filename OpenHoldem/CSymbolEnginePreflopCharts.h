//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: hard-coded preflop strategy (no PokerTracker data needed).
//   * short stacks: push/fold (solved Nash charts for HU / 3-handed Spin&Go,
//     approximation for 6-max and 9-max)
//   * deeper stacks: reference charts (open, defend, 3bet, 4bet, 5bet)
//   The maths and the tables live in ..\ExploitEV\PreflopCharts (portable, tested).
//   This class tracks the preflop action (number of raises and who raised),
//   works out the situation of the user and publishes the result as symbols.
//
//   pf_valid         1 if the values below are valid for the current decision
//   pf_regime        1 = push/fold, 2 = deep-stack charts
//   pf_situation     1 = nobody raised (first in / limped pot)
//                    2 = facing one raise      3 = facing a 3bet after we opened
//                    4 = facing a 4bet after we 3bet
//                    5 = anything else (cold 3bet, 5bet, jam)
//   pf_fold          frequency 0..1 to fold   (pf_fold + pf_call + pf_raise = 1)
//   pf_call          frequency 0..1 to call (or check)
//   pf_raise         frequency 0..1 to raise / push
//   pf_raiseto       total chips of our bet if we raise (including chips already in)
//   pf_allin         1 if the raise is an all-in
//   pf_action        action drawn from the frequencies: 0 fold, 1 call/check, 2 raise
//                    (the draw is stable while the situation does not change)
//   pf_stackbb       effective stack in big blinds
//   pf_position      own position in preflop action order among the dealt players
//   pf_players       players dealt in
//   pf_raises        number of raises seen so far preflop
//   pf_statsreliable 1 if PokerTracker is connected and has enough hands on all
//                    opponents (pf_minhands), 0 if the bot should rely on the charts
//
//******************************************************************************

#ifndef INC_CSYMBOLENGINEPREFLOPCHARTS_H
#define INC_CSYMBOLENGINEPREFLOPCHARTS_H

#include "CVirtualSymbolEngine.h"

namespace ExploitEV { class Range; }

class CSymbolEnginePreflopCharts: public CVirtualSymbolEngine {
 public:
  CSymbolEnginePreflopCharts();
  ~CSymbolEnginePreflopCharts();
 public:
  // Mandatory reset-functions
  void InitOnStartup();
  void UpdateOnConnection();
  void UpdateOnHandreset();
  void UpdateOnNewRound();
  void UpdateOnMyTurn();
  void UpdateOnHeartbeat();
 public:
  // Public accessors
  bool EvaluateSymbol(const CString name, double *result, bool log = false);
  CString SymbolsProvided();
 public:
  // Range an opponent most likely has, derived from the chart of his preflop action.
  // Returns false if his role is unknown (limper, check, multiway 3bet pots...).
  bool OpponentRange(int chair, ExploitEV::Range *range, double *percent_of_hands);
  // PokerTracker has enough hands on that player to trust his stats?
  bool StatsReliableForChair(int chair);
 private:
  void ResetDecision();
  void TrackPreflopBets();
  void Calculate();
  double ReadPokerTrackerHands(int chair);
  double ReadStat(const char *name, int chair);          // reliable PokerTracker stat as fraction, -1 if unknown
  void   LogDecision(int hand_class, double to_call, double pot);
  int  ActionIndexOfChair(int chair);
  bool UpdateStatsReliable();
 private:
  // preflop history of the current hand
  enum { kMaxTrackedRaises = 8 };
  int    _raises;
  int    _raiser_chair[kMaxTrackedRaises + 1];   // index 1.. = chair of the n-th raise
  double _previous_largest_bet;
  double _previous_bet[10];
  double _preflop_bet[10];                       // highest bet of each chair preflop
  // decision
  bool   _valid;
  int    _regime;
  int    _situation;
  double _fold, _call, _raise, _raiseto;
  bool   _allin;
  int    _action;
  double _stack_bb;
  int    _position;
  int    _players;
  bool   _stats_reliable;
  // stable random draw
  double _roll;
  long long _last_logged_key;
  long long _eq_key;          // cache of the preflop equity vs the opener's range
  double _eq_value;
  int    _roll_key;
  unsigned long long _rng_state;
};

#endif  // INC_CSYMBOLENGINEPREFLOPCHARTS_H
