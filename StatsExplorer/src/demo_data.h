//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: random sample hands to try the explorer without a recorded database
//          (and for the automatic tests).
//
//******************************************************************************

#ifndef INC_EXPLORER_DEMO_DATA_H
#define INC_EXPLORER_DEMO_DATA_H

#include <string>
#include "StatsDb.h"

namespace explorer {

// Adds `hands` random 6-max cash hands (hero "Hero" and 7 opponents with different
// styles) to the database; the newest hand is "now", one hand per minute.
// A few tournament hands are added as well. Deterministic for a given seed.
bool AddDemoHands(ohstats::Db *db, int hands, unsigned seed, std::string *error);

// Creates (or extends) the database file.
bool GenerateDemoDb(const std::string &path, int hands, unsigned seed, std::string *error);

}  // namespace explorer

#endif  // INC_EXPLORER_DEMO_DATA_H
