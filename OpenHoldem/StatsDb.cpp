//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: SQLite database of the OpenHoldem stats, see StatsDb.h
//
//******************************************************************************

#include "StatsDb.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <limits>
#include <sstream>

#include "sqlite3.h"

namespace ohstats {

//------------------------------------------------------------------------------
// Statistic definitions: value = (numerator) / (denominator), aggregated over
// the selected hand_player rows. "COUNT(*)" = number of hands.
// The names are the PokerTracker-names, so the existing pt_-symbols work.
//------------------------------------------------------------------------------

namespace {

struct StatDef {
  const char *name;
  const char *num;
  const char *den;
};

const StatDef kStats[] = {
  { "hands",                 "COUNT(*)",                                "1" },
  // General
  { "vpip",                  "SUM(vpip)",                               "COUNT(*)" },
  { "vpip_L",                "SUM(vpip)",                               "COUNT(*)" },
  { "pfr",                   "SUM(pfr)",                                "SUM(pfr_opp)" },
  { "pfr_L",                 "SUM(pfr)",                                "SUM(pfr_opp)" },
  { "rfi",                   "SUM(rfi)",                                "SUM(rfi_opp)" },
  { "wtsd",                  "SUM(wtsd)",                               "SUM(seen_flop)" },
  { "wssd",                  "SUM(wssd)",                               "SUM(wtsd)" },
  // Preflop
  { "steal_attempt",         "SUM(steal)",                              "SUM(steal_opp)" },
  { "bb_fold_to_steal",      "SUM(bb_fold_steal)",                      "SUM(bb_steal_opp)" },
  { "sb_fold_to_steal",      "SUM(sb_fold_steal)",                      "SUM(sb_steal_opp)" },
  { "3bet_vs_steal",         "SUM(three_bet_steal)",                    "SUM(three_bet_steal_opp)" },
  { "bb_3bet_vs_steal",      "SUM(bb_3bet_steal)",                      "SUM(bb_3bet_steal_opp)" },
  { "sb_3bet_vs_steal",      "SUM(sb_3bet_steal)",                      "SUM(sb_3bet_steal_opp)" },
  { "preflop_called_raise",  "SUM(called_raise)",                       "SUM(called_raise_opp)" },
  { "3bet",                  "SUM(three_bet)",                          "SUM(three_bet_opp)" },
  { "fold_to_3bet",          "SUM(fold_3bet)",                          "SUM(fold_3bet_opp)" },
  { "fold_to_3bet_afterR",   "SUM(fold_3bet_r)",                        "SUM(fold_3bet_r_opp)" },
  { "fold_to_resteal",       "SUM(fold_resteal)",                       "SUM(fold_resteal_opp)" },
  { "4bet",                  "SUM(four_bet)",                           "SUM(four_bet_opp)" },
  { "fold_to_4bet",          "SUM(fold_4bet)",                          "SUM(fold_4bet_opp)" },
  { "preflop_squeeze",       "SUM(squeeze)",                            "SUM(squeeze_opp)" },
  // Aggression factor (bets+raises / calls) and frequency (bets+raises / all actions)
  { "flop_af",               "SUM(flop_br)",                            "SUM(flop_call)" },
  { "turn_af",               "SUM(turn_br)",                            "SUM(turn_call)" },
  { "river_af",              "SUM(river_br)",                           "SUM(river_call)" },
  { "total_af",              "SUM(flop_br+turn_br+river_br)",           "SUM(flop_call+turn_call+river_call)" },
  { "flop_afq",              "SUM(flop_br)",                            "SUM(flop_br+flop_call+flop_fold)" },
  { "turn_afq",              "SUM(turn_br)",                            "SUM(turn_br+turn_call+turn_fold)" },
  { "river_afq",             "SUM(river_br)",                           "SUM(river_br+river_call+river_fold)" },
  { "total_afq",             "SUM(flop_br+turn_br+river_br)",           "SUM(flop_br+flop_call+flop_fold+turn_br+turn_call+turn_fold+river_br+river_call+river_fold)" },
  // Streets seen
  { "flop_seen",             "SUM(seen_flop)",                          "COUNT(*)" },
  { "turn_seen",             "SUM(seen_turn)",                          "COUNT(*)" },
  { "river_seen",            "SUM(seen_river)",                         "COUNT(*)" },
  // Continuation bets
  { "flop_cbet",             "SUM(flop_cbet)",                          "SUM(flop_cbet_opp)" },
  { "flop_fold_to_cbet",     "SUM(flop_fold_cbet)",                     "SUM(flop_cbet_faced)" },
  { "flop_raise_cbet",       "SUM(flop_raise_cbet)",                    "SUM(flop_cbet_faced)" },
  { "turn_cbet",             "SUM(turn_cbet)",                          "SUM(turn_cbet_opp)" },
  { "turn_fold_to_cbet",     "SUM(turn_fold_cbet)",                     "SUM(turn_cbet_faced)" },
  { "river_fold_to_cbet",    "SUM(river_fold_cbet)",                    "SUM(river_cbet_faced)" },
  // Other postflop
  { "flop_checkraise",       "SUM(flop_cr)",                            "SUM(flop_cr_opp)" },
  { "turn_checkraise",       "SUM(turn_cr)",                            "SUM(turn_cr_opp)" },
  { "turn_checkcall",        "SUM(turn_cc)",                            "SUM(turn_cr_opp)" },
  { "flop_donkbet",          "SUM(flop_donk)",                          "SUM(flop_donk_opp)" },
  { "river_bet",             "SUM(river_bet)",                          "SUM(river_bet_opp)" },
  // Pots with a 3-bet, defence against squeezes, check-raise, 3-bet on later streets, floats
  { "4bet_plus_ratio",                    "SUM(pfr)*SUM(four_bet_r)",                 "SUM(pfr_opp)*SUM(four_bet_r_opp)" },
  { "fold_to_pf_squeeze",                 "SUM(sq_def_fold_r+sq_def_fold_c)",         "SUM(sq_def_opp_r+sq_def_opp_c)" },
  { "fold_to_squeeze_as_raiser",          "SUM(sq_def_fold_r)",                       "SUM(sq_def_opp_r)" },
  { "fold_to_squeeze_as_caller",          "SUM(sq_def_fold_c)",                       "SUM(sq_def_opp_c)" },
  { "cbet_flop_in_3bet_pot",              "SUM(flop_cbet_p3p)",                       "SUM(flop_cbet_opp_p3p)" },
  { "fold_to_f_cbet_in_3bet_pot",         "SUM(flop_fold_cbet_p3p)",                  "SUM(flop_cbet_faced_p3p)" },
  { "donk_flop_in_3bet_plus_pot",         "SUM(flop_donk_p3p)",                       "SUM(flop_donk_opp_p3p)" },
  { "flop_check_raise_in_3bet_pot",       "SUM(flop_cr_p3)",                          "SUM(flop_cr_opp_p3)" },
  { "flop_check_raise_in_3bet_plus_pot",  "SUM(flop_cr_p3p)",                         "SUM(flop_cr_opp_p3p)" },
  { "fold_to_f_check_raise",              "SUM(flop_fcr)",                            "SUM(flop_fcr_opp)" },
  { "fold_to_f_check_raise_3b_plus_pot",  "SUM(flop_fcr_p3p)",                        "SUM(flop_fcr_opp_p3p)" },
  { "flop_fold_to_3bet",                  "SUM(flop_f3)",                             "SUM(flop_f3_opp)" },
  { "turn_fold_to_3bet",                  "SUM(turn_f3)",                             "SUM(turn_f3_opp)" },
  { "river_fold_to_3bet",                 "SUM(river_f3)",                            "SUM(river_f3_opp)" },
  { "postflop_fold_to_3bet",              "SUM(flop_f3+turn_f3+river_f3)",            "SUM(flop_f3_opp+turn_f3_opp+river_f3_opp)" },
  { "float_flop",                         "SUM(float_flop)",                          "SUM(float_flop_opp)" },
  { "flop_float",                         "SUM(float_flop)",                          "SUM(float_flop_opp)" },
  { "float_flop_in_3bet_pot",             "SUM(float3_flop)",                         "SUM(float3_flop_opp)" },
  { "call_f_float_bet",                   "SUM(fd_call_sr)",                          "SUM(fd_opp_sr)" },
  { "call_f_float_bet_in_3b_pot",         "SUM(fd_call_p3)",                          "SUM(fd_opp_p3)" },
  { "fold_to_f_float_bet_in_3b_pot",      "SUM(fd_fold_p3)",                          "SUM(fd_opp_p3)" },
  { "fold_to_f_float_bet_in_3b_plus_pot", "SUM(fd_fold_p3p)",                         "SUM(fd_opp_p3p)" },
  { "cbet_flop_opp_in_3bet_pot",          "SUM(flop_cbet_opp_p3p)",                   "1" },
  { "fold_to_f_cbet_in_3bet_plus_pot_opp", "SUM(flop_cbet_faced_p3p)",                 "1" },
  { "fold_to_f_check_raise_opp",          "SUM(flop_fcr_opp)",                        "1" },
  { "fold_to_squeeze_as_raiser_opp",      "SUM(sq_def_opp_r)",                        "1" },
  // By size of the open raise (2bb / 2.5bb / 3bb) and BB against the small blind
  { "rfi_2bb",                        "SUM(rfi_s2)",                          "SUM(rfi_opp)" },
  { "rfi_25bb",                       "SUM(rfi_s25)",                         "SUM(rfi_opp)" },
  { "rfi_3bb",                        "SUM(rfi_s3)",                          "SUM(rfi_opp)" },
  { "steal_attempt_2bb",              "SUM(steal_s2)",                        "SUM(steal_opp)" },
  { "steal_attempt_25bb",             "SUM(steal_s25)",                       "SUM(steal_opp)" },
  { "steal_attempt_3bb",              "SUM(steal_s3)",                        "SUM(steal_opp)" },
  { "3bet_vs_2bb",                    "SUM(three_bet_s2)",                    "SUM(three_bet_opp_s2)" },
  { "3bet_vs_25bb",                   "SUM(three_bet_s25)",                   "SUM(three_bet_opp_s25)" },
  { "3bet_vs_3bb",                    "SUM(three_bet_s3)",                    "SUM(three_bet_opp_s3)" },
  { "3bet_vs_steal_2bb",              "SUM(three_bet_steal_s2)",              "SUM(three_bet_steal_opp_s2)" },
  { "3bet_vs_steal_25bb",             "SUM(three_bet_steal_s25)",             "SUM(three_bet_steal_opp_s25)" },
  { "3bet_vs_steal_3bb",              "SUM(three_bet_steal_s3)",              "SUM(three_bet_steal_opp_s3)" },
  { "bb_3bet_vs_steal_2bb",           "SUM(bb_3bet_steal_s2)",                "SUM(bb_3bet_steal_opp_s2)" },
  { "bb_3bet_vs_steal_25bb",          "SUM(bb_3bet_steal_s25)",               "SUM(bb_3bet_steal_opp_s25)" },
  { "bb_3bet_vs_steal_3bb",           "SUM(bb_3bet_steal_s3)",                "SUM(bb_3bet_steal_opp_s3)" },
  { "sb_3bet_vs_steal_2bb",           "SUM(sb_3bet_steal_s2)",                "SUM(sb_3bet_steal_opp_s2)" },
  { "sb_3bet_vs_steal_25bb",          "SUM(sb_3bet_steal_s25)",               "SUM(sb_3bet_steal_opp_s25)" },
  { "sb_3bet_vs_steal_3bb",           "SUM(sb_3bet_steal_s3)",                "SUM(sb_3bet_steal_opp_s3)" },
  { "bb_3bet_vs_sb",                  "SUM(vs_sb_3bet)",                      "SUM(vs_sb_opp)" },
  { "bb_fold_vs_sb",                  "SUM(vs_sb_fold)",                      "SUM(vs_sb_opp)" },
  { "bb_call_vs_sb",                  "SUM(vs_sb_call)",                      "SUM(vs_sb_opp)" },
  { "bb_3bet_vs_sb_2bb",              "SUM(vs_sb_3bet_s2)",                   "SUM(vs_sb_opp_s2)" },
  { "bb_3bet_vs_sb_25bb",             "SUM(vs_sb_3bet_s25)",                  "SUM(vs_sb_opp_s25)" },
  { "bb_3bet_vs_sb_3bb",              "SUM(vs_sb_3bet_s3)",                   "SUM(vs_sb_opp_s3)" },
  // Opportunities (counts)
  { "cbet_flop_opp",                          "SUM(flop_cbet_opp)",     "1" },
  { "fold_to_3bet_afterR_opp",                "SUM(fold_3bet_r_opp)",   "1" },
  { "preflop_squeeze_opp",                    "SUM(squeeze_opp)",       "1" },
  { "flop_fold_to_cbet__flop_raise_cbet_opp", "SUM(flop_cbet_faced)",   "1" },
  { "flop_donkbet_opp",                       "SUM(flop_donk_opp)",     "1" },
};
const int kNumberOfStats = (int)(sizeof(kStats) / sizeof(kStats[0]));

const StatDef *FindStat(const std::string &name) {
  for (int i = 0; i < kNumberOfStats; ++i) {
    if (name == kStats[i].name) return &kStats[i];
  }
  return NULL;
}

// Small RAII wrapper for prepared statements
class Stmt {
 public:
  Stmt(sqlite3 *db, const std::string &sql) : stmt_(NULL), ok_(false) {
    ok_ = (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, NULL) == SQLITE_OK);
  }
  ~Stmt() { if (stmt_) sqlite3_finalize(stmt_); }
  bool ok() const { return ok_; }
  sqlite3_stmt *get() { return stmt_; }
 private:
  sqlite3_stmt *stmt_;
  bool ok_;
};

std::string BuildStatSql(const StatDef &def, const StatFilter &filter) {
  std::ostringstream sql;
  sql << "SELECT CASE WHEN (" << def.den << ") > 0 THEN (" << def.num
      << ") * 1.0 / (" << def.den << ") END FROM ("
      << "SELECT * FROM hand_player WHERE name = ?";
  if (!filter.site.empty()) sql << " AND site = ?";
  if (!filter.any_game_type) sql << " AND is_tournament = ?";
  if (filter.since > 0)     sql << " AND played_at >= ?";
  if (filter.position >= 0) sql << " AND position = ?";
  sql << " ORDER BY played_at DESC, id DESC";
  if (filter.nb_hands > 0)  sql << " LIMIT ?";
  sql << ");";
  return sql.str();
}

const char *ActionName(hh::ActionType t) {
  switch (t) {
    case hh::kPostAnte:       return "post_ante";
    case hh::kPostSmallBlind: return "post_sb";
    case hh::kPostBigBlind:   return "post_bb";
    case hh::kFold:           return "fold";
    case hh::kCheck:          return "check";
    case hh::kCall:           return "call";
    case hh::kBet:            return "bet";
    case hh::kRaise:          return "raise";
  }
  return "?";
}

}  // namespace

