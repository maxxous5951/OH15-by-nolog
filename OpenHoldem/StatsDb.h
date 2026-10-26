//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: SQLite database of the OpenHoldem stats (replacement for PokerTracker).
//
//   One file, no server. Filled with the hands that the handhistory generator
//   reconstructs (hh::Hand), queried for the "pt_xxx" symbols.
//
//   Tables:
//     hands        one row per hand (table, blinds, board, pot, ...)
//     hand_player  one row per player and hand: stacks, cards, position and
//                  the counters of StatsCore.h (the statistics are ratios of
//                  sums of these counters)
//     hand_actions every action of the hand (for later use / import / export)
//
//   Std C++ + SQLite only (no MFC), so it can be unit-tested anywhere.
//   All functions are thread safe (one mutex per database object).
//
//******************************************************************************

#ifndef INC_OHSTATS_DB_H
#define INC_OHSTATS_DB_H

#include <mutex>
#include <string>
#include <vector>
#include "HandHistoryCore.h"
#include "StatsCore.h"

struct sqlite3;

namespace ohstats {

const int kSchemaVersion = 3;

// Which hands are used for a statistic
struct StatFilter {
  std::string site;           // lower-case site name, "" = all sites
  bool        tournament;     // cash game or tournament hands
  bool        any_game_type;  // cash games and tournaments together (ignores "tournament")
  int         nb_hands;       // only the last N hands of the player, 0 = all
  long long   since;          // only hands since this time (seconds since 1970), 0 = all
  int         position;       // only hands in this position (enum Position), -1 = all
  StatFilter() : tournament(false), any_game_type(false), nb_hands(0), since(0), position(-1) {}
};

// ---- Queries of the explorer (player table, hand list, hand) ----

// One row of the player table
struct PlayerRow {
  std::string         name;
  long long           hands;
  double              won;        // net result (money or chips)
  std::vector<double> values;     // one value per requested statistic, NaN = undefined
  std::vector<double> opportunities;   // denominators of these values (for confidence intervals)
  PlayerRow() : hands(0), won(0) {}
};

struct PlayerQuery {
  StatFilter               filter;
  bool                     any_game_type;   // cash games and tournaments together
  std::string              name_contains;   // "" = all players
  int                      min_hands;
  std::vector<std::string> stats;           // names of the statistics (columns)
  int                      limit;
  PlayerQuery() : any_game_type(false), min_hands(1), limit(1000) {}
};

// ---- Generic access (reports of the explorer) ----

struct Cell {
  enum Type { kNull, kInt, kReal, kText };
  Type        type;
  long long   i;
  double      d;
  std::string s;
  Cell() : type(kNull), i(0), d(0) {}
  static Cell Int(long long v)              { Cell c; c.type = kInt;  c.i = v; return c; }
  static Cell Real(double v)                { Cell c; c.type = kReal; c.d = v; return c; }
  static Cell Text(const std::string &v)    { Cell c; c.type = kText; c.s = v; return c; }
  bool        IsNull() const { return type == kNull; }
  double      AsDouble() const { return type == kInt ? (double)i : type == kReal ? d : 0.0; }
  long long   AsInt() const    { return type == kInt ? i : type == kReal ? (long long)d : 0; }
  std::string AsText() const;
};
typedef std::vector<Cell> Row;

// Filters on hands. They apply to "p" (the hand_player row of the selected player or
// of the hero), "h" (hands) and "n" (hand_notes), see HandFromSql / HandFilterSql.
struct HandFilter {
  int         position;        // -1 = all (enum Position)
  std::string hole_class;      // "AKs", "QQ", "T9o" ... "" = all
  std::string board_has;       // cards / ranks that must be on the board, e.g. "Ah" or "A K"
  double      min_pot_bb, max_pot_bb;   // final pot in big blinds, 0 = no limit
  double      min_eff_bb, max_eff_bb;   // effective stack in big blinds, 0 = no limit
  int         result;          // 0 = all, 1 = won, 2 = lost
  int         pot_type;        // 0 = all, 1 = limped, 2 = single raised, 3 = 3-bet, 4 = 4-bet or more
  int         showdown;        // 0 = all, 1 = went to showdown, 2 = no showdown
  bool        marked_only;
  std::string tag;             // hands with this tag
  std::string text;            // hands whose note contains this text
  HandFilter() : position(-1), min_pot_bb(0), max_pot_bb(0), min_eff_bb(0), max_eff_bb(0),
                 result(0), pot_type(0), showdown(0), marked_only(false) {}
  bool Active() const;
};

struct HandQuery {
  std::string site;            // "" = all
  int         game_type;       // -1 = all, 0 = cash, 1 = tournament
  long long   since;           // 0 = all
  std::string player;          // only hands of this player (exact name), "" = all
  HandFilter  filter;
  int         limit, offset;
  HandQuery() : game_type(-1), since(0), limit(200), offset(0) {}
};

// One line of the hand list. The "player" columns belong to the selected
// player, else to the hero (the user), if he took part.
struct HandRow {
  long long   pk;
  std::string site, hand_id, table_name, board;
  long long   played_at;
  bool        tournament, showdown;
  double      sblind, bblind, pot;
  int         n_players;
  bool        has_player, player_folded, marked;
  std::string player_name, player_hole;
  int         player_position;
  double      player_net;
  HandRow() : pk(0), played_at(0), tournament(false), showdown(false), sblind(0), bblind(0),
              pot(0), n_players(0), has_player(false), player_folded(false), marked(false),
              player_position(-1), player_net(0) {}
};

// "AKs", "QQ", "T9o" from the two hole cards ("" if unknown)
std::string HoleClass(const std::string &card1, const std::string &card2);
// FROM clause with the aliases h, p, n and " AND ..." conditions for the filter
// (binds are appended in the order of the placeholders)
std::string HandFromSql(bool by_player);       // first placeholder: player name, if by_player
std::string HandFilterSql(const HandFilter &filter, std::vector<Cell> *binds);

// "2026/10/25 19:24:00" in local time (like the hand histories)
std::string FormatTimestamp(long long seconds_since_1970);

class Db {
 public:
  Db();
  ~Db();

