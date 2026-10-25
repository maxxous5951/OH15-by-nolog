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

const int kSchemaVersion = 1;

// Which hands are used for a statistic
struct StatFilter {
  std::string site;           // lower-case site name, "" = all sites
  bool        tournament;     // cash game or tournament hands
  int         nb_hands;       // only the last N hands of the player, 0 = all
  long long   since;          // only hands since this time (seconds since 1970), 0 = all
  int         position;       // only hands in this position (enum Position), -1 = all
  StatFilter() : tournament(false), nb_hands(0), since(0), position(-1) {}
};

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

  // Checks that the file is healthy and that the SQL of all statistics is valid
  bool SelfTest(std::string *error = NULL);

  // Statistics that can be calculated (names without "pt_" prefix)
  static bool IsSupportedStat(const std::string &stat_name);
  static std::vector<std::string> SupportedStats();

 private:
  bool Exec(const char *sql, std::string *error);
  bool CreateSchema(std::string *error);
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
