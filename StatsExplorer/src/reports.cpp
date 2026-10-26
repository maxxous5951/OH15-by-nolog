//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: analysis of the stats database, see reports.h
//
//******************************************************************************

#include "reports.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <limits>
#include <map>
#include <sstream>

#include "explorer_model.h"

namespace explorer {

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();

// "FROM ... WHERE ..." for the scope, binds in the order of the placeholders
std::string ScopeSql(const Scope &s, std::vector<ohstats::Cell> *binds) {
  const bool by_player = !s.player.empty();
  if (by_player) binds->push_back(ohstats::Cell::Text(s.player));
  std::string sql = ohstats::HandFromSql(by_player) + " WHERE p.id IS NOT NULL";
  if (!s.site.empty())  { sql += " AND h.site = ?"; binds->push_back(ohstats::Cell::Text(s.site)); }
  if (s.game_type >= 0) { sql += " AND h.is_tournament = ?"; binds->push_back(ohstats::Cell::Int(s.game_type)); }
  if (s.since > 0)      { sql += " AND h.played_at >= ?"; binds->push_back(ohstats::Cell::Int(s.since)); }
  sql += ohstats::HandFilterSql(s.filter, binds);
  return sql;
}

const char *kNet = "(p.end_stack - p.start_stack)";
const char *kNetBb = "((p.end_stack - p.start_stack) / h.bblind)";

}  // namespace

//------------------------------------------------------------------------------
// Graph
//------------------------------------------------------------------------------

double Graph::WinrateMargin() const {
  if (hands < 2) return 0;
  return 1.96 * std_dev_bb / sqrt((double)hands) * 100.0;
}

bool BuildGraph(ohstats::Db *db, const Scope &scope, Graph *graph, std::string *error) {
  *graph = Graph();
  std::vector<ohstats::Cell> binds;
  const std::string sql = std::string("SELECT h.played_at, ") + kNetBb + ", h.showdown, " + kNet +
                          ScopeSql(scope, &binds) + " AND h.bblind > 0 ORDER BY h.played_at, h.id;";
  std::vector<ohstats::Row> rows;
  if (!db->Select(sql, binds, &rows, error)) return false;
  double total = 0, showdown = 0, other = 0, sum_squares = 0;
  for (size_t i = 0; i < rows.size(); ++i) {
    const double net_bb = rows[i][1].AsDouble();
    total += net_bb;
    sum_squares += net_bb * net_bb;
    if (rows[i][2].AsInt() != 0) showdown += net_bb; else other += net_bb;
    graph->money += rows[i][3].AsDouble();
    GraphPoint p;
    p.time = rows[i][0].AsInt();
    p.total = total; p.showdown = showdown; p.no_showdown = other;
    graph->points.push_back(p);
  }
  graph->hands = (long long)rows.size();
  graph->net_bb = total;
  if (graph->hands > 0) {
    const double n = (double)graph->hands, mean = total / n;
    graph->bb_per_100 = mean * 100.0;
    graph->std_dev_bb = n > 1 ? sqrt(std::max(0.0, (sum_squares - n * mean * mean) / (n - 1))) : 0;
  }
  return true;
}

//------------------------------------------------------------------------------
// Reports
//------------------------------------------------------------------------------

const char *ReportName(ReportKind kind) {
  switch (kind) {
    case kReportPosition:     return "Position";
    case kReportStakes:       return "Stakes";
    case kReportTable:        return "Table";
    case kReportHour:         return "Hour of the day";
    case kReportWeekday:      return "Day of the week";
    case kReportStartingHand: return "Starting hand";
    case kReportPotType:      return "Pot type";
    case kReportStackDepth:   return "Stack depth";
  }
  return "";
}

static std::string ReportLabel(ReportKind kind, const ohstats::Cell &key) {
  char buffer[64];
  switch (kind) {
    case kReportPosition:   return key.IsNull() ? "?" : PositionName((int)key.AsInt());
    case kReportStakes:     snprintf(buffer, sizeof(buffer), "BB %g", key.AsDouble()); return buffer;
    case kReportHour:       snprintf(buffer, sizeof(buffer), "%02d:00", (int)key.AsInt()); return buffer;
    case kReportWeekday: {
      static const char *days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
      int d = (int)key.AsInt();
      return (d >= 0 && d < 7) ? days[d] : "?";
    }
    case kReportPotType: {
      static const char *types[] = { "?", "Limped", "Single raised", "3-bet pot", "4-bet+ pot" };
      int t = (int)key.AsInt();
      return (t >= 0 && t < 5) ? types[t] : "?";
    }
    case kReportStackDepth: {
      static const char *buckets[] = { "< 10 bb", "10 - 20 bb", "20 - 40 bb", "40 - 100 bb", "100+ bb" };
      int t = (int)key.AsInt();
      return (t >= 0 && t < 5) ? buckets[t] : "?";
    }
    default: return key.IsNull() ? "?" : key.AsText();
  }
}

