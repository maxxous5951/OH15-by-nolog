//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: MFC glue: themed Find / Replace bar of the formula editor.
//
//   Replaces the old native CFindReplaceDialog. The bar is a child window that
//   floats in the top right corner of the editor (like in modern code editors):
//
//     * incremental search while typing, with "x of n" match counter
//     * match case / whole word toggles
//     * Enter = next, Shift+Enter = previous, Esc = close
//     * optional replace row (arrow on the left): Replace / Replace all
//
//   The bar knows nothing about Scintilla: the formula editor provides the
//   actual search / replace operations through FindBarEvents.
//
//******************************************************************************

#ifndef INC_MODERNFINDBAR_H
#define INC_MODERNFINDBAR_H

#include "ModernUI/ModernTheme.h"
#include <functional>
#include <vector>

namespace modernglue {

// Sent by the edit fields to the bar: wParam = VK_RETURN / VK_ESCAPE / VK_TAB,
// lParam = 1 if Shift is pressed.
const UINT WMU_FINDBAR_KEY = WM_APP + 0x0440;

const int kFindBarControlId = 0xE9F3;
const int kFindEditId       = 0xE9F4;
const int kReplaceEditId    = 0xE9F5;

// Single line edit that hands Enter / Esc / Tab to the find bar
// (otherwise the formula dialog would treat them as OK / Cancel).
class CFindEdit : public CEdit {
 protected:
  afx_msg UINT OnGetDlgCode();
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
  DECLARE_MESSAGE_MAP()
};

struct FindBarEvents {
  // Select the next / previous match. incremental = the user is still typing
  // (search from the beginning of the current selection).
  // Returns true if something was found.
  std::function<bool(const CString& what, bool match_case, bool whole_word,
                     bool forward, bool incremental)> find;
  std::function<void(const CString& what, const CString& with,
                     bool match_case, bool whole_word)> replace_one;
  std::function<int(const CString& what, const CString& with,
                    bool match_case, bool whole_word)> replace_all;
  // Number of matches and (1-based) index of the current one, 0 if none
  std::function<void(const CString& what, bool match_case, bool whole_word,
                     int* total, int* current)> count;
  // The bar got closed by the user
  std::function<void()> closed;
};

class CFindBar : public CWnd {
 public:
  CFindBar();
  BOOL CreateBar(CWnd* parent, UINT id);
  void SetEvents(const FindBarEvents& events) { _events = events; }

  // Shows the bar (keeps the replace mode), focuses the search field.
  void ShowBar(const CString& initial_text);
  void HideBar();
  bool IsOpen() const { return m_hWnd != NULL && IsWindowVisible(); }
  // Puts the bar into the top right corner of the editor (client coordinates
  // of the parent) and in front of all siblings.
  void PositionIn(const CRect& editor_rect);
  void RefreshCount();
  void ApplyTheme();

 protected:
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
  afx_msg LRESULT OnEditKey(WPARAM, LPARAM);
  afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
  afx_msg void OnFindTextChanged();
  afx_msg void OnEditFocusChanged();
  DECLARE_MESSAGE_MAP()

 private:
  enum Kind { kToggleReplace, kCase, kWord, kPrev, kNext, kClose, kReplaceOne, kReplaceAll };
  struct Element {
    Kind kind;
    Gdiplus::RectF rect;
  };

  void  Layout();
  int   BarHeight(float scale) const;
  int   HitTest(CPoint point) const;
  void  Activate(Kind kind);
  void  SetReplaceMode(bool replace_mode);
  bool  RunFind(bool forward, bool incremental);
  CString FindText();
  CString ReplaceText();
  void  PaintField(Gdiplus::Graphics& g, const Gdiplus::RectF& field, bool focused, bool error);
  void  PaintElement(Gdiplus::Graphics& g, const Element& element, bool hot, bool pressed);

  CFindEdit _find_edit, _replace_edit;
  CFont     _font;
  CBrush    _brush_field;
  FindBarEvents _events;
  CRect     _editor_rect;
  std::vector<Element> _elements;
  Gdiplus::RectF _find_field, _replace_field, _counter_rect;
  int  _hot, _pressed;
  int  _total, _current;
  bool _replace_mode, _match_case, _whole_word, _not_found;
  bool _tracking, _suppress_change;
};

}  // namespace modernglue

#endif  // INC_MODERNFINDBAR_H
