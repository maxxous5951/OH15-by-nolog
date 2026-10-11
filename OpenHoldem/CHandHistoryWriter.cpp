//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Part of the modular handhistory-generator.
//   Not really a symbol-engine but it is convenient
//   to make use of the symbol-engine-concepts,
//   Also used for the new summary in the logs.
//
// This module collects hh-data and writes it
//   at appropriate moments to disk, both for
//   real hand-histories and for the summary in the logs.
//
//******************************************************************************

#include "stdafx.h"
#include "CHandHistoryWriter.h"

#include <fstream>
#include <shlobj.h>
#include <string>

#include "CBetroundCalculator.h"
#include "CEngineContainer.h"
#include "CHandHistoryDealPhase.h"
#include "CHandHistoryUncontested.h"
#include "CScraper.h"
#include "CSessionCounter.h"
#include "CSymbolEngineDealerchair.h"
#include "CSymbolEngineIsTournament.h"
#include "CSymbolEngineTableLimits.h"
#include "CSymbolEngineUserchair.h"
#include "CTableState.h"
#include "..\CTablemap\CTablemap.h"

CHandHistoryWriter *p_handhistory_writer = NULL;

CHandHistoryWriter::CHandHistoryWriter() {
  // The values of some symbol-engines depend on other engines.
  // As the engines get later called in the order of initialization
  // we assure correct ordering by checking if they are initialized.
  //
  // This engine collects data from other HH-modules
  // and therefore should be executes after all the rest.
  assert(p_handhistory_deal_phase != NULL);
  assert(p_handhistory_uncontested != NULL);
  _hand_counter = 0;
  _level = 1;
  _last_sblind = _last_bblind = 0;
  _warned_state = -1;
  // The scraped pot is not reliable at every casino,
  // therefore it is not used to reject hands (yet).
  _options.check_pot = false;
  _reconstructor.SetOptions(_options);
  // Cash game or tournament, as selected by the user
  ReloadSettings();
}

CHandHistoryWriter::~CHandHistoryWriter() {
}

void CHandHistoryWriter::InitOnStartup() {
}

void CHandHistoryWriter::UpdateOnConnection() {
  _reconstructor.Reset();
  ReloadSettings();
}

//------------------------------------------------------------------------------
// Settings: cash game or tournament
//------------------------------------------------------------------------------

CString CHandHistoryWriter::SettingsPath() {
  CString directory;
  directory.Format("%s\\Hand_Histories", OpenHoldemDirectory().GetString());
  SHCreateDirectoryEx(NULL, directory.GetString(), NULL);
  return directory + "\\HH_Settings.ini";
}

hh::Settings CHandHistoryWriter::LoadSettings() {
  hh::Settings settings;
  // Cash game (default) or tournament: preference, selected by the user
  settings.game_type = Preferences()->handhistory_tournament() ? hh::kTournament : hh::kCashGame;
  // Optional values for tournaments, edited by hand
  CString path = SettingsPath();
  char buffer[64];
  GetPrivateProfileString("HandHistory", "tournament_id", "0", buffer, sizeof(buffer), path.GetString());
  settings.tournament_id = _strtoui64(buffer, NULL, 10);
  GetPrivateProfileString("HandHistory", "buyin", "0", buffer, sizeof(buffer), path.GetString());
  settings.buyin = atof(buffer);
  GetPrivateProfileString("HandHistory", "fee", "0", buffer, sizeof(buffer), path.GetString());
  settings.fee = atof(buffer);
  if (settings.buyin < 0) settings.buyin = 0;
  if (settings.fee < 0)   settings.fee = 0;
  return settings;
}

