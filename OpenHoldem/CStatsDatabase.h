//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: OpenHoldem's own stats database as an alternative to PokerTracker.
//
//   Glue between OpenHoldem (table state, preferences, pt_-symbols)
//   and the database (StatsDb.h, no dependencies on OpenHoldem):
//     * RecordHand(): called by the handhistory generator for every
//       reconstructed (valid) hand, if "record hands" is enabled in the
//       preferences
//     * GetStat() / NameExists(): used instead of the PokerTracker queries
//       if the preference "stats source" is "OpenHoldem database".
//       The symbols stay the same (pt_vpip_raischair, pt_3bet3, ...).
//
//   All functions are called from the heartbeat thread, the test function
//   of the preferences may run in the GUI thread. The database object itself
//   is thread safe, the small caches here are protected by a critical section.
//
//******************************************************************************

#ifndef INC_CSTATSDATABASE_H
#define INC_CSTATSDATABASE_H

#include <map>
#include <string>
#include "HandHistoryCore.h"
#include "StatsDb.h"
#include "..\Shared\CCritSec\CCritSec.h"

enum StatsSource {
  kStatsSourcePokerTracker = 0,     // default: nothing changes for existing users
  kStatsSourceOpenHoldemDb = 1
};

class CStatsDatabase {
 public:
  CStatsDatabase();
  ~CStatsDatabase();
 public:
  // The preference "stats source"
  static bool UseAsStatsSource();
  // Default location: <OpenHoldem folder>\Hand_Histories\oh_stats.db
  static CString DefaultPath();
  // Location from the preferences (default if empty)
  static CString ConfiguredPath();
 public:
  // Opens the database from the preferences (reopens, if the path changed).
  // Returns true if the database is usable.
  bool Open(CString *error = NULL);
  void Close();
  bool IsConnected();
  // For the "Test" button of the preferences: opens the file with a temporary
  // connection (the one used for playing is not touched), runs the self test
  // and counts the hands. The message is meant for the user.
  static bool TestPath(const CString &path, CString *message);
 public:
  // Called by the handhistory generator for every valid hand
  void RecordHand(const hh::Hand &hand);
 public:
  // pt_-symbols
  bool   NameExists(int chair);
  bool   NameHasChanged(int chair);
  void   ClearChair(int chair);
  // kUndefined if the player or the statistic is unknown or has no data yet
  double GetStat(int chair, int stat_index);
 private:
  CString SiteName();
  int     CurrentPosition(int chair);
  bool    OpenLocked(CString *error);
  void    ClearStatCache();
 private:
  ohstats::Db _db;
  CCritSec    _lock;
  // Name resolution per chair
  struct ChairName {
    std::string scraped, resolved;
    bool        found;
    int         level;  // 0 = site and game type match, 1 = any game type, 2 = any site and game type
    DWORD       checked_at;
    ChairName() : found(false), level(0), checked_at(0) {}
  };
  ChairName   _chair[kMaxNumberOfPlayers];
  // Cache of the stat values; invalid as soon as the database changes
  std::map<std::string, double> _stat_cache;
  long long   _generation;
  CString     _opened_path;
};

CStatsDatabase *StatsDatabase();

#endif  // INC_CSTATSDATABASE_H
