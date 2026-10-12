//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: The header of the main window, drawn as one flicker-free surface:
//   custom title bar (icon, title, theme button, connection pill, minimize,
//   close), menu bar, main toolbar and the row of flag buttons.
//
//   Commands are sent to the parent window as WM_COMMAND(id, 0), exactly like
//   native menu items and toolbar buttons did, so existing message maps keep
//   working unchanged.
//
//******************************************************************************

#ifndef INC_MODERNUI_HEADER_H
#define INC_MODERNUI_HEADER_H

#include "ModernMenu.h"

namespace modernui {

struct ToolButtonDef {
  int          id;
  Icon         icon;
  const wchar_t* label;
  bool         checkable;      // sticky button (toggles on click)
  bool         success_tint;   // green instead of blue when checked
  bool         group_start;    // separator before this button
};

class ModernHeader {
 public:
  ModernHeader();
  ~ModernHeader();

  // ---- window ----
  bool Create(HWND parent, int control_id);
  HWND hwnd() const { return hwnd_; }

  // ---- configuration (call before the first paint) ----
  void SetMenuBar(const MenuBar& bar) { menus_ = bar; Rebuild(); }
  void SetMenuHooks(std::function<void(Menu&)> before_show) { before_show_ = before_show; }
  void SetCommandIconResolver(std::function<Icon(int)> resolver) { icon_for_ = resolver; }
  void SetAppIcon(HICON icon);
  void SetToolbarButtons(const std::vector<ToolButtonDef>& defs);
  void SetFlagCommandIds(const int* ids, int count);

  // ---- state ----
  void SetTitle(const std::wstring& title);
  void SetConnected(bool connected);
  void EnableButton(int id, bool enabled);
  void CheckButton(int id, bool checked);
  bool IsButtonEnabled(int id) const;
  bool IsButtonChecked(int id) const;

  // ---- geometry ----
  int  Height(float scale) const;          // total header height in pixels
  int  TitleBarHeight(float scale) const;

  // ---- keyboard access ----
  bool OpenMenuByMnemonic(wchar_t ch);

  // ---- painting without a window (also used by the preview tool) ----
  void Layout(Gdiplus::Graphics& g, int width, float scale);
  void Paint(Gdiplus::Graphics& g, int width, float scale);

 private:
  enum Kind { kMenu, kTool, kFlag, kTheme, kMinimize, kClose };
  struct Element {
    Kind kind;
    int  id;           // command id (tool/flag) or menu index
    Gdiplus::RectF rect;
    bool enabled, checked, success_tint, checkable, group_start;
    Icon icon;
    std::wstring label;
  };

  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);
  int  HitTest(int x, int y) const;
  bool IsDragArea(int x, int y) const;
  void OpenMenu(int menu_index, bool select_first);
  void ShowThemeMenu();
  void Activate(int element_index);
  void PostCommand(int id);
  void SetHot(int index);
  void Rebuild();
  Element* FindElement(Kind kind, int id);
  const Element* FindElement(Kind kind, int id) const;
  void Invalidate() { if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE); }

  void PaintTitleBar(Gdiplus::Graphics& g, const Palette& c);
  void PaintElement(Gdiplus::Graphics& g, const Palette& c, size_t index);

  HWND hwnd_, parent_;
  float scale_;
  int   width_;
  bool  relayout_;
  std::wstring title_;
  bool  connected_;
  Gdiplus::Bitmap* app_icon_;
  mutable CRITICAL_SECTION lock_;      // buttons are changed from the engine thread

  MenuBar menus_;
  std::vector<ToolButtonDef> tool_defs_;
  std::vector<int> flag_ids_;
  std::vector<Element> elements_;
  // Extra rectangles
  Gdiplus::RectF title_text_rect_, pill_rect_, flags_label_rect_;
  std::vector<float> separators_;      // x positions of the toolbar separators
  float title_h_, menu_h_, tool_h_, flags_h_;

  int hot_, pressed_, open_menu_;
  bool tracking_leave_;
  std::function<void(Menu&)> before_show_;
  std::function<Icon(int)> icon_for_;
};

// Preferred height of the status bar.
int StatusbarHeight(float scale);

class ModernStatusbar {
 public:
  ModernStatusbar();
  ~ModernStatusbar();
  bool Create(HWND parent, int control_id);
  HWND hwnd() const { return hwnd_; }
  void SetTexts(const std::wstring& action, const std::wstring& handrank, const std::wstring& prwin);
  static void Paint(Gdiplus::Graphics& g, int width, int height, float scale, const Palette& c,
                    const std::wstring& action, const std::wstring& handrank, const std::wstring& prwin);
 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  HWND hwnd_;
  std::wstring action_, handrank_, prwin_;
};

}  // namespace modernui

#endif  // INC_MODERNUI_HEADER_H