//------------------------------------------------------------------------------
// Helpers
//------------------------------------------------------------------------------

bool ParsePeriodSeconds(const std::string &period, long long *seconds) {
  size_t i = 0;
  while (i < period.size() && isdigit((unsigned char)period[i])) ++i;
  if (i == 0 || i == period.size()) return false;
  long long amount = atoll(period.substr(0, i).c_str());
  std::string unit = period.substr(i);
  for (size_t k = 0; k < unit.size(); ++k) unit[k] = (char)tolower((unsigned char)unit[k]);
  long long unit_seconds = 0;
  if (unit == "year" || unit == "years")          unit_seconds = 365LL * 86400;
  else if (unit == "month" || unit == "months")   unit_seconds = 30LL * 86400;
  else if (unit == "week" || unit == "weeks")     unit_seconds = 7LL * 86400;
  else if (unit == "day" || unit == "days")       unit_seconds = 86400;
  else if (unit == "hour" || unit == "hours")     unit_seconds = 3600;
  else if (unit == "minute" || unit == "minutes") unit_seconds = 60;
  else if (unit == "second" || unit == "seconds") unit_seconds = 1;
  else return false;
  if (seconds) *seconds = amount * unit_seconds;
  return true;
}

int EditDistance(const std::string &a, const std::string &b) {
  const size_t n = a.size(), m = b.size();
  std::vector<int> prev(m + 1), cur(m + 1);
  for (size_t j = 0; j <= m; ++j) prev[j] = (int)j;
  for (size_t i = 1; i <= n; ++i) {
    cur[0] = (int)i;
    for (size_t j = 1; j <= m; ++j) {
      int cost = (tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 1])) ? 0 : 1;
      cur[j] = std::min(std::min(prev[j] + 1, cur[j - 1] + 1), prev[j - 1] + cost);
    }
    prev.swap(cur);
  }
  return prev[m];
}