void CHandHistoryWriter::ReloadSettings() {
  hh::Settings old_settings = _settings;
  _settings = LoadSettings();
  if (_settings.game_type != old_settings.game_type) {
    // Blind levels belong to one tournament
    _level = 1;
    _last_sblind = _last_bblind = 0;
    _warned_state = -1;
  }
  // Tournaments have no rake: everything that is missing from the pot is an error
  hh::Options defaults;
  _options.max_rake = (_settings.game_type == hh::kTournament) ? 0.01 : defaults.max_rake;
  _reconstructor.SetOptions(_options);
  _format_options.tournament_buyin = _settings.buyin;
  _format_options.tournament_fee   = _settings.fee;
  write_log(Preferences()->debug_handhistory(),
    "[HandHistory] game type: %s\n",
    (_settings.game_type == hh::kTournament) ? "tournament" : "cash game");
}

void CHandHistoryWriter::UpdateOnHandreset() {
  // Only a hint: the reconstructor detects the end of a hand itself
  // (new hole cards, shrinking bets, ...), because the handreset signal
  // can be spurious in the middle of a hand or missing between two hands.
  _reconstructor.HandResetHint();
}

void CHandHistoryWriter::UpdateOnNewRound() {
}

void CHandHistoryWriter::UpdateOnMyTurn() {
}

void CHandHistoryWriter::UpdateOnHeartbeat() {
  if (!Preferences()->handhistory_generator_enable()) {
    return;
  }
  _reconstructor.Feed(BuildSnapshot());
  WriteCompletedHands();
}

void CHandHistoryWriter::WriteCompletedHands() {
  hh::Hand hand;
  while (_reconstructor.PopCompleted(&hand)) {
    WriteHand(hand);
  }
}

hh::Snapshot CHandHistoryWriter::BuildSnapshot() {
  hh::Snapshot s;
  s.betround = p_betround_calculator->betround();
  s.nseats = p_tablemap->nchairs();
  if (s.nseats > hh::kMaxSeats) s.nseats = hh::kMaxSeats;
  s.dealer = p_engine_container->symbol_engine_dealerchair()->dealerchair();
  s.hero   = p_engine_container->symbol_engine_userchair()->userchair();
  s.sblind = p_engine_container->symbol_engine_tablelimits()->sblind();
  s.bblind = p_engine_container->symbol_engine_tablelimits()->bblind();
  s.ante   = p_engine_container->symbol_engine_tablelimits()->ante();
  s.pot    = p_table_state->Pot(0);
  s.time   = (long long) time(NULL);
  for (int i = 0; i < 5; ++i) {
    Card *card = p_table_state->CommonCards(i);
    if (card->IsKnownCard()) {
      s.board[i] = (LPCTSTR)card->ToString();
    }
  }
  for (int i = 0; i < s.nseats; ++i) {
    CPlayer *player = p_table_state->Player(i);
    hh::SeatSnapshot &seat = s.seat[i];
    seat.seated  = player->seated();
    seat.active  = player->active();
    seat.name    = (LPCTSTR)player->name();
    seat.balance = player->_balance.GetValue();
    seat.bet     = player->_bet.GetValue();
    if (seat.balance < 0) seat.balance = 0;
    if (seat.bet < 0)     seat.bet = 0;
    for (int c = 0; c < 2; ++c) {
      Card *card = player->hole_cards(c);
      if (card->IsKnownCard()) {
        seat.cards[c] = (LPCTSTR)card->ToString();
      }
    }
  }
  return s;
}

CString CHandHistoryWriter::OutputDirectory() {
  CString path;
  path.Format("%s\\Hand_Histories\\%d", OpenHoldemDirectory().GetString(), p_sessioncounter->session_id());
  SHCreateDirectoryEx(NULL, path.GetString(), NULL);
  return path;
}

void CHandHistoryWriter::AppendToFile(const CString &path, const std::string &text) {
  // Binary mode: the text already contains Windows line-endings
  std::ofstream file(path.GetString(), std::ios::out | std::ios::app | std::ios::binary);
  if (file.is_open()) {
    file << text;
  }
}

