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
// Hand history generator (NL Hold'em cash games).
//
// Every heartbeat this module takes a snapshot of the table and feeds it to
// the hand reconstructor (HandHistoryCore.h). When a hand is over (handreset)
// the reconstructed hand is checked and
//   * written in PokerStars format to
//       OpenHoldem\Hand_Histories\<SESSION_ID>\HH_<date>.txt
//     (to be imported by PokerTracker / Hold'em Manager), or
//   * if it is inconsistent, written together with the reasons to
//       OpenHoldem\Hand_Histories\<SESSION_ID>\rejected_<date>.log
//
// Enabled by the preference "handhistory_generator_enable".
//
//******************************************************************************

#ifndef INC_CHANDHISTORYWRITER_H
#define INC_CHANDHISTORYWRITER_H

#include <time.h>
#include "CVirtualSymbolEngine.h"
#include "HandHistoryCore.h"
#include "HandHistoryFormat.h"

const int kMaxLines = 256;

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
 public:
	// Public accessors
	bool EvaluateSymbol(const CString name, double *result, bool log = false);
  CString SymbolsProvided();
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
  void AppendToFile(const CString &path, const std::string &text);
 private:
  hh::Reconstructor   _reconstructor;
  hh::Options         _options;
  hh::FormatOptions   _format_options;
  unsigned int        _hand_counter;
};

extern CHandHistoryWriter *p_handhistory_writer;

#endif INC_CHANDHISTORYWRITER_H