//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: panels of the explorer: graph, reports, sessions, tournaments, leaks,
//          hand filters, import / export dialogs (used by explorer_ui.cpp).
//
//******************************************************************************

#ifndef INC_EXPLORER_UI_PANELS_H
#define INC_EXPLORER_UI_PANELS_H

#include <string>
#include "explorer_ui.h"

namespace explorer {

enum Tab { kTabPlayer = 1, kTabHands = 2, kTabHand = 3, kTabGraph = 4, kTabReports = 5,
           kTabSessions = 6, kTabTournaments = 7 };

std::string FilterSummary(const ohstats::HandFilter &filter);
ImVec4      TypeColor(PlayerType type);
bool        InputString(const char *label, std::string *text, const char *hint, float width);

void DrawFilterBar(Explorer &ex, UiState &ui);         // active hand filters + buttons
void DrawGraphTab(Explorer &ex, UiState &ui);
void DrawReportsTab(Explorer &ex, UiState &ui);
void DrawSessionsTab(Explorer &ex, UiState &ui);
void DrawTournamentsTab(Explorer &ex, UiState &ui);
void DrawHud(Explorer &ex);                            // key stats of the selected player in one line
void DrawLeaks(Explorer &ex);
void DrawPanelPopups(Explorer &ex, UiState &ui);       // filter, import, export dialogs

}  // namespace explorer

#endif  // INC_EXPLORER_UI_PANELS_H
