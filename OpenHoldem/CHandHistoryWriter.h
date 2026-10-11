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
// Hand history generator (NL Hold'em cash games and tournaments).
//
// Every heartbeat this module takes a snapshot of the table and feeds it to
// the hand reconstructor (HandHistoryCore.h). When a hand is over (handreset)
// the reconstructed hand is checked and
//   * written in PokerStars format to
//       OpenHoldem\Hand_Histories\<SESSION_ID>\HH_<date>.txt             (cash game)
//       OpenHoldem\Hand_Histories\<SESSION_ID>\HH_Tournament_<date>.txt  (tournament)
//     (to be imported by PokerTracker / Hold'em Manager), or
//   * if it is inconsistent, written together with the reasons to
//       OpenHoldem\Hand_Histories\<SESSION_ID>\rejected_<date>.log
//
// Enabled by the preference "handhistory_generator_enable".
//
// Cash game or tournament is chosen by the user (radio buttons on the page
// "Handhistory Generator" of the preferences, never guessed) and remembered
// in the preference "handhistory_tournament".
// The optional values for tournaments (tournament-ID, buy-in, fee) can be
// set in OpenHoldem\Hand_Histories\HH_Settings.ini (see hh::Settings).
//
//******************************************************************************

#ifndef INC_CHANDHISTORYWRITER_H
#define INC_CHANDHISTORYWRITER_H

#include <time.h>
#include <vector>
#include "CVirtualSymbolEngine.h"
#include "HandHistoryCore.h"
#include "HandHistoryFormat.h"

const int kMaxLines = 256;

namespace hh {

enum GameType { kCashGame = 0, kTournament = 1 };

// What the user selected: game_type comes from the preferences,
// the optional values from Hand_Histories\HH_Settings.ini, section [HandHistory]:
//   tournament_id=0     0 = automatic (900000000 + session-ID), else the ID of the tournament
//   buyin=0.00          tournament buy-in, 0 and fee 0 = "Freeroll"
//   fee=0.00            tournament fee
struct Settings {
  GameType           game_type;
  unsigned long long tournament_id;
  double             buyin, fee;
  Settings() : game_type(kCashGame), tournament_id(0), buyin(0), fee(0) {}
};

} // namespace hh

class CHandHistoryWriter: public CVirtualSymbolEngine {
 public:
	CHandHistoryWriter();
	~CHandHistoryWriter();
 public:
	// Mandatory reset-functions
	void InitOnStartup();
	void UpdateOnConnection();
	void UpdateOnHandreset();
	void UpdateOnNewRound();
	void UpdateOnMyTurn();
	void UpdateOnHeartbeat();
  // Action profile: remember the formula line of every action of the bot
  void UpdateAfterAutoplayerAction(int autoplayer_action_code);
 public:
	// Public accessors
	bool EvaluateSymbol(const CString name, double *result, bool log = false);
  CString SymbolsProvided();
 public:
  // Cash game / tournament selection (preferences) and optional values.
  // ReloadSettings() has to be called by the preferences dialog (if the writer exists)
  // after the selection was changed.
  static hh::Settings LoadSettings();
  void ReloadSettings();
public:
  // The following functions are called by the old hand-history modules.
  // They are obsolete: the reconstruction is now done by hh::Reconstructor.
  void AddMessage(CString message);
 public:
  void PostsSmallBlind(int chair);
  void PostsBigBlind(int chair);
  void PostsAnte(int chair);
 public:
  void Checks(int chair);
  void Folds(int chair);
  void Calls(int chair);
  void Raises(int chair);
 public:
  void WinsUncontested(int chair);
 private:
  hh::Snapshot BuildSnapshot();
  void WriteHand(hh::Hand hand);
  void WriteCompletedHands();
  CString OutputDirectory();
  static CString SettingsPath();
  void AppendToFile(const CString &path, const std::string &text);
 private:
  hh::Reconstructor   _reconstructor;
  hh::Options         _options;
  hh::FormatOptions   _format_options;
  hh::Settings        _settings;
  unsigned int        _hand_counter;
  // Tournaments: the blind level is counted whenever the blinds of
  // a written hand differ from the previous one (the level is not scraped).
  int                 _level;
  double              _last_sblind, _last_bblind;
  int                 _warned_state;   // wrong selection already reported
  // Action profile (optional second file): formula line of every action of the bot
  struct ProfileNote {
    int         action_code;
    long long   time;
    std::string text;
  };
  std::vector<ProfileNote> _notes;
  void AttachNotes(hh::Hand *hand);
};

extern CHandHistoryWriter *p_handhistory_writer;

#endif INC_CHANDHISTORYWRITER_H