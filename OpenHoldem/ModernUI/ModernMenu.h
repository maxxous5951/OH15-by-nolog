//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Themed replacement for native Win32 menus.
//
//   * The menu structure is read from the existing .rc menu resources
//     (single source of truth for command IDs, texts and shortcuts), then the
//     native HMENU is thrown away. No native menu is ever shown.
//   * Popups are owner-drawn with GDI+ and follow the light/dark theme.
//   * Mouse and keyboard handling (arrows, Enter, Esc, mnemonics, hover
//     switching between the entries of a menu bar).
//   * Single level only (none of the OpenHoldem menus has sub-menus).
//
//******************************************************************************

#ifndef INC_MODERNUI_MENU_H
#define INC_MODERNUI_MENU_H

#include "ModernGfx.h"
#include "ModernTheme.h"
#include <functional>

namespace modernui {

struct MenuItem {
  int          id;
  std::wstring text;       // without '&'
  std::wstring shortcut;   // text after the tab, e.g. "Ctrl+N"
  wchar_t      mnemonic;   // character after '&' (upper case), or 0
  Icon         icon;
  bool         separator, enabled, checked, radio;
  MenuItem() : id(0), mnemonic(0), icon(Icon::None), separator(false),
               enabled(true), checked(false), radio(false) {}
};

struct Menu {
  std::wstring text;       // title in the menu bar, without '&'
  wchar_t      mnemonic;
  int          command;    // != 0: top level entry that directly executes a command
  std::vector<MenuItem> items;
  Menu() : mnemonic(0), command(0) {}
  MenuItem* Find(int id) {
    for (size_t i = 0; i < items.size(); ++i) if (items[i].id == id) return &items[i];
    return nullptr;
  }
};

typedef std::vector<Menu> MenuBar;

// Splits "&Open...\tCtrl+O" into text / mnemonic / shortcut.
void ParseMenuText(const std::wstring& raw, std::wstring* text, wchar_t* mnemonic, std::wstring* shortcut);

// Reads a MENU resource. Top level entries with a popup become Menu objects
// with items; top level plain commands become Menu objects with "command".
bool LoadMenuBarFromResource(HINSTANCE module, UINT resource_id, MenuBar* out);
// Same for a context menu resource: returns the first popup.
bool LoadPopupFromResource(HINSTANCE module, UINT resource_id, Menu* out);

// ---------------------------------------------------------------------------
// Popup tracking
// ---------------------------------------------------------------------------

struct TrackResult {
  enum Kind { Dismissed, Command, NavLeft, NavRight, SwitchTo };
  Kind kind;
  int  command;       // Kind == Command
  int  index;         // Kind == SwitchTo: menu bar index
  TrackResult() : kind(Dismissed), command(0), index(-1) {}
};

// Used when a popup is opened from a menu bar: lets the popup ask which
// bar entry is under the mouse, so that hovering switches menus.
struct BarContext {
  std::function<int(POINT screen)> hit_test;   // returns bar index or -1
  int current;
  BarContext() : current(-1) {}
};

// Shows the popup at screen position (x, y) (top-left corner) and runs a
// modal loop until the user picks something or dismisses it.
// "anchor_top": if the popup does not fit below y, it is shown above
// y - anchor_height instead.
TrackResult TrackPopup(HWND owner, Menu& menu, int x, int y, int anchor_height,
                       const BarContext* bar, bool select_first);

// Convenience for context menus: returns the chosen command or 0.
int TrackContextMenu(HWND owner, Menu& menu, POINT screen);

// ---------------------------------------------------------------------------
// Painting (shared by the popup window and the preview tool)
// ---------------------------------------------------------------------------

struct MenuLayout {
  std::vector<Gdiplus::RectF> rects;   // per item
  float width, height;
};
MenuLayout LayoutMenu(Gdiplus::Graphics& g, const Menu& menu, float scale);
void PaintMenu(Gdiplus::Graphics& g, const Menu& menu, const MenuLayout& layout,
               int hover, float scale, const Palette& colors);

}  // namespace modernui

#endif  // INC_MODERNUI_MENU_H
