//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: MFC glue: the 13x13 hand matrix of the hand list editor.
//
//   * One themed (light / dark) window, drawn with GDI+, instead of 169
//     check boxes.
//   * Pairs on the diagonal, suited hands above, offsuit hands below it.
//   * Click a hand to toggle it. Press the left OR the right mouse button and
//     move the mouse over other hands to select / deselect many hands at once:
//     the first hand decides (it was selected -> everything touched gets
//     deselected, it was not selected -> everything touched gets selected).
//   * Keyboard: arrow keys move, space toggles.
//
//   Ranks are 2..14 (k_rank_ace), like in COHScriptList::IsOnList(first, second):
//   first > second means suited, first < second means offsuit.
//
//******************************************************************************

#ifndef INC_MODERNHANDGRID_H
#define INC_MODERNHANDGRID_H

#include "ModernUI/ModernTheme.h"
#include <string>

namespace modernglue {

// Sent to the parent (SendMessage) whenever the user changed the selection.
const UINT WMU_HANDGRID_CHANGED = WM_APP + 0x0431;

// Child window id (outside of the ranges used by the dialog resources)
const int kHandGridControlId = 0xE9F2;

class CHandGridWnd : public CWnd {
 public:
  CHandGridWnd();
  BOOL CreateGrid(CWnd* parent, const CRect& rect, UINT id);

  // Ranks 2..14. "first" is the rank of the row, "second" the rank of the column.
  bool IsSelected(int first_rank, int second_rank) const;
  // Does not notify the parent
  void SetSelected(int first_rank, int second_rank, bool selected);
  void SelectPairs();
  void SelectBroadways();
  void SelectSuited();
  void ClearAll();
  // Number of combos (pairs = 6, suited = 4, offsuit = 12) of the selection
  int  HandCount() const;
  // "AA", "AKs", "AKo", "T9s", ...
  static std::wstring LabelFor(int first_rank, int second_rank);

 protected:
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnRButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
  afx_msg void OnCaptureChanged(CWnd* pWnd);
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg UINT OnGetDlgCode();
  afx_msg void OnSetFocus(CWnd* pOld);
  afx_msg void OnKillFocus(CWnd* pNew);
  DECLARE_MESSAGE_MAP()

 private:
  void  GetMetrics(float* cell, float* gap, float* origin_x, float* origin_y) const;
  Gdiplus::RectF CellRect(int row, int col) const;
  // lenient: the gap between two cells belongs to the cell on its left / above
  bool  CellAt(CPoint point, bool lenient, int* row, int* col) const;
  void  BeginDrag(CPoint point, int button);
  void  EndDrag(int button);
  bool  ApplyCell(int row, int col);               // true if something changed
  bool  ApplyLine(CPoint from, CPoint to);         // all cells between two points
  void  NotifyChanged();
  void  InvalidateCell(int row, int col);
  static int RankOfIndex(int index) { return 14 - index; }

  bool  _selected[15][15];   // [first_rank][second_rank], 0 and 1 unused
  int   _hot_row, _hot_col;
  int   _focus_row, _focus_col;
  bool  _keyboard_focus;
  bool  _tracking;
  bool  _dragging;
  bool  _drag_value;
  int   _drag_button;        // 1 = left, 2 = right
  CPoint _last_point;
};

}  // namespace modernglue

#endif  // INC_MODERNHANDGRID_H