//------------------------------------------------------------------------------
// Database
//------------------------------------------------------------------------------

Db::Db() : db_(NULL) {}

Db::~Db() { Close(); }

void Db::SetError(std::string *error, const std::string &message) {
  if (error) *error = message;
}

bool Db::IsOpen() {
  std::lock_guard<std::mutex> lock(mutex_);
  return db_ != NULL;
}

std::string Db::Path() {
  std::lock_guard<std::mutex> lock(mutex_);
  return path_;
}

void Db::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    sqlite3_close(db_);
    db_ = NULL;
  }
}

bool Db::Exec(const char *sql, std::string *error) {
  char *message = NULL;
  int rc = sqlite3_exec(db_, sql, NULL, NULL, &message);
  if (rc != SQLITE_OK) {
    SetError(error, message ? message : "SQLite error");
    sqlite3_free(message);
    return false;
  }
  return true;
}

bool Db::EnsureColumn(const char *table, const char *column, const char *type, std::string *error) {
  bool found = false;
  {
    Stmt stmt(db_, std::string("PRAGMA table_info(") + table + ");");
    if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); return false; }
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
      const char *name = (const char *)sqlite3_column_text(stmt.get(), 1);
      if (name && strcmp(name, column) == 0) found = true;
    }
  }
  if (found) return true;
  return Exec((std::string("ALTER TABLE ") + table + " ADD COLUMN " + column + " " + type + ";").c_str(), error);
}

bool Db::Backfill(std::string *error) {
  std::string message;
  if (!Exec("BEGIN IMMEDIATE;", &message)) { SetError(error, message); return false; }
  bool ok = Exec(
    "UPDATE hands SET pot_type = CASE (SELECT COUNT(*) FROM hand_actions a WHERE a.hand_pk = hands.id"
    " AND a.street = 1 AND a.type = 'raise') WHEN 0 THEN 1 WHEN 1 THEN 2 WHEN 2 THEN 3 ELSE 4 END"
    " WHERE pot_type IS NULL;"
    "UPDATE hands SET eff_bb = (SELECT p.start_stack FROM hand_player p WHERE p.hand_pk = hands.id"
    " ORDER BY p.start_stack DESC LIMIT 1 OFFSET 1) / NULLIF(bblind, 0) WHERE eff_bb IS NULL;", &message);
  if (ok) {
    std::vector<std::pair<long long, std::string> > classes;
    {
      Stmt stmt(db_, "SELECT id, hole FROM hand_player WHERE hole_class IS NULL AND length(hole) >= 4;");
      if (stmt.ok()) {
        while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
          std::string hole = (const char *)sqlite3_column_text(stmt.get(), 1);
          classes.push_back(std::make_pair((long long)sqlite3_column_int64(stmt.get(), 0),
            HoleClass(hole.substr(0, hole.size() / 2), hole.substr(hole.size() / 2))));
        }
      }
    }
    Stmt update(db_, "UPDATE hand_player SET hole_class = ? WHERE id = ?;");
    ok = update.ok();
    for (size_t i = 0; ok && i < classes.size(); ++i) {
      sqlite3_reset(update.get());
      sqlite3_bind_text(update.get(), 1, classes[i].second.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(update.get(), 2, classes[i].first);
      ok = sqlite3_step(update.get()) == SQLITE_DONE;
      if (!ok) message = sqlite3_errmsg(db_);
    }
  }
  std::string ignored;
  if (ok && Exec("COMMIT;", &message)) return true;
  Exec("ROLLBACK;", &ignored);
  SetError(error, message);
  return false;
}

bool Db::CreateSchema(std::string *error) {
  std::ostringstream counters;
  for (int i = 0; i < kNumCounters; ++i) {
    counters << ", " << CounterName(i) << " INTEGER NOT NULL DEFAULT 0";
  }
  std::ostringstream sql;
  sql <<
    "CREATE TABLE IF NOT EXISTS hands ("
    " id INTEGER PRIMARY KEY,"
    " site TEXT NOT NULL,"
    " hand_id TEXT NOT NULL,"
    " played_at INTEGER NOT NULL,"
    " table_name TEXT,"
    " is_tournament INTEGER NOT NULL DEFAULT 0,"
    " tournament_id TEXT,"
    " level INTEGER,"
    " nseats INTEGER, n_players INTEGER, dealer INTEGER, hero INTEGER,"
    " sblind REAL, bblind REAL, ante REAL,"
    " board TEXT, showdown INTEGER, last_street INTEGER,"
    " total_pot REAL, rake REAL,"
    " uncalled REAL, uncalled_seat INTEGER, sb_seat INTEGER, bb_seat INTEGER,"
    " UNIQUE(site, hand_id));"
    "CREATE TABLE IF NOT EXISTS hand_player ("
    " id INTEGER PRIMARY KEY,"
    " hand_pk INTEGER NOT NULL REFERENCES hands(id) ON DELETE CASCADE,"
    " site TEXT NOT NULL,"
    " is_tournament INTEGER NOT NULL DEFAULT 0,"
    " played_at INTEGER NOT NULL,"
    " seat INTEGER,"
    " name TEXT NOT NULL COLLATE NOCASE,"
    " position INTEGER,"
    " is_hero INTEGER NOT NULL DEFAULT 0,"
    " start_stack REAL, end_stack REAL,"
    " hole TEXT,"
    " contrib REAL, collected REAL,"
    " folded INTEGER NOT NULL DEFAULT 0"
    << counters.str() << ");"
    "CREATE INDEX IF NOT EXISTS idx_hand_player_name"
    " ON hand_player(name, site, is_tournament, played_at);"
    "CREATE INDEX IF NOT EXISTS idx_hand_player_hand ON hand_player(hand_pk);"
    "CREATE TABLE IF NOT EXISTS hand_actions ("
    " hand_pk INTEGER NOT NULL REFERENCES hands(id) ON DELETE CASCADE,"
    " seq INTEGER NOT NULL,"
    " seat INTEGER, street INTEGER, type TEXT,"
    " amount REAL, to_amount REAL, allin INTEGER, raise_by REAL,"
    " PRIMARY KEY(hand_pk, seq));"
    "CREATE TABLE IF NOT EXISTS hand_notes ("
    " hand_pk INTEGER PRIMARY KEY REFERENCES hands(id) ON DELETE CASCADE,"
    " marked INTEGER NOT NULL DEFAULT 0,"
    " tags TEXT NOT NULL DEFAULT '',"
    " note TEXT NOT NULL DEFAULT '');";
  if (!Exec(sql.str().c_str(), error)) return false;
  // Version 1 -> 2: columns for rebuilding hands (hand text, replayer)
  if (!EnsureColumn("hands", "uncalled", "REAL", error) ||
      !EnsureColumn("hands", "uncalled_seat", "INTEGER", error) ||
      !EnsureColumn("hands", "sb_seat", "INTEGER", error) ||
      !EnsureColumn("hands", "bb_seat", "INTEGER", error) ||
      !EnsureColumn("hand_actions", "raise_by", "REAL", error) ||
      // Version 2 -> 3: values for the filters of the explorer
      !EnsureColumn("hands", "pot_type", "INTEGER", error) ||
      !EnsureColumn("hands", "eff_bb", "REAL", error) ||
      !EnsureColumn("hand_player", "hole_class", "TEXT", error)) return false;
  std::ostringstream version;
  version << "PRAGMA user_version=" << kSchemaVersion << ";";
  return Exec(version.str().c_str(), error);
}

