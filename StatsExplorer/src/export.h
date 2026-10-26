//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: exports of the explorer: CSV tables and hand history text files.
//
//******************************************************************************

#ifndef INC_EXPLORER_EXPORT_H
#define INC_EXPLORER_EXPORT_H

#include <string>
#include <vector>
#include "explorer_model.h"

namespace explorer {

// "a,b" -> "\"a,b\"", quotes doubled
std::string CsvField(const std::string &text);

// Each function returns the number of rows / hands written, -1 on errors (message in `error`).
long long ExportPlayersCsv(const Explorer &explorer, const std::string &path, std::string *error);
long long ExportReportCsv(const Explorer &explorer, const std::string &path, std::string *error);
// The hand list of the current filters (all pages)
long long ExportHandListCsv(Explorer *explorer, const std::string &path, std::string *error);
// The full text of the hands of the current filters (all pages), PokerStars format
long long ExportHandsText(Explorer *explorer, const std::string &path, long long max_hands, std::string *error);

}  // namespace explorer

#endif  // INC_EXPLORER_EXPORT_H
