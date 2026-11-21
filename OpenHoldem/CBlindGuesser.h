//****************************************************************************** 
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//****************************************************************************** 
//
// Purpose: first guess of blind values for CTableLimits,
//   which might be overridden by blind-locking.
//   * some casinos don't display blinds at all
//   * others display garbage like 0/0
//   * others display the first level in tournaments, 
//     then freeze and overpaint the titlebar with something 
//     that looks like a title
//   * some display sb/bb, some display bb/BB 
//     and others use the same title-format for both, depending on game-type
//   * ...
//   There are no easy solutions unfortunately if you want to make OH work 
//   at every casino out there.
//
//****************************************************************************** 

#ifndef INC_CBLINDGUESSER_H
#define INC_CBLINDGUESSER_H

#include "CBlindLevels.h"

class CBlindGuesser {
public:
	CBlindGuesser();
	~CBlindGuesser();
public:
	// Returns true if the result is reliable, i.e. it comes from the scraper
	// or from two blind-bets at the table that form a known blind-level.
	// Returns false for rough guesses and for the "complete failure" fallback.
	// Callers must not lock unreliable results.
	bool Guess(double *sblind, double *bblind, double *bbet, double *ante);
private:
	void GetFirstBlindDataFromScraper(double *sblind, double *bblind, double *bbet, double *ante);
	// Returns true if both blinds were seen as bets and form a known level
	bool GetFirstBlindDataFromBetsAtTheTable(double *sblind, double *bblind, double *bbet, double *ante);
private:
	bool SBlindBBlindCombinationReasonable(double sblind, double bblind);
	bool SBlindBBetCombinationReasonable(double sblind, double bbet);
	bool BBlindBBetCombinationReasonable(double bblind, double bbet);
private:
	// Some beautiful number in the range of 40..50%
	double ReasonableLookingHalfBlindValue(double known_value);
	bool CompletePartiallyKnownBlinds(double *sblind, double *bblind, double *bbet);
private:
	CBlindLevels _blind_levels;
};

#endif INC_CBLINDGUESSER_H