bool Db::Open(const std::string &path, std::string *error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    sqlite3_close(db_);
    db_ = NULL;
  }
  if (path.empty()) {
    SetError(error, "no database path");
    return false;
  }
  int rc = sqlite3_open_v2(path.c_str(), &db_,
    SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, NULL);
  if (rc != SQLITE_OK) {
    SetError(error, db_ ? sqlite3_errmsg(db_) : "out of memory");
    if (db_) sqlite3_close(db_);
    db_ = NULL;
    return false;
  }
  sqlite3_busy_timeout(db_, 5000);
  std::string ignored;
  Exec("PRAGMA foreign_keys=ON;", &ignored);
  Exec("PRAGMA journal_mode=WAL;", &ignored);
  Exec("PRAGMA synchronous=NORMAL;", &ignored);
  // A database of a newer version must not be touched
  int old_version = 0;
  {
    Stmt stmt(db_, "PRAGMA user_version;");
    if (stmt.ok() && sqlite3_step(stmt.get()) == SQLITE_ROW) {
      int version = sqlite3_column_int(stmt.get(), 0);
      old_version = version;
      if (version > kSchemaVersion) {
        SetError(error, "database was created by a newer version of OpenHoldem");
        sqlite3_close(db_);
        db_ = NULL;
        return false;
      }
    }
  }
  if (!CreateSchema(error) || (old_version > 0 && old_version < 3 && !Backfill(error))) {
    sqlite3_close(db_);
    db_ = NULL;
    return false;
  }
  path_ = path;
  return true;
}

long long Db::CountHands() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return -1;
  Stmt stmt(db_, "SELECT COUNT(*) FROM hands;");
  if (!stmt.ok() || sqlite3_step(stmt.get()) != SQLITE_ROW) return -1;
  return sqlite3_column_int64(stmt.get(), 0);
}

bool Db::InsertHand(const hh::Hand &hand, const std::string &site, std::string *error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) { SetError(error, "database not open"); return false; }
  if (hand.hand_id.empty()) { SetError(error, "hand without ID"); return false; }
  std::vector<PlayerStats> players = Analyze(hand);
  if (players.empty()) { SetError(error, "no players"); return false; }

  const long long played_at = hand.start_time != 0 ? hand.start_time : (long long)time(NULL);
  std::string board;
  for (int i = 0; i < hand.board_count && i < 5; ++i) {
    if (!board.empty()) board += " ";
    board += hand.board[i];
  }
  int n_players = 0;
  for (int i = 0; i < hh::kMaxSeats; ++i) if (hand.in_hand[i]) ++n_players;

  std::string message;
  if (!Exec("BEGIN IMMEDIATE;", &message)) { SetError(error, message); return false; }

  bool ok = false;
  do {
    sqlite3_int64 hand_pk = 0;
    {
      Stmt stmt(db_,
        "INSERT OR IGNORE INTO hands(site, hand_id, played_at, table_name, is_tournament,"
        " tournament_id, level, nseats, n_players, dealer, hero, sblind, bblind, ante,"
        " board, showdown, last_street, total_pot, rake, uncalled, uncalled_seat, sb_seat, bb_seat,"
        " pot_type, eff_bb)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
      if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); break; }
      sqlite3_stmt *s = stmt.get();
      int k = 1;
      sqlite3_bind_text(s, k++, site.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(s, k++, hand.hand_id.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int64(s, k++, played_at);
      sqlite3_bind_text(s, k++, hand.table_name.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(s, k++, hand.tournament ? 1 : 0);
      sqlite3_bind_text(s, k++, hand.tournament_id.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(s, k++, hand.level);
      sqlite3_bind_int(s, k++, hand.nseats);
      sqlite3_bind_int(s, k++, n_players);
      sqlite3_bind_int(s, k++, hand.dealer);
      sqlite3_bind_int(s, k++, hand.hero);
      sqlite3_bind_double(s, k++, hand.sblind);
      sqlite3_bind_double(s, k++, hand.bblind);
      sqlite3_bind_double(s, k++, hand.ante);
      sqlite3_bind_text(s, k++, board.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_int(s, k++, hand.showdown ? 1 : 0);
      sqlite3_bind_int(s, k++, hand.last_street);
      sqlite3_bind_double(s, k++, hand.total_pot);
      sqlite3_bind_double(s, k++, hand.rake);
      sqlite3_bind_double(s, k++, hand.uncalled);
      sqlite3_bind_int(s, k++, hand.uncalled_seat);
      sqlite3_bind_int(s, k++, hand.sb_seat);
      sqlite3_bind_int(s, k++, hand.bb_seat);
      {
        int raises = 0;
        for (size_t a = 0; a < hand.actions.size(); ++a) {
          if (hand.actions[a].street == 1 && hand.actions[a].type == hh::kRaise) ++raises;
        }
        sqlite3_bind_int(s, k++, raises == 0 ? 1 : raises == 1 ? 2 : raises == 2 ? 3 : 4);
        // effective stack: the second biggest stack
        double biggest = 0, second = 0;
        for (int i = 0; i < hh::kMaxSeats; ++i) {
          if (!hand.in_hand[i]) continue;
          if (hand.start_stack[i] > biggest) { second = biggest; biggest = hand.start_stack[i]; }
          else if (hand.start_stack[i] > second) second = hand.start_stack[i];
        }
        if (hand.bblind > 0) sqlite3_bind_double(s, k++, second / hand.bblind);
        else sqlite3_bind_null(s, k++);
      }
      if (sqlite3_step(s) != SQLITE_DONE) { SetError(error, sqlite3_errmsg(db_)); break; }
      if (sqlite3_changes(db_) == 0) {
        SetError(error, "duplicate hand");
        break;
      }
      hand_pk = sqlite3_last_insert_rowid(db_);
    }

    // Players
    std::ostringstream columns, marks;
    for (int i = 0; i < kNumCounters; ++i) {
      columns << ", " << CounterName(i);
      marks << ",?";
    }
    std::string player_sql =
      "INSERT INTO hand_player(hand_pk, site, is_tournament, played_at, seat, name, position,"
      " is_hero, start_stack, end_stack, hole, contrib, collected, folded, hole_class" + columns.str() +
      ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?" + marks.str() + ");";
    bool players_ok = true;
    {
      Stmt stmt(db_, player_sql);
      if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); break; }
      sqlite3_stmt *s = stmt.get();
      for (size_t i = 0; i < players.size() && players_ok; ++i) {
        const PlayerStats &p = players[i];
        sqlite3_reset(s);
        sqlite3_clear_bindings(s);
        std::string hole = hand.hole[p.seat][0] + hand.hole[p.seat][1];
        int k = 1;
        sqlite3_bind_int64(s, k++, hand_pk);
        sqlite3_bind_text(s, k++, site.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(s, k++, hand.tournament ? 1 : 0);
        sqlite3_bind_int64(s, k++, played_at);
        sqlite3_bind_int(s, k++, p.seat);
        sqlite3_bind_text(s, k++, p.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(s, k++, p.position);
        sqlite3_bind_int(s, k++, p.seat == hand.hero ? 1 : 0);
        sqlite3_bind_double(s, k++, hand.start_stack[p.seat]);
        sqlite3_bind_double(s, k++, hand.end_stack[p.seat]);
        sqlite3_bind_text(s, k++, hole.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(s, k++, hand.contrib[p.seat]);
        sqlite3_bind_double(s, k++, hand.collected[p.seat]);
        sqlite3_bind_int(s, k++, hand.folded[p.seat] ? 1 : 0);
        {
          std::string cls = HoleClass(hand.hole[p.seat][0], hand.hole[p.seat][1]);
          if (cls.empty()) sqlite3_bind_null(s, k++);
          else sqlite3_bind_text(s, k++, cls.c_str(), -1, SQLITE_TRANSIENT);
        }
        for (int c = 0; c < kNumCounters; ++c) sqlite3_bind_int(s, k++, p.counters[c]);
        if (sqlite3_step(s) != SQLITE_DONE) {
          SetError(error, sqlite3_errmsg(db_));
          players_ok = false;
        }
      }
    }
    if (!players_ok) break;

    // Actions
    {
      Stmt stmt(db_,
        "INSERT INTO hand_actions(hand_pk, seq, seat, street, type, amount, to_amount, allin, raise_by)"
        " VALUES(?,?,?,?,?,?,?,?,?);");
      if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); break; }
      sqlite3_stmt *s = stmt.get();
      bool actions_ok = true;
      for (size_t i = 0; i < hand.actions.size() && actions_ok; ++i) {
        const hh::Action &a = hand.actions[i];
        sqlite3_reset(s);
        sqlite3_clear_bindings(s);
        int k = 1;
        sqlite3_bind_int64(s, k++, hand_pk);
        sqlite3_bind_int(s, k++, (int)i);
        sqlite3_bind_int(s, k++, a.seat);
        sqlite3_bind_int(s, k++, a.street);
        sqlite3_bind_text(s, k++, ActionName(a.type), -1, SQLITE_STATIC);
        sqlite3_bind_double(s, k++, a.amount);
        sqlite3_bind_double(s, k++, a.to);
        sqlite3_bind_int(s, k++, a.allin ? 1 : 0);
        sqlite3_bind_double(s, k++, a.raise_by);
        if (sqlite3_step(s) != SQLITE_DONE) {
          SetError(error, sqlite3_errmsg(db_));
          actions_ok = false;
        }
      }
      if (!actions_ok) break;
    }
    ok = true;
  } while (false);

  std::string ignored;
  if (ok) {
    if (!Exec("COMMIT;", &message)) {
      SetError(error, message);
      Exec("ROLLBACK;", &ignored);
      return false;
    }
    return true;
  }
  Exec("ROLLBACK;", &ignored);
  return false;
}

std::string Db::ResolveName(const std::string &scraped_name, const StatFilter &filter) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_ || scraped_name.empty()) return "";
  const std::string site_clause = filter.site.empty() ? "" : " AND site = ?";

  // 1. Exact match (ignoring case)
  {
    Stmt stmt(db_, "SELECT name FROM hand_player WHERE name = ?" + site_clause +
                   " AND is_tournament = ? LIMIT 1;");
    if (!stmt.ok()) return "";
    int k = 1;
    sqlite3_bind_text(stmt.get(), k++, scraped_name.c_str(), -1, SQLITE_TRANSIENT);
    if (!filter.site.empty()) sqlite3_bind_text(stmt.get(), k++, filter.site.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt.get(), k++, filter.tournament ? 1 : 0);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
      return (const char *)sqlite3_column_text(stmt.get(), 0);
    }
  }

  // 2. Unique closest name (scraping errors)
  const int length = (int)scraped_name.size();
  const int allowed = (length < 4) ? 0 : (length < 12 ? 1 : 2);
  if (allowed == 0) return "";
  Stmt stmt(db_, "SELECT DISTINCT name FROM hand_player WHERE is_tournament = ?" + site_clause + ";");
  if (!stmt.ok()) return "";
  int k = 1;
  sqlite3_bind_int(stmt.get(), k++, filter.tournament ? 1 : 0);
  if (!filter.site.empty()) sqlite3_bind_text(stmt.get(), k++, filter.site.c_str(), -1, SQLITE_TRANSIENT);
  std::string best;
  int best_distance = allowed + 1;
  bool ambiguous = false;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
    const char *candidate = (const char *)sqlite3_column_text(stmt.get(), 0);
    if (!candidate) continue;
    std::string name = candidate;
    if (abs((int)name.size() - length) > allowed) continue;
    int distance = EditDistance(scraped_name, name);
    if (distance > allowed) continue;
    if (distance < best_distance) {
      best_distance = distance;
      best = name;
      ambiguous = false;
    } else if (distance == best_distance) {
      ambiguous = true;
    }
  }
  return ambiguous ? std::string() : best;
}