bool BuildReport(ohstats::Db *db, const Scope &scope, ReportKind kind, std::vector<ReportRow> *out,
                 std::string *error) {
  out->clear();
  std::string group;
  switch (kind) {
    case kReportPosition:     group = "p.position"; break;
    case kReportStakes:       group = "h.bblind"; break;
    case kReportTable:        group = "h.table_name"; break;
    case kReportHour:         group = "CAST(strftime('%H', h.played_at, 'unixepoch', 'localtime') AS INTEGER)"; break;
    case kReportWeekday:      group = "CAST(strftime('%w', h.played_at, 'unixepoch', 'localtime') AS INTEGER)"; break;
    case kReportStartingHand: group = "p.hole_class"; break;
    case kReportPotType:      group = "h.pot_type"; break;
    case kReportStackDepth:
      group = "CASE WHEN p.start_stack / h.bblind < 10 THEN 0 WHEN p.start_stack / h.bblind < 20 THEN 1"
              " WHEN p.start_stack / h.bblind < 40 THEN 2 WHEN p.start_stack / h.bblind < 100 THEN 3 ELSE 4 END";
      break;
  }
  std::vector<ohstats::Cell> binds;
  const std::string sql = "SELECT " + group + " AS k, COUNT(*), SUM" + kNet + ", SUM" + kNetBb +
    ", SUM(p.vpip) * 1.0 / COUNT(*), SUM(p.pfr) * 1.0 / NULLIF(SUM(p.pfr_opp), 0),"
    " SUM(CASE WHEN h.showdown = 1 AND p.folded = 0 AND p.end_stack > p.start_stack THEN 1 ELSE 0 END) * 1.0"
    " / NULLIF(SUM(CASE WHEN h.showdown = 1 AND p.folded = 0 THEN 1 ELSE 0 END), 0)" +
    ScopeSql(scope, &binds) + " AND h.bblind > 0" +
    (kind == kReportStartingHand ? " AND p.hole_class IS NOT NULL" : "") +
    " GROUP BY k ORDER BY k;";
  std::vector<ohstats::Row> rows;
  if (!db->Select(sql, binds, &rows, error)) return false;
  for (size_t i = 0; i < rows.size(); ++i) {
    ReportRow r;
    r.key = rows[i][0].AsText();
    r.label = ReportLabel(kind, rows[i][0]);
    r.hands = rows[i][1].AsInt();
    r.net = rows[i][2].AsDouble();
    r.net_bb = rows[i][3].AsDouble();
    r.bb_per_100 = r.hands > 0 ? r.net_bb / (double)r.hands * 100.0 : 0;
    r.vpip = rows[i][4].IsNull() ? kNaN : rows[i][4].AsDouble();
    r.pfr = rows[i][5].IsNull() ? kNaN : rows[i][5].AsDouble();
    r.sd_win = rows[i][6].IsNull() ? kNaN : rows[i][6].AsDouble();
    out->push_back(r);
  }
  if (kind == kReportPosition) {   // natural order: EP CO BTN SB BB
    // already ordered by the position number, with "?" (NULL) first: move it to the end
    std::stable_sort(out->begin(), out->end(), [](const ReportRow &a, const ReportRow &b) {
      return (a.label == "?") < (b.label == "?");
    });
  }
  if (kind == kReportStartingHand) {   // strongest first (by ranks), pairs, suited, offsuit
    std::sort(out->begin(), out->end(), [](const ReportRow &a, const ReportRow &b) {
      static const char kRanks[] = "AKQJT98765432";
      auto rank = [](char c) { const char *p = strchr(kRanks, c); return p ? (int)(p - kRanks) : 99; };
      int ar = rank(a.label[0]), br = rank(b.label[0]);
      if (ar != br) return ar < br;
      return rank(a.label[1]) < rank(b.label[1]);
    });
  }
  return true;
}

