//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: import of PokerStars-style hand histories (as written by OpenHoldem
//          or by PokerStars: cash games and tournaments) into the stats database.
//
//******************************************************************************

#ifndef INC_EXPLORER_HH_IMPORT_H
#define INC_EXPLORER_HH_IMPORT_H

#include <string>
#include <vector>
#include "HandHistoryCore.h"
#include "StatsDb.h"

namespace explorer {

// Parses all hands of the text. Hands that can't be understood are skipped and
// described in `errors` (may be NULL). Returns the number of hands parsed.
int ParsePokerStars(const std::string &text, std::vector<hh::Hand> *hands,
                    std::vector<std::string> *errors = NULL);

struct ImportResult {
  int hands_found, imported, duplicates, failed;
  std::vector<std::string> errors;
  ImportResult() : hands_found(0), imported(0), duplicates(0), failed(0) {}
};

// Parses and stores the hands; `site` is the lower-case site name stored with them
bool ImportText(ohstats::Db *db, const std::string &text, const std::string &site, ImportResult *result);
bool ImportFile(ohstats::Db *db, const std::string &path, const std::string &site, ImportResult *result);

}  // namespace explorer

#endif  // INC_EXPLORER_HH_IMPORT_H