bool Db::GetStat(const std::string &stat_name, const std::string &player,
                 const StatFilter &filter, double *value) {
  const StatDef *def = FindStat(stat_name);
  if (!def || player.empty()) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  Stmt stmt(db_, BuildStatSql(*def, filter));
  if (!stmt.ok()) return false;
  sqlite3_stmt *s = stmt.get();
  int k = 1;
  sqlite3_bind_text(s, k++, player.c_str(), -1, SQLITE_TRANSIENT);
  if (!filter.site.empty()) sqlite3_bind_text(s, k++, filter.site.c_str(), -1, SQLITE_TRANSIENT);
  if (!filter.any_game_type) sqlite3_bind_int(s, k++, filter.tournament ? 1 : 0);
  if (filter.since > 0)     sqlite3_bind_int64(s, k++, filter.since);
  if (filter.position >= 0) sqlite3_bind_int(s, k++, filter.position);
  if (filter.nb_hands > 0)  sqlite3_bind_int(s, k++, filter.nb_hands);
  if (sqlite3_step(s) != SQLITE_ROW) return false;
  if (sqlite3_column_type(s, 0) == SQLITE_NULL) return false;
  if (value) *value = sqlite3_column_double(s, 0);
  return true;
}

bool Db::GetStatFraction(const std::string &stat_name, const std::string &player,
                        const StatFilter &filter, double *num, double *den) {
  const StatDef *def = FindStat(stat_name);
  if (!def || player.empty()) return false;
  StatDef fraction = { def->name, def->num, def->den };
  std::string sql = BuildStatSql(fraction, filter);
  // Same rows, but both parts of the ratio
  const std::string head = std::string("SELECT CASE WHEN (") + def->den + ") > 0 THEN (" + def->num +
                           ") * 1.0 / (" + def->den + ") END FROM (";
  if (sql.compare(0, head.size(), head) != 0) return false;
  sql = std::string("SELECT (") + def->num + ") * 1.0, (" + def->den + ") * 1.0 FROM (" + sql.substr(head.size());
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;
  Stmt stmt(db_, sql);
  if (!stmt.ok()) return false;
  sqlite3_stmt *s = stmt.get();
  int k = 1;
  sqlite3_bind_text(s, k++, player.c_str(), -1, SQLITE_TRANSIENT);
  if (!filter.site.empty()) sqlite3_bind_text(s, k++, filter.site.c_str(), -1, SQLITE_TRANSIENT);
  if (!filter.any_game_type) sqlite3_bind_int(s, k++, filter.tournament ? 1 : 0);
  if (filter.since > 0)     sqlite3_bind_int64(s, k++, filter.since);
  if (filter.position >= 0) sqlite3_bind_int(s, k++, filter.position);
  if (filter.nb_hands > 0)  sqlite3_bind_int(s, k++, filter.nb_hands);
  if (sqlite3_step(s) != SQLITE_ROW) return false;
  if (num) *num = sqlite3_column_type(s, 0) == SQLITE_NULL ? 0 : sqlite3_column_double(s, 0);
  if (den) *den = sqlite3_column_type(s, 1) == SQLITE_NULL ? 0 : sqlite3_column_double(s, 1);
  return true;
}

