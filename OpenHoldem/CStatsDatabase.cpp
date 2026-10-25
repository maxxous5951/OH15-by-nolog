//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: OpenHoldem's own stats database, see CStatsDatabase.h
//
//******************************************************************************

#include "stdafx.h"
#include "CStatsDatabase.h"

#include <shlobj.h>
#include <string>

#include "CEngineContainer.h"
#include "CSessionCounter.h"
#include "CSymbolEngineIsTournament.h"
#include "CTableState.h"
#include "..\CTablemap\CTablemap.h"
#include "..\PokerTracker_Query_Definitions\pokertracker_query_definitions.h"

namespace {

// A failed attempt to open the database is not repeated at every hand
const DWORD kRetryOpenAfterMs = 30 * 1000;
// A name that is not in the database is looked up again after some time,
// because hands may have been recorded in the meantime.
const DWORD kRetryUnknownNameAfterMs = 20 * 1000;

DWORD g_last_open_failure = 0;
bool  g_open_failed_before = false;

// Same rule as in the PokerTracker-code: a name that only consists of
// "l", "1", "i", ".", "," is a bad scrape
bool NameLooksLikeBadScrape(const std::string &name) {
  for (size_t i = 0; i < name.size(); ++i) {
    char c = name[i];
    if (c != 'l' && c != '1' && c != 'i' && c != '.' && c != ',') return false;
  }
  return true;
}

std::string ToStd(const CString &s) {
  return std::string((LPCTSTR)s);
}

}  // namespace

CStatsDatabase *StatsDatabase() {
  // Created on first use, never deleted (the process is about to end anyway)
  static CStatsDatabase *instance = new CStatsDatabase;
  return instance;
}

CStatsDatabase::CStatsDatabase() : _generation(0) {
}

CStatsDatabase::~CStatsDatabase() {
  _db.Close();
}

bool CStatsDatabase::UseAsStatsSource() {
  return Preferences()->stats_source() == kStatsSourceOpenHoldemDb;
}

CString CStatsDatabase::DefaultPath() {
  CString path;
  path.Format("%s\\Hand_Histories\\%s", OpenHoldemDirectory().GetString(),
    ohstats::DefaultDbFileName());
  return path;
}

CString CStatsDatabase::ConfiguredPath() {
  CString path = Preferences()->stats_db_path();
  path.Trim();
  if (path.IsEmpty()) return DefaultPath();
  return path;
}

bool CStatsDatabase::OpenLocked(CString *error) {
  CString path = ConfiguredPath();
  if (_db.IsOpen() && _opened_path == path) return true;
  if (g_open_failed_before && (GetTickCount() - g_last_open_failure) < kRetryOpenAfterMs
      && error == NULL) {
    return false;
  }
  _db.Close();
  ClearStatCache();
  // Create the folder (not the file)
  int last_separator = path.ReverseFind('\\');
  if (last_separator > 0) {
    SHCreateDirectoryEx(NULL, path.Left(last_separator).GetString(), NULL);
  }
  std::string message;
  if (_db.Open(ToStd(path), &message)) {
    _opened_path = path;
    g_open_failed_before = false;
    write_log(Preferences()->debug_pokertracker(),
      "[StatsDatabase] opened %s\n", path.GetString());
    return true;
  }
  g_open_failed_before = true;
  g_last_open_failure = GetTickCount();
  _opened_path = "";
  write_log(k_always_log_errors,
    "[StatsDatabase] ERROR: can't open %s: %s\n", path.GetString(), message.c_str());
  if (error) *error = message.c_str();
  return false;
}

bool CStatsDatabase::Open(CString *error) {
  CSLock lock(_lock);
  return OpenLocked(error);
}

void CStatsDatabase::Close() {
  CSLock lock(_lock);
  _db.Close();
  _opened_path = "";
  ClearStatCache();
  for (int i = 0; i < kMaxNumberOfPlayers; ++i) _chair[i] = ChairName();
}

