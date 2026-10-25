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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
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
  { "fold_to_3bet_afterR",   "SUM(fold_3bet)",                          "SUM(fold_3bet_opp)" },
  { "fold_to_resteal",       "SUM(fold_resteal)",                       "SUM(fold_resteal_opp)" },
  { "4bet",                  "SUM(four_bet)",                           "SUM(four_bet_opp)" },
  { "fold_to_4bet",          "SUM(fold_4bet)",                          "SUM(fold_4bet_opp)" },
  { "preflop_squeeze",       "SUM(squeeze)",                            "SUM(squeeze_opp)" },
  // Aggression factor (bets+raises / calls) and frequency (bets+raises / all actions)
  { "flop_af",               "SUM(flop_br)",                            "SUM(flop_call)" },
  { "turn_af",               "SUM(turn_br)",                            "SUM(turn_call)" },
  { "river_af",              "SUM(river_br)",                           "SUM(river_call)" },
  { "total_af",              "SUM(flop_br+turn_br+river_br)",           "SUM(flop_call+turn_call+river_call)" },
  { "flop_afq",              "SUM(flop_br)",                            "SUM(flop_br+flop_call+flop_check+flop_fold)" },
  { "turn_afq",              "SUM(turn_br)",                            "SUM(turn_br+turn_call+turn_check+turn_fold)" },
  { "river_afq",             "SUM(river_br)",                           "SUM(river_br+river_call+river_check+river_fold)" },
  { "total_afq",             "SUM(flop_br+turn_br+river_br)",           "SUM(flop_br+flop_call+flop_check+flop_fold+turn_br+turn_call+turn_check+turn_fold+river_br+river_call+river_check+river_fold)" },
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
  // Opportunities (counts)
  { "cbet_flop_opp",                          "SUM(flop_cbet_opp)",     "1" },
  { "fold_to_3bet_afterR_opp",                "SUM(fold_3bet_opp)",     "1" },
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
  sql << " AND is_tournament = ?";
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
    " amount REAL, to_amount REAL, allin INTEGER,"
    " PRIMARY KEY(hand_pk, seq));";
  if (!Exec(sql.str().c_str(), error)) return false;
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
  {
    Stmt stmt(db_, "PRAGMA user_version;");
    if (stmt.ok() && sqlite3_step(stmt.get()) == SQLITE_ROW) {
      int version = sqlite3_column_int(stmt.get(), 0);
      if (version > kSchemaVersion) {
        SetError(error, "database was created by a newer version of OpenHoldem");
        sqlite3_close(db_);
        db_ = NULL;
        return false;
      }
    }
  }
  if (!CreateSchema(error)) {
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
        " board, showdown, last_street, total_pot, rake)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
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
      " is_hero, start_stack, end_stack, hole, contrib, collected, folded" + columns.str() +
      ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?" + marks.str() + ");";
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
        "INSERT INTO hand_actions(hand_pk, seq, seat, street, type, amount, to_amount, allin)"
        " VALUES(?,?,?,?,?,?,?,?);");
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
  sqlite3_bind_int(s, k++, filter.tournament ? 1 : 0);
  if (filter.since > 0)     sqlite3_bind_int64(s, k++, filter.since);
  if (filter.position >= 0) sqlite3_bind_int(s, k++, filter.position);
  if (filter.nb_hands > 0)  sqlite3_bind_int(s, k++, filter.nb_hands);
  if (sqlite3_step(s) != SQLITE_ROW) return false;
  if (sqlite3_column_type(s, 0) == SQLITE_NULL) return false;
  if (value) *value = sqlite3_column_double(s, 0);
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

}  // namespace ohstats