bool Db::SelfTest(std::string *error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) { SetError(error, "database not open"); return false; }
  StatFilter all_filters;
  all_filters.site = "x";
  all_filters.since = 1;
  all_filters.position = 0;
  all_filters.nb_hands = 1;
  for (int i = 0; i < kNumberOfStats; ++i) {
    Stmt stmt(db_, BuildStatSql(kStats[i], all_filters));
    if (!stmt.ok()) {
      SetError(error, std::string("stat \"") + kStats[i].name + "\": " + sqlite3_errmsg(db_));
      return false;
    }
  }
  Stmt check(db_, "PRAGMA quick_check;");
  if (!check.ok() || sqlite3_step(check.get()) != SQLITE_ROW ||
      std::string((const char *)sqlite3_column_text(check.get(), 0)) != "ok") {
    SetError(error, "database file is damaged (quick_check failed)");
    return false;
  }
  return true;
}

bool Db::IsSupportedStat(const std::string &stat_name) {
  return FindStat(stat_name) != NULL;
}

std::vector<std::string> Db::SupportedStats() {
  std::vector<std::string> names;
  for (int i = 0; i < kNumberOfStats; ++i) names.push_back(kStats[i].name);
  return names;
}



//------------------------------------------------------------------------------
// Explorer queries
//------------------------------------------------------------------------------

namespace {

std::string ColumnText(sqlite3_stmt *s, int column) {
  const char *text = (const char *)sqlite3_column_text(s, column);
  return text ? text : "";
}

// LIKE pattern that matches the text anywhere (special characters escaped, ESCAPE '\\')
std::string LikePattern(const std::string &text) {
  std::string pattern = "%";
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' || text[i] == '_' || text[i] == '\\') pattern += '\\';
    pattern += text[i];
  }
  return pattern + "%";
}

struct ActionTypeResult { bool ok; hh::ActionType type; };

ActionTypeResult ParseActionName(const std::string &name) {
  ActionTypeResult r; r.ok = true; r.type = hh::kFold;
  if (name == "post_ante") r.type = hh::kPostAnte;
  else if (name == "post_sb") r.type = hh::kPostSmallBlind;
  else if (name == "post_bb") r.type = hh::kPostBigBlind;
  else if (name == "fold") r.type = hh::kFold;
  else if (name == "check") r.type = hh::kCheck;
  else if (name == "call") r.type = hh::kCall;
  else if (name == "bet") r.type = hh::kBet;
  else if (name == "raise") r.type = hh::kRaise;
  else r.ok = false;
  return r;
}

}  // namespace

std::string FormatTimestamp(long long seconds_since_1970) {
  time_t t = (time_t)seconds_since_1970;
  struct tm tm_value;
#ifdef _WIN32
  localtime_s(&tm_value, &t);
#else
  localtime_r(&t, &tm_value);
#endif
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y/%m/%d %H:%M:%S", &tm_value);
  return buffer;
}

bool Db::PlayerTable(const PlayerQuery &query, std::vector<PlayerRow> *rows, std::string *error) {
  if (!rows) return false;
  rows->clear();
  std::vector<const StatDef *> defs;
  for (size_t i = 0; i < query.stats.size(); ++i) {
    const StatDef *def = FindStat(query.stats[i]);
    if (!def) { SetError(error, "unknown statistic: " + query.stats[i]); return false; }
    defs.push_back(def);
  }
  const StatFilter &f = query.filter;
  std::ostringstream sql;
  sql << "SELECT name, COUNT(*), SUM(end_stack - start_stack)";
  for (size_t i = 0; i < defs.size(); ++i) {
    sql << ", CASE WHEN (" << defs[i]->den << ") > 0 THEN (" << defs[i]->num
        << ") * 1.0 / (" << defs[i]->den << ") END, (" << defs[i]->den << ") * 1.0";
  }
  sql << " FROM (SELECT *";
  if (f.nb_hands > 0) {
    sql << ", ROW_NUMBER() OVER (PARTITION BY name ORDER BY played_at DESC, id DESC) AS rn";
  }
  sql << " FROM hand_player WHERE 1=1";
  if (!f.site.empty())       sql << " AND site = ?";
  const bool any_type = query.any_game_type || f.any_game_type;
  if (!any_type)  sql << " AND is_tournament = ?";
  if (f.since > 0)           sql << " AND played_at >= ?";
  if (f.position >= 0)       sql << " AND position = ?";
  if (!query.name_contains.empty()) sql << " AND name LIKE ? ESCAPE '\\'";
  sql << ")";
  if (f.nb_hands > 0) sql << " WHERE rn <= ?";
  sql << " GROUP BY name HAVING COUNT(*) >= ? ORDER BY COUNT(*) DESC, name LIMIT ?;";

  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) { SetError(error, "database not open"); return false; }
  Stmt stmt(db_, sql.str());
  if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); return false; }
  sqlite3_stmt *s = stmt.get();
  int k = 1;
  if (!f.site.empty())       sqlite3_bind_text(s, k++, f.site.c_str(), -1, SQLITE_TRANSIENT);
  if (!any_type)  sqlite3_bind_int(s, k++, f.tournament ? 1 : 0);
  if (f.since > 0)           sqlite3_bind_int64(s, k++, f.since);
  if (f.position >= 0)       sqlite3_bind_int(s, k++, f.position);
  if (!query.name_contains.empty()) {
    std::string pattern = LikePattern(query.name_contains);
    sqlite3_bind_text(s, k++, pattern.c_str(), -1, SQLITE_TRANSIENT);
  }
  if (f.nb_hands > 0)        sqlite3_bind_int(s, k++, f.nb_hands);
  sqlite3_bind_int(s, k++, query.min_hands < 1 ? 1 : query.min_hands);
  sqlite3_bind_int(s, k++, query.limit > 0 ? query.limit : -1);
  int rc;
  while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
    PlayerRow row;
    row.name = ColumnText(s, 0);
    row.hands = sqlite3_column_int64(s, 1);
    row.won = sqlite3_column_double(s, 2);
    for (size_t i = 0; i < defs.size(); ++i) {
      int column = 3 + 2 * (int)i;
      row.values.push_back(sqlite3_column_type(s, column) == SQLITE_NULL
        ? std::numeric_limits<double>::quiet_NaN() : sqlite3_column_double(s, column));
      row.opportunities.push_back(sqlite3_column_type(s, column + 1) == SQLITE_NULL
        ? 0.0 : sqlite3_column_double(s, column + 1));
    }
    rows->push_back(row);
  }
  if (rc != SQLITE_DONE) { SetError(error, sqlite3_errmsg(db_)); return false; }
  return true;
}

std::string Cell::AsText() const {
  char buffer[48];
  switch (type) {
    case kInt:  snprintf(buffer, sizeof(buffer), "%lld", i); return buffer;
    case kReal: snprintf(buffer, sizeof(buffer), "%g", d);   return buffer;
    case kText: return s;
    default:    return "";
  }
}

bool HandFilter::Active() const {
  return position >= 0 || !hole_class.empty() || !board_has.empty() || min_pot_bb > 0 || max_pot_bb > 0 ||
         min_eff_bb > 0 || max_eff_bb > 0 || result != 0 || pot_type != 0 || showdown != 0 ||
         marked_only || !tag.empty() || !text.empty();
}

std::string HoleClass(const std::string &card1, const std::string &card2) {
  if (card1.size() < 2 || card2.size() < 2) return "";
  static const char kRanks[] = "23456789TJQKA";
  const char *r1 = strchr(kRanks, card1[0]), *r2 = strchr(kRanks, card2[0]);
  if (!r1 || !r2) return "";
  char hi = card1[0], lo = card2[0];
  if (r2 > r1) { hi = card2[0]; lo = card1[0]; }
  std::string out;
  out += hi; out += lo;
  if (hi != lo) out += (card1[1] == card2[1]) ? 's' : 'o';
  return out;
}

std::string HandFromSql(bool by_player) {
  return std::string(" FROM hands h ") + (by_player ? "JOIN" : "LEFT JOIN") +
         " hand_player p ON p.hand_pk = h.id AND " + (by_player ? "p.name = ?" : "p.is_hero = 1") +
         " LEFT JOIN hand_notes n ON n.hand_pk = h.id";
}

static std::string LikeAround(const std::string &text) {
  std::string pattern = "%";
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '%' || text[i] == '_' || text[i] == '\\') pattern += '\\';
    pattern += text[i];
  }
  return pattern + "%";
}

