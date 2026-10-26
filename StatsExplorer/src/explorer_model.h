//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Model of the stats explorer (no GUI code, testable):
//   filters, player table with sorting, hand list, selected hand, replayer.
//
//******************************************************************************

#ifndef INC_EXPLORER_MODEL_H
#define INC_EXPLORER_MODEL_H

#include <map>
#include <string>
#include <vector>
#include "StatsDb.h"
#include "equity.h"
#include "reports.h"

namespace explorer {

//------------------------------------------------------------------------------
// Statistics: display
//------------------------------------------------------------------------------

enum StatKind { kKindPercent, kKindCount, kKindRatio };

StatKind    KindOf(const std::string &stat);
// "-" for undefined values (NaN)
std::string FormatStat(const std::string &stat, double value);
// Short column title: "vpip" -> "VPIP", "3bet_vs_steal" -> "3Bet vs Steal"
std::string StatLabel(const std::string &stat);
bool        IsUndefined(double value);

struct Section {
  std::string              title;
  std::vector<std::string> stats;
};
// Sections of the player sheet. The last section ("Other") holds every statistic
// that is not in one of the others, so nothing is missing.
const std::vector<Section> &Sections();
// Columns of the player table at startup
const std::vector<std::string> &DefaultColumns();
// Column presets: "General", "Preflop", "Postflop", "Sizes"
const std::vector<std::string> &PresetNames();
std::vector<std::string> PresetColumns(const std::string &name);

//------------------------------------------------------------------------------
// Amounts, positions
//------------------------------------------------------------------------------

std::string FormatAmount(double value, bool tournament);   // 12.5 -> "12.50", chips -> "1250"
const char *PositionName(int position);                    // "BTN", "SB", ...
std::string FormatDate(long long seconds_since_1970);      // "2026-10-25 19:24"

//------------------------------------------------------------------------------
// Replayer
//------------------------------------------------------------------------------

struct ReplayFrame {
  double      pot;
  double      stack[hh::kMaxSeats];
  double      bet[hh::kMaxSeats];        // bet of the current street
  bool        folded[hh::kMaxSeats];
  int         board_cards;
  int         street;                    // 1 = preflop .. 4 = river
  int         actor;                     // seat of the last action, -1 = none
  std::string text;                      // "P3 raises to 3.00"
  ReplayFrame() : pot(0), board_cards(0), street(1), actor(-1) {
    for (int i = 0; i < hh::kMaxSeats; ++i) { stack[i] = bet[i] = 0; folded[i] = false; }
  }
};

// Frame 0: before the first action (antes taken), frames 1..n: after each action,
// last frame: result (uncalled bet returned, pot collected).
std::vector<ReplayFrame> BuildReplay(const hh::Hand &hand);

//------------------------------------------------------------------------------
// Explorer
//------------------------------------------------------------------------------

struct Filters {
  int  site_index;       // 0 = all sites, else index in Explorer::sites + 1
  int  game_type;        // 0 = cash, 1 = tournament, 2 = both
  int  min_hands;
  int  last_hands;       // 0 = all
  int  period_index;     // see PeriodNames()
  int  position;         // -1 = all, else ohstats::Position
  std::string name;      // contains
  Filters() : site_index(0), game_type(0), min_hands(1), last_hands(0), period_index(0), position(-1) {}
};

const std::vector<std::string> &PeriodNames();     // "All time", "Last 24 hours", ...
long long PeriodSeconds(int period_index);         // 0 = no limit

class Explorer {
 public:
  Explorer();
  bool Open(const std::string &path, std::string *error);
  bool IsOpen() { return db.IsOpen(); }

  // Reload from the database with the current filters
  void RefreshPlayers();
  void RefreshHands();
  void SortPlayers();
  void SelectPlayer(const std::string &name);       // "" = nobody (hands of the hero)
  bool SelectHand(long long pk);
  // Value of one statistic of the selected player (sheet), NaN = undefined
  double SheetValue(const std::string &stat);
  bool   AddColumn(const std::string &stat);
  void   RemoveColumn(const std::string &stat);
  void   SetColumns(const std::vector<std::string> &stats);
  bool   HasColumn(const std::string &stat) const;
  // The explorer reloads when the database was changed by another process
  // (OpenHoldem is recording hands): hand count differs from the last refresh.
  bool   DatabaseChanged();

  ohstats::StatFilter MakeStatFilter() const;
  // The hands that are analysed (graph, reports): the selected player, else the hero
  Scope MakeScope() const;
  // Analyses (recomputed when the scope or the data changed)
  void UpdateGraph();
  void UpdateReport();
  void UpdateSessions();
  void UpdateTournaments();
  void UpdateLeaks();
  // Sample size of a statistic of the selected player: opportunities
  bool SheetFraction(const std::string &stat, double *num, double *den);
  PlayerType TypeOf(const std::string &player) const;
  // Notes of the selected hand
  void SaveNote();
  // Equity of the players in the current replay frame (NaN = unknown cards)
  void UpdateEquity();
  // Clears all hand filters
  void ClearHandFilter();
  long long Since() const;

  ohstats::Db                    db;
  std::string                    path, error;
  std::vector<std::string>       sites;
  Filters                        filters;
  std::vector<std::string>       columns;           // statistics of the table
  std::vector<ohstats::PlayerRow> players;
  int                            sort_column;       // -2 = name, -1 = hands, -3 = won, >= 0 = columns[i]
  bool                           sort_descending;
  std::string                    selected_player;
  std::vector<ohstats::HandRow>  hands;
  long long                      hands_total;
  int                            hands_page;
  static const int               kHandsPerPage = 100;
  long long                      selected_hand;     // pk, 0 = none
  hh::Hand                       hand;
  std::string                    hand_site;
  std::string                    hand_text;
  std::vector<ReplayFrame>       replay;
  int                            replay_step;
  long long                      total_hands_in_db;
  // Hand filters (hand list, graph, reports)
  ohstats::HandFilter            hand_filter;
  // Analyses
  Graph                          graph;
  ReportKind                     report_kind;
  std::vector<ReportRow>         report;
  HandMatrix                     matrix;
  bool                           report_as_matrix;
  std::vector<Session>           sessions;
  int                            session_gap_minutes;
  std::vector<TournamentRow>     tournaments;
  std::vector<LeakRow>           leaks;
  std::map<std::string, PlayerType> player_types;
  // Notes of the selected hand
  bool                           note_marked;
  std::string                    note_tags, note_text;
  // Equity of the current replay frame, per seat (NaN: unknown)
  double                         equity[hh::kMaxSeats];
  bool                           equity_exact;
  std::string                    pot_odds_text;
  bool                           players_dirty, hands_dirty;
 private:
  std::string                    analysis_key_[5];
  std::string                    equity_key_;
  std::map<std::string, std::pair<double,double> > fraction_cache_;
  std::string                    sheet_key_;
  std::map<std::string, double>  sheet_cache_;
};

}  // namespace explorer

#endif  // INC_EXPLORER_MODEL_H