bool CStatsDatabase::IsConnected() {
  CSLock lock(_lock);
  return OpenLocked(NULL);
}

bool CStatsDatabase::TestPath(const CString &path, CString *message) {
  int last_separator = path.ReverseFind('\\');
  if (last_separator > 0) {
    SHCreateDirectoryEx(NULL, path.Left(last_separator).GetString(), NULL);
  }
  ohstats::Db db;
  std::string problem;
  if (!db.Open(ToStd(path), &problem)) {
    if (message) message->Format("Can't open the database:\n%s\n\n%s", path.GetString(), problem.c_str());
    return false;
  }
  if (!db.SelfTest(&problem)) {
    if (message) message->Format("Database opened, but the self test failed:\n%s", problem.c_str());
    return false;
  }
  if (message) {
    message->Format("OK\n\n%s\n%I64d hands recorded.", path.GetString(), db.CountHands());
  }
  return true;
}

void CStatsDatabase::ClearStatCache() {
  _stat_cache.clear();
}

//------------------------------------------------------------------------------
// Recording
//------------------------------------------------------------------------------

CString CStatsDatabase::SiteName() {
  CString site = p_tablemap->sitename();
  if (site.IsEmpty()) site = p_tablemap->network();
  site.Trim();
  site.MakeLower();
  if (site.IsEmpty()) site = "unknown";
  return site;
}

void CStatsDatabase::RecordHand(const hh::Hand &hand) {
  if (!Preferences()->stats_db_record()) return;
  CSLock lock(_lock);
  if (!OpenLocked(NULL)) return;
  // Several instances of OpenHoldem may write to the same file:
  // the hand-IDs are only unique within a session.
  hh::Hand copy = hand;
  char prefix[32];
  sprintf_s(prefix, sizeof(prefix), "%d-", p_sessioncounter->session_id());
  copy.hand_id = std::string(prefix) + hand.hand_id;
  std::string error;
  if (_db.InsertHand(copy, ToStd(SiteName()), &error)) {
    ++_generation;
    ClearStatCache();
    write_log(Preferences()->debug_handhistory(),
      "[StatsDatabase] hand %s stored\n", copy.hand_id.c_str());
  } else if (error != "duplicate hand") {
    write_log(k_always_log_errors,
      "[StatsDatabase] ERROR: can't store hand %s: %s\n", copy.hand_id.c_str(), error.c_str());
  }
}

//------------------------------------------------------------------------------
// Names of the players
//------------------------------------------------------------------------------

bool CStatsDatabase::NameHasChanged(int chair) {
  if (chair < kFirstChair || chair > kLastChair) return false;
  CString name = p_table_state->Player(chair)->name();
  if (name.IsEmpty()) return false;
  std::string scraped = ToStd(name);
  if (NameLooksLikeBadScrape(scraped)) return false;
  CSLock lock(_lock);
  return !(_chair[chair].found && _chair[chair].scraped == scraped);
}

void CStatsDatabase::ClearChair(int chair) {
  if (chair < kFirstChair || chair > kLastChair) return;
  CSLock lock(_lock);
  _chair[chair] = ChairName();
}

bool CStatsDatabase::NameExists(int chair) {
  if (chair < kFirstChair || chair > kLastChair) return false;
  CString name = p_table_state->Player(chair)->name();
  if (name.IsEmpty()) return false;
  std::string scraped = ToStd(name);
  if (NameLooksLikeBadScrape(scraped)) return false;
  CSLock lock(_lock);
  ChairName &c = _chair[chair];
  if (c.scraped == scraped) {
    if (c.found) return true;
    // Unknown name: ask again from time to time
    if ((GetTickCount() - c.checked_at) < kRetryUnknownNameAfterMs) return false;
  }
  if (!OpenLocked(NULL)) return false;
  ohstats::StatFilter filter;
  filter.site = ToStd(SiteName());
  filter.tournament = p_engine_container->symbol_engine_istournament()->istournament();
  std::string resolved = _db.ResolveName(scraped, filter);
  c.scraped = scraped;
  c.resolved = resolved;
  c.found = !resolved.empty();
  c.checked_at = GetTickCount();
  write_log(Preferences()->debug_pokertracker(),
    "[StatsDatabase] chair %d: [%s] -> [%s]\n", chair, scraped.c_str(),
    c.found ? resolved.c_str() : "unknown");
  return c.found;
}