void CHandHistoryWriter::WriteHand(hh::Hand hand) {
  // Identification
  time_t start = (hand.start_time != 0) ? (time_t) hand.start_time : time(NULL);
  struct tm local;
  localtime_s(&local, &start);
  char timestamp[32];
  strftime(timestamp, sizeof(timestamp), "%Y/%m/%d %H:%M:%S", &local);
  char day[16];
  strftime(day, sizeof(day), "%Y%m%d", &local);
  // 12 digits like at PokerStars: seconds since 1970 plus a counter
  char id[32];
  sprintf_s(id, sizeof(id), "%I64u", (unsigned long long)start * 100ULL + (_hand_counter % 100));
  ++_hand_counter;
  hand.hand_id = id;
  hand.timestamp = timestamp;
  CString title = p_table_state->TableTitle()->Title();
  title.Replace("'", " ");
  hand.table_name = (LPCTSTR)title;

  // Cash game or tournament: as selected by the user, never guessed.
  const bool tournament = (_settings.game_type == hh::kTournament);
  hand.tournament = tournament;
  if (tournament) {
    unsigned long long tournament_id = _settings.tournament_id;
    if (tournament_id == 0) {
      tournament_id = 900000000ULL + (unsigned long long) p_sessioncounter->session_id();
    }
    char tid[32];
    sprintf_s(tid, sizeof(tid), "%I64u", tournament_id);
    hand.tournament_id = tid;
    hand.table_name = std::string(tid) + " 1";   // like PokerStars: "<tournament-ID> <table number>"
    // The blind level is not scraped: count the changes of the blinds.
    // Only reliable hands are used, the blinds of a rejected hand may be guesses.
    if (hand.valid() && hand.sblind > 0 && hand.bblind > 0) {
      if (_last_sblind > 0 && _last_bblind > 0 &&
          (fabs(hand.sblind - _last_sblind) > hh::kEps || fabs(hand.bblind - _last_bblind) > hh::kEps)) {
        ++_level;
      }
      _last_sblind = hand.sblind;
      _last_bblind = hand.bblind;
    }
    hand.level = _level;
  }
  // The user is told once if the selection does not fit to what OpenHoldem detected
  // (istournament is a heuristic, therefore it is only a warning)
  {
    const bool detected = p_engine_container->symbol_engine_istournament()->istournament();
    const int state = (tournament ? 2 : 0) + (detected ? 1 : 0);
    if (detected != tournament && _warned_state != state) {
      _warned_state = state;
      write_log(k_always_log_errors,
        "[HandHistory] WARNING: handhistories are written for a %s, but OpenHoldem detected a %s. "
        "Please check the selection in Preferences / Handhistory Generator.\n",
        tournament ? "tournament" : "cash game",
        detected ? "tournament" : "cash game");
    }
  }

  CString directory = OutputDirectory();
  CString path;
  if (hand.valid()) {
    path.Format("%s\\%s_%s.txt", directory.GetString(), tournament ? "HH_Tournament" : "HH", day);
    AppendToFile(path, hh::FormatPokerStars(hand, _format_options));
    write_log(Preferences()->debug_handhistory(), 
      "[HandHistory] hand %s written\n", id);
  } else {
    path.Format("%s\\rejected_%s.log", directory.GetString(), day);
    AppendToFile(path, hh::FormatRejected(hand, _format_options));
    write_log(Preferences()->debug_handhistory(), 
      "[HandHistory] hand %s rejected (%s)\n", id,
      hand.problems.empty() ? "" : hand.problems[0].c_str());
  }
}

void CHandHistoryWriter::AddMessage(CString message) {
  // Obsolete, see header
}

void CHandHistoryWriter::PostsSmallBlind(int chair) {
}

void CHandHistoryWriter::PostsBigBlind(int chair) {
}

void CHandHistoryWriter::PostsAnte(int chair) {
}

void CHandHistoryWriter::Checks(int chair) {
}

void CHandHistoryWriter::Folds(int chair) {
}

void CHandHistoryWriter::Calls(int chair) {
}

void CHandHistoryWriter::Raises(int chair) {
}

void CHandHistoryWriter::WinsUncontested(int chair) {
}

bool CHandHistoryWriter::EvaluateSymbol(const CString name, double *result, bool log /* = false */) {
  // No symbols provided
  return false;
}

CString CHandHistoryWriter::SymbolsProvided() {
  // No symbols provided
  return "";
}