std::string HandFilterSql(const HandFilter &f, std::vector<Cell> *binds) {
  std::string sql;
  if (f.position >= 0) { sql += " AND p.position = ?"; binds->push_back(Cell::Int(f.position)); }
  if (!f.hole_class.empty()) { sql += " AND p.hole_class = ?"; binds->push_back(Cell::Text(f.hole_class)); }
  {
    std::istringstream cards(f.board_has);
    std::string card;
    while (cards >> card) {
      sql += " AND h.board LIKE ? ESCAPE '\\'";
      binds->push_back(Cell::Text(LikeAround(card)));
    }
  }
  if (f.min_pot_bb > 0) { sql += " AND h.bblind > 0 AND h.total_pot / h.bblind >= ?"; binds->push_back(Cell::Real(f.min_pot_bb)); }
  if (f.max_pot_bb > 0) { sql += " AND h.bblind > 0 AND h.total_pot / h.bblind <= ?"; binds->push_back(Cell::Real(f.max_pot_bb)); }
  if (f.min_eff_bb > 0) { sql += " AND h.eff_bb >= ?"; binds->push_back(Cell::Real(f.min_eff_bb)); }
  if (f.max_eff_bb > 0) { sql += " AND h.eff_bb <= ?"; binds->push_back(Cell::Real(f.max_eff_bb)); }
  if (f.result == 1) sql += " AND p.end_stack - p.start_stack > 0.001";
  if (f.result == 2) sql += " AND p.end_stack - p.start_stack < -0.001";
  if (f.pot_type > 0) { sql += " AND h.pot_type = ?"; binds->push_back(Cell::Int(f.pot_type)); }
  if (f.showdown == 1) sql += " AND h.showdown = 1";
  if (f.showdown == 2) sql += " AND h.showdown = 0";
  if (f.marked_only) sql += " AND n.marked = 1";
  if (!f.tag.empty()) {
    sql += " AND (',' || n.tags || ',') LIKE ? ESCAPE '\\'";
    binds->push_back(Cell::Text(LikeAround("," + f.tag + ",")));
  }
  if (!f.text.empty()) { sql += " AND n.note LIKE ? ESCAPE '\\'"; binds->push_back(Cell::Text(LikeAround(f.text))); }
  return sql;
}

namespace {

void BindCells(sqlite3_stmt *s, const std::vector<Cell> &binds, int *k) {
  for (size_t i = 0; i < binds.size(); ++i) {
    const Cell &c = binds[i];
    switch (c.type) {
      case Cell::kInt:  sqlite3_bind_int64(s, (*k)++, c.i); break;
      case Cell::kReal: sqlite3_bind_double(s, (*k)++, c.d); break;
      case Cell::kText: sqlite3_bind_text(s, (*k)++, c.s.c_str(), -1, SQLITE_TRANSIENT); break;
      default:          sqlite3_bind_null(s, (*k)++); break;
    }
  }
}

}  // namespace

bool Db::ListHands(const HandQuery &q, std::vector<HandRow> *rows, long long *total, std::string *error) {
  if (!rows) return false;
  rows->clear();
  const bool by_player = !q.player.empty();
  std::vector<Cell> binds;
  if (by_player) binds.push_back(Cell::Text(q.player));
  std::string where = " WHERE 1=1";
  if (!q.site.empty())  { where += " AND h.site = ?"; binds.push_back(Cell::Text(q.site)); }
  if (q.game_type >= 0) { where += " AND h.is_tournament = ?"; binds.push_back(Cell::Int(q.game_type)); }
  if (q.since > 0)      { where += " AND h.played_at >= ?"; binds.push_back(Cell::Int(q.since)); }
  where += HandFilterSql(q.filter, &binds);
  const std::string from = HandFromSql(by_player);

  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) { SetError(error, "database not open"); return false; }
  if (total) {
    *total = 0;
    Stmt stmt(db_, "SELECT COUNT(*)" + from + where + ";");
    if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); return false; }
    int k = 1;
    BindCells(stmt.get(), binds, &k);
    if (sqlite3_step(stmt.get()) == SQLITE_ROW) *total = sqlite3_column_int64(stmt.get(), 0);
  }
  Stmt stmt(db_,
    "SELECT h.id, h.site, h.hand_id, h.table_name, h.board, h.played_at, h.is_tournament,"
    " h.showdown, h.sblind, h.bblind, h.total_pot, h.n_players,"
    " p.id, p.name, p.hole, p.position, p.folded, p.end_stack - p.start_stack, COALESCE(n.marked, 0)" +
    from + where + " ORDER BY h.played_at DESC, h.id DESC LIMIT ? OFFSET ?;");
  if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); return false; }
  sqlite3_stmt *s = stmt.get();
  int k = 1;
  BindCells(s, binds, &k);
  sqlite3_bind_int(s, k++, q.limit > 0 ? q.limit : -1);
  sqlite3_bind_int(s, k++, q.offset > 0 ? q.offset : 0);
  int rc;
  while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
    HandRow r;
    r.pk = sqlite3_column_int64(s, 0);
    r.site = ColumnText(s, 1);
    r.hand_id = ColumnText(s, 2);
    r.table_name = ColumnText(s, 3);
    r.board = ColumnText(s, 4);
    r.played_at = sqlite3_column_int64(s, 5);
    r.tournament = sqlite3_column_int(s, 6) != 0;
    r.showdown = sqlite3_column_int(s, 7) != 0;
    r.sblind = sqlite3_column_double(s, 8);
    r.bblind = sqlite3_column_double(s, 9);
    r.pot = sqlite3_column_double(s, 10);
    r.n_players = sqlite3_column_int(s, 11);
    r.has_player = sqlite3_column_type(s, 12) != SQLITE_NULL;
    if (r.has_player) {
      r.player_name = ColumnText(s, 13);
      r.player_hole = ColumnText(s, 14);
      r.player_position = sqlite3_column_int(s, 15);
      r.player_folded = sqlite3_column_int(s, 16) != 0;
      r.player_net = sqlite3_column_double(s, 17);
    }
    r.marked = sqlite3_column_int(s, 18) != 0;
    rows->push_back(r);
  }
  if (rc != SQLITE_DONE) { SetError(error, sqlite3_errmsg(db_)); return false; }
  return true;
}

bool Db::Select(const std::string &sql, const std::vector<Cell> &binds, std::vector<Row> *rows,
                std::string *error) {
  if (!rows) return false;
  rows->clear();
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) { SetError(error, "database not open"); return false; }
  Stmt stmt(db_, sql);
  if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); return false; }
  if (!sqlite3_stmt_readonly(stmt.get())) { SetError(error, "Select: statement changes data"); return false; }
  sqlite3_stmt *s = stmt.get();
  int k = 1;
  BindCells(s, binds, &k);
  const int columns = sqlite3_column_count(s);
  int rc;
  while ((rc = sqlite3_step(s)) == SQLITE_ROW) {
    Row row;
    for (int c = 0; c < columns; ++c) {
      switch (sqlite3_column_type(s, c)) {
        case SQLITE_INTEGER: row.push_back(Cell::Int(sqlite3_column_int64(s, c))); break;
        case SQLITE_FLOAT:   row.push_back(Cell::Real(sqlite3_column_double(s, c))); break;
        case SQLITE_NULL:    row.push_back(Cell()); break;
        default:             row.push_back(Cell::Text(ColumnText(s, c))); break;
      }
    }
    rows->push_back(row);
  }
  if (rc != SQLITE_DONE) { SetError(error, sqlite3_errmsg(db_)); return false; }
  return true;
}

bool Db::Execute(const std::string &sql, const std::vector<Cell> &binds, std::string *error) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) { SetError(error, "database not open"); return false; }
  Stmt stmt(db_, sql);
  if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); return false; }
  int k = 1;
  BindCells(stmt.get(), binds, &k);
  if (sqlite3_step(stmt.get()) != SQLITE_DONE) { SetError(error, sqlite3_errmsg(db_)); return false; }
  return true;
}