bool BuildHandMatrix(ohstats::Db *db, const Scope &scope, HandMatrix *matrix, std::string *error) {
  static const char kRanks[] = "AKQJT98765432";
  for (int r = 0; r < 13; ++r) for (int c = 0; c < 13; ++c) {
    MatrixCell &cell = matrix->cell[r][c];
    cell = MatrixCell();
    // rows / columns in the order A..2: pairs on the diagonal, suited above it, offsuit below it
    char hi = kRanks[std::min(r, c)], lo = kRanks[std::max(r, c)];
    cell.name = std::string(1, hi) + lo + (r == c ? "" : (c > r ? "s" : "o"));
  }
  std::vector<ReportRow> rows;
  if (!BuildReport(db, scope, kReportStartingHand, &rows, error)) return false;
  for (size_t i = 0; i < rows.size(); ++i) {
    for (int r = 0; r < 13; ++r) for (int c = 0; c < 13; ++c) {
      MatrixCell &cell = matrix->cell[r][c];
      if (cell.name == rows[i].label) {
        cell.hands = rows[i].hands;
        cell.net_bb = rows[i].net_bb;
        cell.bb_per_100 = rows[i].bb_per_100;
      }
    }
  }
  return true;
}

//------------------------------------------------------------------------------
// Sessions and tournaments
//------------------------------------------------------------------------------

bool BuildSessions(ohstats::Db *db, const Scope &scope, long long gap, std::vector<Session> *out,
                   std::string *error) {
  out->clear();
  std::vector<ohstats::Cell> binds;
  const std::string sql = std::string("SELECT h.played_at, ") + kNet + ", " + kNetBb + ScopeSql(scope, &binds) +
                          " AND h.bblind > 0 ORDER BY h.played_at, h.id;";
  std::vector<ohstats::Row> rows;
  if (!db->Select(sql, binds, &rows, error)) return false;
  Session current;
  for (size_t i = 0; i < rows.size(); ++i) {
    const long long t = rows[i][0].AsInt();
    if (current.hands > 0 && t - current.end > gap) { out->push_back(current); current = Session(); }
    if (current.hands == 0) current.start = t;
    current.end = t;
    ++current.hands;
    current.net += rows[i][1].AsDouble();
    current.net_bb += rows[i][2].AsDouble();
  }
  if (current.hands > 0) out->push_back(current);
  for (size_t i = 0; i < out->size(); ++i) {
    Session &s = (*out)[i];
    s.bb_per_100 = s.net_bb / (double)s.hands * 100.0;
    const double hours = (double)(s.end - s.start) / 3600.0;
    s.hands_per_hour = hours > 0.0 ? (double)s.hands / hours : 0.0;
  }
  std::reverse(out->begin(), out->end());
  return true;
}

bool BuildTournaments(ohstats::Db *db, const Scope &scope_in, std::vector<TournamentRow> *out,
                      std::string *error) {
  out->clear();
  Scope scope = scope_in;
  scope.game_type = 1;
  std::vector<ohstats::Cell> binds;
  const std::string sql = "SELECT h.tournament_id, h.played_at, p.start_stack, p.end_stack, h.bblind, h.level" +
                          ScopeSql(scope, &binds) + " ORDER BY h.played_at, h.id;";
  std::vector<ohstats::Row> rows;
  if (!db->Select(sql, binds, &rows, error)) return false;
  std::map<std::string, size_t> index;
  for (size_t i = 0; i < rows.size(); ++i) {
    const std::string id = rows[i][0].AsText();
    std::map<std::string, size_t>::iterator it = index.find(id);
    if (it == index.end()) {
      TournamentRow t;
      t.id = id;
      t.start = rows[i][1].AsInt();
      t.start_stack = rows[i][2].AsDouble();
      t.start_bb = rows[i][2].AsDouble() / std::max(1.0, rows[i][4].AsDouble());
      index[id] = out->size();
      out->push_back(t);
      it = index.find(id);
    }
    TournamentRow &t = (*out)[it->second];
    t.end = rows[i][1].AsInt();
    ++t.hands;
    t.final_stack = rows[i][3].AsDouble();
    t.final_bb = rows[i][3].AsDouble() / std::max(1.0, rows[i][4].AsDouble());
    t.max_level = std::max(t.max_level, (int)rows[i][5].AsInt());
    t.net_chips = t.final_stack - t.start_stack;
  }
  std::sort(out->begin(), out->end(), [](const TournamentRow &a, const TournamentRow &b) { return a.end > b.end; });
  return true;
}

//------------------------------------------------------------------------------
// Statistics
//------------------------------------------------------------------------------

void WilsonInterval(double successes, double trials, double *low, double *high) {
  if (trials <= 0) { *low = 0; *high = 1; return; }
  const double z = 1.96, p = successes / trials, z2 = z * z;
  const double denominator = 1 + z2 / trials;
  const double center = (p + z2 / (2 * trials)) / denominator;
  const double margin = z * sqrt(p * (1 - p) / trials + z2 / (4 * trials * trials)) / denominator;
  *low = std::max(0.0, center - margin);
  *high = std::min(1.0, center + margin);
}