  // Opens (and creates, if necessary) the database file.
  // The folder has to exist (the caller creates it).
  bool Open(const std::string &path, std::string *error = NULL);
  void Close();
  bool IsOpen();
  std::string Path();

  // Stores a (valid) hand. Returns false for duplicates (same site and hand-ID)
  // or on errors. Players without a name are skipped.
  bool InsertHand(const hh::Hand &hand, const std::string &site, std::string *error = NULL);

  // Number of hands in the database, -1 on errors
  long long CountHands();

  // Name of the player in the database that matches the scraped name
  // (exact match, ignoring case, else the unique closest name with a small
  // edit distance because scraping errors are common). "" if unknown.
  std::string ResolveName(const std::string &scraped_name, const StatFilter &filter);

  // The statistic of a player. False if the statistic is not supported or
  // undefined (no opportunities). The values are ratios (0..1) like the
  // values that PokerTracker returns, "hands" is a count.
  bool GetStat(const std::string &stat_name, const std::string &player,
               const StatFilter &filter, double *value);

  // Numerator and denominator of a statistic (value = num / den), same filters as GetStat.
  bool GetStatFraction(const std::string &stat_name, const std::string &player,
                       const StatFilter &filter, double *num, double *den);

  // Player table: one row per player with the requested statistics
  bool PlayerTable(const PlayerQuery &query, std::vector<PlayerRow> *rows, std::string *error = NULL);
  // Hand list, newest first. total = number of hands that match (ignoring limit / offset).
  bool ListHands(const HandQuery &query, std::vector<HandRow> *rows, long long *total = NULL,
                 std::string *error = NULL);
  // Rebuilds a stored hand (for the hand text and the replayer)
  bool LoadHand(long long pk, hh::Hand *hand, std::string *site = NULL, std::string *error = NULL);
  // Free queries for reports (read only: the statement must not change data) and
  // changes of the explorer's own data (notes). The database is locked meanwhile.
  bool Select(const std::string &sql, const std::vector<Cell> &binds, std::vector<Row> *rows,
              std::string *error = NULL);
  bool Execute(const std::string &sql, const std::vector<Cell> &binds, std::string *error = NULL);
  // Notes, stars and tags of hands (tags: comma separated)
  bool GetNote(long long hand_pk, bool *marked, std::string *tags, std::string *note);
  bool SetNote(long long hand_pk, bool marked, const std::string &tags, const std::string &note,
               std::string *error = NULL);

  // Sites that occur in the database (lower case)
  std::vector<std::string> Sites();
  // Names of the players of the database that contain the text
  std::vector<std::string> FindPlayers(const std::string &contains, int limit = 50);

  // Checks that the file is healthy and that the SQL of all statistics is valid
  bool SelfTest(std::string *error = NULL);

  // Statistics that can be calculated (names without "pt_" prefix)
  static bool IsSupportedStat(const std::string &stat_name);
  static std::vector<std::string> SupportedStats();

 private:
  bool Exec(const char *sql, std::string *error);
  bool CreateSchema(std::string *error);
  bool Backfill(std::string *error);
  bool EnsureColumn(const char *table, const char *column, const char *type, std::string *error);
  void SetError(std::string *error, const std::string &message);

  sqlite3    *db_;
  std::string path_;
  std::mutex  mutex_;
};

// "12months", "2days", "30minutes" ... (as in the pt-symbols) to seconds.
// False if the text is not a period.
bool ParsePeriodSeconds(const std::string &period, long long *seconds);

// Levenshtein distance (case-insensitive)
int EditDistance(const std::string &a, const std::string &b);

// "<OpenHoldem folder>\Hand_Histories\oh_stats.db" is built by the caller;
// this only fixes the file name.
inline const char *DefaultDbFileName() { return "oh_stats.db"; }

}  // namespace ohstats

#endif  // INC_OHSTATS_DB_H