//------------------------------------------------------------------------------
// Statistics
//------------------------------------------------------------------------------

// Position of the chair, with the same rules as the positions that are stored
// (ohstats::ComputePositions): button, then blinds, then cut-off, else early.
int CStatsDatabase::CurrentPosition(int chair) {
  struct Local {
    static bool IsChair(const char *symbol, int chair) {
      double result = kUndefined;
      p_engine_container->EvaluateSymbol(symbol, &result, false);
      return (int)result == chair;
    }
  };
  if (Local::IsChair("buttonchair", chair))     return ohstats::kPositionBTN;
  if (Local::IsChair("smallblindchair", chair)) return ohstats::kPositionSB;
  if (Local::IsChair("bigblindchair", chair))   return ohstats::kPositionBB;
  if (Local::IsChair("cutoffchair", chair))     return ohstats::kPositionCO;
  return ohstats::kPositionEP;
}

double CStatsDatabase::GetStat(int chair, int stat_index) {
  if (chair < kFirstChair || chair > kLastChair) return kUndefined;
  if (stat_index < 0 || stat_index >= PT_DLL_GetNumberOfStats()) return kUndefined;
  std::string stat_name = ToStd(PT_DLL_GetBasicSymbolNameWithoutPTPrefix(stat_index));
  if (!ohstats::Db::IsSupportedStat(stat_name)) return kUndefined;
  if (!NameExists(chair)) return kUndefined;

  // The filters of the symbol, e.g. pt_500hands_vpip3 or pt_12months_pfr_raischair
  // (parsed by the PokerTracker-DLL, which sets these two variables)
  ohstats::StatFilter filter;
  filter.site = ToStd(SiteName());
  filter.tournament = p_engine_container->symbol_engine_istournament()->istournament();
  CString hands_filter = nb_hands;
  CString period_filter = time_period;
  if (!hands_filter.IsEmpty()) filter.nb_hands = atoi(hands_filter);
  if (!period_filter.IsEmpty()) {
    long long seconds = 0;
    if (ohstats::ParsePeriodSeconds(ToStd(period_filter), &seconds)) {
      filter.since = (long long)time(NULL) - seconds;
      if (filter.since < 1) filter.since = 1;
    }
  }
  if (Preferences()->stats_db_by_position()) {
    // Like the PokerTracker-queries: the stats of the player at his current position
    filter.position = CurrentPosition(chair);
  }

  CSLock lock(_lock);
  const std::string &player = _chair[chair].resolved;
  if (player.empty()) return kUndefined;
  char key[512];
  sprintf_s(key, sizeof(key), "%s|%s|%s|%d|%d|%s|%d",
    player.c_str(), stat_name.c_str(), filter.site.c_str(), filter.tournament ? 1 : 0,
    filter.nb_hands, (LPCTSTR)period_filter, filter.position);
  std::map<std::string, double>::const_iterator cached = _stat_cache.find(key);
  if (cached != _stat_cache.end()) return cached->second;
  double value = 0;
  double result = kUndefined;
  if (_db.GetStat(stat_name, player, filter, &value)) result = value;
  if (_stat_cache.size() > 5000) _stat_cache.clear();
  _stat_cache[key] = result;
  write_log(Preferences()->debug_pokertracker(),
    "[StatsDatabase] %s of [%s] (chair %d) = %f\n", stat_name.c_str(), player.c_str(), chair, result);
  return result;
}