bool Db::GetNote(long long hand_pk, bool *marked, std::string *tags, std::string *note) {
  std::vector<Row> rows;
  std::vector<Cell> binds(1, Cell::Int(hand_pk));
  if (marked) *marked = false;
  if (tags) tags->clear();
  if (note) note->clear();
  if (!Select("SELECT marked, tags, note FROM hand_notes WHERE hand_pk = ?;", binds, &rows) || rows.empty()) return false;
  if (marked) *marked = rows[0][0].AsInt() != 0;
  if (tags) *tags = rows[0][1].AsText();
  if (note) *note = rows[0][2].AsText();
  return true;
}

bool Db::SetNote(long long hand_pk, bool marked, const std::string &tags, const std::string &note,
                 std::string *error) {
  std::vector<Cell> binds;
  if (!marked && tags.empty() && note.empty()) {
    binds.push_back(Cell::Int(hand_pk));
    return Execute("DELETE FROM hand_notes WHERE hand_pk = ?;", binds, error);
  }
  binds.push_back(Cell::Int(hand_pk));
  binds.push_back(Cell::Int(marked ? 1 : 0));
  binds.push_back(Cell::Text(tags));
  binds.push_back(Cell::Text(note));
  return Execute("INSERT INTO hand_notes(hand_pk, marked, tags, note) VALUES(?,?,?,?)"
                 " ON CONFLICT(hand_pk) DO UPDATE SET marked = excluded.marked, tags = excluded.tags,"
                 " note = excluded.note;", binds, error);
}

bool Db::LoadHand(long long pk, hh::Hand *hand, std::string *site, std::string *error) {
  if (!hand) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) { SetError(error, "database not open"); return false; }
  hh::Hand h;
  {
    Stmt stmt(db_,
      "SELECT site, hand_id, played_at, table_name, is_tournament, tournament_id, level, nseats,"
      " dealer, hero, sblind, bblind, ante, board, showdown, last_street, total_pot, rake,"
      " uncalled, uncalled_seat, sb_seat, bb_seat FROM hands WHERE id = ?;");
    if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); return false; }
    sqlite3_stmt *s = stmt.get();
    sqlite3_bind_int64(s, 1, pk);
    if (sqlite3_step(s) != SQLITE_ROW) { SetError(error, "hand not found"); return false; }
    if (site) *site = ColumnText(s, 0);
    h.hand_id = ColumnText(s, 1);
    h.start_time = sqlite3_column_int64(s, 2);
    h.timestamp = FormatTimestamp(h.start_time);
    h.table_name = ColumnText(s, 3);
    h.tournament = sqlite3_column_int(s, 4) != 0;
    h.tournament_id = ColumnText(s, 5);
    h.level = sqlite3_column_int(s, 6);
    h.nseats = sqlite3_column_int(s, 7);
    h.dealer = sqlite3_column_int(s, 8);
    h.hero = sqlite3_column_int(s, 9);
    h.sblind = sqlite3_column_double(s, 10);
    h.bblind = sqlite3_column_double(s, 11);
    h.ante = sqlite3_column_double(s, 12);
    std::istringstream board(ColumnText(s, 13));
    std::string card;
    while (h.board_count < 5 && (board >> card)) h.board[h.board_count++] = card;
    h.showdown = sqlite3_column_int(s, 14) != 0;
    h.last_street = sqlite3_column_int(s, 15);
    h.total_pot = sqlite3_column_double(s, 16);
    h.rake = sqlite3_column_double(s, 17);
    h.uncalled = sqlite3_column_double(s, 18);   // NULL (version 1) = 0
    h.uncalled_seat = sqlite3_column_type(s, 19) == SQLITE_NULL ? -1 : sqlite3_column_int(s, 19);
    h.sb_seat = sqlite3_column_type(s, 20) == SQLITE_NULL ? -1 : sqlite3_column_int(s, 20);
    h.bb_seat = sqlite3_column_type(s, 21) == SQLITE_NULL ? -1 : sqlite3_column_int(s, 21);
  }
  {
    Stmt stmt(db_,
      "SELECT seat, name, position, start_stack, end_stack, hole, contrib, collected, folded"
      " FROM hand_player WHERE hand_pk = ? ORDER BY seat;");
    if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); return false; }
    sqlite3_stmt *s = stmt.get();
    sqlite3_bind_int64(s, 1, pk);
    while (sqlite3_step(s) == SQLITE_ROW) {
      int seat = sqlite3_column_int(s, 0);
      if (seat < 0 || seat >= hh::kMaxSeats) continue;
      h.in_hand[seat] = true;
      h.name[seat] = ColumnText(s, 1);
      int position = sqlite3_column_int(s, 2);
      if (h.sb_seat < 0 && position == kPositionSB) h.sb_seat = seat;
      if (h.bb_seat < 0 && position == kPositionBB) h.bb_seat = seat;
      h.start_stack[seat] = sqlite3_column_double(s, 3);
      h.end_stack[seat] = sqlite3_column_double(s, 4);
      std::string hole = ColumnText(s, 5);
      if (hole.size() >= 4) {
        h.hole[seat][0] = hole.substr(0, hole.size() / 2);
        h.hole[seat][1] = hole.substr(hole.size() / 2);
      }
      h.contrib[seat] = sqlite3_column_double(s, 6);
      h.collected[seat] = sqlite3_column_double(s, 7);
      h.folded[seat] = sqlite3_column_int(s, 8) != 0;
    }
  }
  {
    Stmt stmt(db_,
      "SELECT seat, street, type, amount, to_amount, allin, raise_by FROM hand_actions"
      " WHERE hand_pk = ? ORDER BY seq;");
    if (!stmt.ok()) { SetError(error, sqlite3_errmsg(db_)); return false; }
    sqlite3_stmt *s = stmt.get();
    sqlite3_bind_int64(s, 1, pk);
    double current_bet = 0;
    int current_street = 0;
    while (sqlite3_step(s) == SQLITE_ROW) {
      hh::Action a;
      a.seat = sqlite3_column_int(s, 0);
      a.street = sqlite3_column_int(s, 1);
      ActionTypeResult t = ParseActionName(ColumnText(s, 2));
      if (!t.ok) continue;
      a.type = t.type;
      a.amount = sqlite3_column_double(s, 3);
      a.to = sqlite3_column_double(s, 4);
      a.allin = sqlite3_column_int(s, 5) != 0;
      if (a.street != current_street) { current_street = a.street; current_bet = 0; }
      if (sqlite3_column_type(s, 6) != SQLITE_NULL) a.raise_by = sqlite3_column_double(s, 6);
      else if (a.type == hh::kRaise) a.raise_by = a.to - current_bet;   // version 1
      if (a.type != hh::kFold && a.type != hh::kCheck && a.type != hh::kPostAnte && a.to > current_bet) {
        current_bet = a.to;
      }
      h.actions.push_back(a);
    }
  }
  *hand = h;
  return true;
}

std::vector<std::string> Db::Sites() {
  std::vector<std::string> sites;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return sites;
  Stmt stmt(db_, "SELECT DISTINCT site FROM hands ORDER BY site;");
  if (!stmt.ok()) return sites;
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) sites.push_back(ColumnText(stmt.get(), 0));
  return sites;
}

std::vector<std::string> Db::FindPlayers(const std::string &contains, int limit) {
  std::vector<std::string> names;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return names;
  Stmt stmt(db_, "SELECT name FROM hand_player WHERE name LIKE ? ESCAPE '\\'"
                 " GROUP BY name ORDER BY COUNT(*) DESC, name LIMIT ?;");
  if (!stmt.ok()) return names;
  std::string pattern = LikePattern(contains);
  sqlite3_bind_text(stmt.get(), 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt.get(), 2, limit > 0 ? limit : -1);
  while (sqlite3_step(stmt.get()) == SQLITE_ROW) names.push_back(ColumnText(stmt.get(), 0));
  return names;
}

}  // namespace ohstats