bool HasConfidenceInterval(const std::string &stat) {
  if (stat == "hands" || stat == "4bet_plus_ratio") return false;
  return KindOf(stat) == kKindPercent;
}

const std::vector<LeakRange> &LeakRanges() {
  // Approximate reference ranges of a solid regular, 6-max no-limit cash game
  static const LeakRange kRanges[] = {
    { "vpip",             0.20, 0.28, "Too tight: you fold playable hands, especially on the button and cut-off.",
                                      "Too loose: tighten your ranges, especially in early position." },
    { "pfr",              0.15, 0.24, "Too passive preflop: raise more instead of calling.",
                                      "Raising too often: check your early-position ranges." },
    { "rfi",              0.14, 0.24, "Opening too rarely when first in.", "Opening too much when first in." },
    { "steal_attempt",    0.28, 0.45, "Stealing too rarely from CO, BTN and SB.", "Stealing too much: you get 3-bet more." },
    { "3bet",             0.05, 0.10, "3-betting too rarely: you are easy to exploit.", "3-betting too often." },
    { "fold_to_3bet",     0.45, 0.65, "Folding too little to 3-bets: you pay off too much.",
                                      "Folding too much to 3-bets: you can be exploited." },
    { "bb_fold_to_steal", 0.50, 0.75, "Defending the big blind too much against steals.",
                                      "Folding the big blind too much against steals." },
    { "flop_cbet",        0.55, 0.75, "Continuation bet too rarely.", "Continuation bet too often." },
    { "flop_fold_to_cbet", 0.40, 0.55, "Folding too little to flop c-bets.", "Folding too much to flop c-bets." },
    { "turn_cbet",        0.40, 0.60, "Giving up too often on the turn.", "Barreling the turn too often." },
    { "wtsd",             0.24, 0.32, "Going to showdown too rarely: you fold too much postflop.",
                                      "Going to showdown too often: too many calls." },
    { "wssd",             0.50, 0.57, "Winning too little at showdown: calling too wide.",
                                      "Winning very often at showdown: you may be folding too much." },
  };
  static const std::vector<LeakRange> ranges(kRanges, kRanges + sizeof(kRanges) / sizeof(kRanges[0]));
  return ranges;
}

bool BuildLeaks(ohstats::Db *db, const std::string &player, const ohstats::StatFilter &filter,
                std::vector<LeakRow> *out, std::string *error) {
  out->clear();
  if (player.empty()) { if (error) *error = "no player"; return false; }
  const std::vector<LeakRange> &ranges = LeakRanges();
  for (size_t i = 0; i < ranges.size(); ++i) {
    LeakRow row;
    row.stat = ranges[i].stat;
    row.range_low = ranges[i].low; row.range_high = ranges[i].high;
    row.value = kNaN; row.opportunities = 0; row.ci_low = 0; row.ci_high = 1;
    row.status = kLeakNoData;
    double num = 0, den = 0;
    if (db->GetStatFraction(row.stat, player, filter, &num, &den) && den >= 1) {
      row.opportunities = den;
      row.value = num / den;
      WilsonInterval(num, den, &row.ci_low, &row.ci_high);
      // only a clear deviation counts: the whole interval is outside the range
      if (row.ci_high < ranges[i].low)       { row.status = kLeakLow;  row.advice = ranges[i].too_low; }
      else if (row.ci_low > ranges[i].high)  { row.status = kLeakHigh; row.advice = ranges[i].too_high; }
      else                                   { row.status = kLeakOk; }
    }
    out->push_back(row);
  }
  return true;
}

PlayerType ClassifyPlayer(double vpip, double pfr, long long hands) {
  if (hands < 30 || vpip != vpip) return kTypeUnknown;
  const double v = vpip * 100.0, p = (pfr != pfr) ? 0.0 : pfr * 100.0;
  if (v >= 40 && p >= 30) return kTypeManiac;
  if (v >= 30 && p < 12)  return kTypeCallingStation;
  if (v >= 30 && p < 20)  return kTypeFish;
  if (v < 14)             return kTypeNit;
  if (p / v >= 0.65)      return v <= 25 ? kTypeTag : kTypeLag;
  return kTypePassive;
}

const char *PlayerTypeName(PlayerType type) {
  switch (type) {
    case kTypeNit: return "Nit";
    case kTypeTag: return "TAG";
    case kTypeLag: return "LAG";
    case kTypeManiac: return "Maniac";
    case kTypeFish: return "Fish";
    case kTypeCallingStation: return "Calling station";
    case kTypePassive: return "Passive";
    default: return "?";
  }
}

}  // namespace explorer
