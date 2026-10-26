//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: analysis of the stats database (no GUI, testable):
//   results graph, reports by position / stakes / table / hour / hand / ...,
//   starting hand matrix, sessions, tournaments, leaks, player types,
//   confidence intervals.
//
//******************************************************************************

#ifndef INC_EXPLORER_REPORTS_H
#define INC_EXPLORER_REPORTS_H

#include <string>
#include <vector>
#include "StatsDb.h"

namespace explorer {

// Which hands are analysed: the hands of one player (else those of the hero)
struct Scope {
  std::string         player;          // "" = hero
  std::string         site;            // "" = all
  int                 game_type;       // -1 = all, 0 = cash, 1 = tournament
  long long           since;           // 0 = all
  ohstats::HandFilter filter;
  Scope() : game_type(0), since(0) {}
};

//------------------------------------------------------------------------------
// Results graph
//------------------------------------------------------------------------------

struct GraphPoint {
  long long time;
  double    total;          // cumulative result in big blinds
  double    showdown;       // ... of the hands that went to showdown
  double    no_showdown;
};

struct Graph {
  std::vector<GraphPoint> points;     // one per hand, oldest first
  long long hands;
  double    net_bb;
  double    bb_per_100;
  double    std_dev_bb;               // standard deviation of the result of one hand, in bb
  double    money;                    // result in money (or chips)
  Graph() : hands(0), net_bb(0), bb_per_100(0), std_dev_bb(0), money(0) {}
  // 95 % confidence interval of the winrate (bb/100)
  double WinrateMargin() const;
};

bool BuildGraph(ohstats::Db *db, const Scope &scope, Graph *graph, std::string *error = NULL);

//------------------------------------------------------------------------------
// Reports
//------------------------------------------------------------------------------

enum ReportKind {
  kReportPosition, kReportStakes, kReportTable, kReportHour, kReportWeekday,
  kReportStartingHand, kReportPotType, kReportStackDepth
};
const char *ReportName(ReportKind kind);

struct ReportRow {
  std::string label;
  std::string key;
  long long   hands;
  double      net;           // money / chips
  double      net_bb;
  double      bb_per_100;
  double      vpip, pfr;     // 0..1, NaN = undefined
  double      sd_win;        // share of showdowns won, NaN
  ReportRow() : hands(0), net(0), net_bb(0), bb_per_100(0), vpip(0), pfr(0), sd_win(0) {}
};

bool BuildReport(ohstats::Db *db, const Scope &scope, ReportKind kind, std::vector<ReportRow> *rows,
                 std::string *error = NULL);

// 13 x 13 matrix: pairs on the diagonal, suited hands above, offsuit below
// (rows and columns: A K Q ... 2)
struct MatrixCell {
  std::string name;          // "AKs"
  long long   hands;
  double      net_bb;
  double      bb_per_100;
  MatrixCell() : hands(0), net_bb(0), bb_per_100(0) {}
};
struct HandMatrix {
  MatrixCell cell[13][13];
};
bool BuildHandMatrix(ohstats::Db *db, const Scope &scope, HandMatrix *matrix, std::string *error = NULL);

//------------------------------------------------------------------------------
// Sessions and tournaments
//------------------------------------------------------------------------------

struct Session {
  long long start, end;
  long long hands;
  double    net, net_bb, bb_per_100;
  double    hands_per_hour;
  Session() : start(0), end(0), hands(0), net(0), net_bb(0), bb_per_100(0), hands_per_hour(0) {}
};
// A new session starts after a pause of more than gap_seconds. Newest first.
bool BuildSessions(ohstats::Db *db, const Scope &scope, long long gap_seconds, std::vector<Session> *sessions,
                   std::string *error = NULL);

struct TournamentRow {
  std::string id;
  long long   start, end;
  long long   hands;
  double      start_stack, final_stack, start_bb, final_bb;
  int         max_level;
  double      net_chips;
  TournamentRow() : start(0), end(0), hands(0), start_stack(0), final_stack(0), start_bb(0), final_bb(0),
                    max_level(1), net_chips(0) {}
};
// Newest first. (Buy-ins and prizes are not in the database, so no ROI / ITM.)
bool BuildTournaments(ohstats::Db *db, const Scope &scope, std::vector<TournamentRow> *rows,
                      std::string *error = NULL);

//------------------------------------------------------------------------------
// Statistics: leaks, player types, confidence intervals
//------------------------------------------------------------------------------

// 95 % Wilson interval of a proportion
void WilsonInterval(double successes, double trials, double *low, double *high);
// False for statistics that are not simple proportions (aggression factors, ...)
bool HasConfidenceInterval(const std::string &stat);

struct LeakRange {
  const char *stat;
  double low, high;          // reference range (proportion)
  const char *too_low;       // advice
  const char *too_high;
};
const std::vector<LeakRange> &LeakRanges();

enum LeakStatus { kLeakOk, kLeakLow, kLeakHigh, kLeakNoData };
struct LeakRow {
  std::string stat;
  double      value, opportunities;
  double      ci_low, ci_high;
  double      range_low, range_high;
  LeakStatus  status;
  std::string advice;
};
// Compares the statistics of a player with the reference ranges. A statistic is only
// judged when the whole confidence interval is outside the range.
bool BuildLeaks(ohstats::Db *db, const std::string &player, const ohstats::StatFilter &filter,
                std::vector<LeakRow> *rows, std::string *error = NULL);

enum PlayerType {
  kTypeUnknown, kTypeNit, kTypeTag, kTypeLag, kTypeManiac, kTypeFish, kTypeCallingStation, kTypePassive
};
// vpip, pfr: proportions, hands: sample size
PlayerType ClassifyPlayer(double vpip, double pfr, long long hands);
const char *PlayerTypeName(PlayerType type);

}  // namespace explorer

#endif  // INC_EXPLORER_REPORTS_H
