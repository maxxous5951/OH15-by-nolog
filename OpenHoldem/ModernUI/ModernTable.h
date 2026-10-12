//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Renderer of the poker table in the main window.
//
//   The renderer knows nothing about the OpenHoldem engine. It draws a plain
//   data snapshot (TableView), which makes it testable and keeps the engine
//   and the UI decoupled. COpenHoldemView fills the snapshot.
//
//******************************************************************************

#ifndef INC_MODERNUI_TABLE_H
#define INC_MODERNUI_TABLE_H

#include "ModernGfx.h"
#include "ModernTheme.h"

namespace modernui {

struct CardView {
  enum Kind { None, Back, Face };
  Kind kind;
  char rank;   // 'A','K','Q','J','T','9'..'2'  (T is shown as "10")
  char suit;   // 'c','d','h','s'
  CardView() : kind(None), rank('?'), suit('?') {}
};

struct SeatView {
  bool seated, active, dealer, is_user;
  std::wstring name;
  std::wstring balance;    // already formatted, e.g. "1000" or "Out (0)"
  std::wstring bet;        // empty = no bet
  CardView cards[4];
  int card_count;
  SeatView() : seated(false), active(false), dealer(false), is_user(false), card_count(0) {}
};

// Order of the indicator badges.
enum IndicatorIndex {
  kIndFold = 0, kIndCall, kIndCheck, kIndRaise, kIndAllin,      // right group: F C K R A
  kIndAutopost, kIndSitin, kIndSitout, kIndLeave, kIndPrefold,  // left group:  T I O L P
  kIndicatorCount
};

struct TableView {
  int nchairs;
  SeatView seats[10];
  CardView community[5];
  std::wstring hand_line;    // "Hand #: 2"
  std::wstring game_line;    // "NL 5/10/20"
  std::wstring ante_line;    // "" or "Ante: 5"
  std::wstring pot_line;     // "Pot: 255"
  bool indicator_on[kIndicatorCount];
  TableView() : nchairs(0) {
    for (int i = 0; i < kIndicatorCount; ++i) indicator_on[i] = false;
  }
  // True if drawing both snapshots would give the same picture.
  bool SameAs(const TableView& other) const;
};

// Draws everything (background, felt, info box, cards, seats, bets, dealer
// button, indicator badges) into "area". "s" is the DPI scale (1.0 = 96 dpi).
void DrawTable(Gdiplus::Graphics& g, const Gdiplus::RectF& area, float s,
               const Palette& colors, const TableView& view);

}  // namespace modernui

#endif  // INC_MODERNUI_TABLE_H
