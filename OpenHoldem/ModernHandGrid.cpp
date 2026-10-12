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
//******************************************************************************

#include "stdafx.h"
#include "ModernHandGrid.h"

#include "ModernUI/ModernGfx.h"
#include <math.h>

namespace modernglue {

using namespace modernui;
using namespace Gdiplus;

namespace {
const int kAceRank = 14;
const int kTwoRank = 2;
const int kCells   = 13;
const wchar_t kRankChars[] = L"23456789TJQKA";   // index = rank - 2

inline int IndexOfRank(int rank) { return kAceRank - rank; }
}  // namespace

BEGIN_MESSAGE_MAP(CHandGridWnd, CWnd)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_RBUTTONDOWN()
  ON_WM_RBUTTONUP()
  ON_WM_MOUSEMOVE()
  ON_MESSAGE(WM_MOUSELEAVE, &CHandGridWnd::OnMouseLeave)
  ON_WM_CAPTURECHANGED()
  ON_WM_KEYDOWN()
  ON_WM_CHAR()
  ON_WM_GETDLGCODE()
  ON_WM_SETFOCUS()
  ON_WM_KILLFOCUS()
END_MESSAGE_MAP()

CHandGridWnd::CHandGridWnd() {
  ZeroMemory(_selected, sizeof(_selected));
  _hot_row = _hot_col = -1;
  _focus_row = _focus_col = 0;
  _keyboard_focus = false;
  _tracking = false;
  _dragging = false;
  _drag_value = true;
  _drag_button = 0;
  _last_point = CPoint(0, 0);
}

BOOL CHandGridWnd::CreateGrid(CWnd* parent, const CRect& rect, UINT id) {
  LPCTSTR class_name = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
    ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
  return CWnd::CreateEx(0, class_name, "", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
    rect, parent, id);
}

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

bool CHandGridWnd::IsSelected(int first_rank, int second_rank) const {
  if (first_rank < kTwoRank || first_rank > kAceRank) return false;
  if (second_rank < kTwoRank || second_rank > kAceRank) return false;
  return _selected[first_rank][second_rank];
}

void CHandGridWnd::SetSelected(int first_rank, int second_rank, bool selected) {
  if (first_rank < kTwoRank || first_rank > kAceRank) return;
  if (second_rank < kTwoRank || second_rank > kAceRank) return;
  _selected[first_rank][second_rank] = selected;
  if (m_hWnd) Invalidate(FALSE);
}

void CHandGridWnd::SelectPairs() {
  for (int i = kTwoRank; i <= kAceRank; ++i) _selected[i][i] = true;
  if (m_hWnd) Invalidate(FALSE);
}

void CHandGridWnd::SelectBroadways() {
  for (int i = 10; i <= kAceRank; ++i) {
    for (int j = 10; j <= kAceRank; ++j) _selected[i][j] = true;
  }
  if (m_hWnd) Invalidate(FALSE);
}

void CHandGridWnd::SelectSuited() {
  for (int i = kTwoRank; i <= kAceRank; ++i) {
    for (int j = kTwoRank; j < i; ++j) _selected[i][j] = true;
  }
  if (m_hWnd) Invalidate(FALSE);
}

void CHandGridWnd::ClearAll() {
  ZeroMemory(_selected, sizeof(_selected));
  if (m_hWnd) Invalidate(FALSE);
}

int CHandGridWnd::HandCount() const {
  int count = 0;
  for (int i = kTwoRank; i <= kAceRank; ++i) {
    for (int j = kTwoRank; j <= kAceRank; ++j) {
      if (!_selected[i][j]) continue;
      if (i == j)     count += 6;    // pairs
      else if (i < j) count += 12;   // offsuit
      else            count += 4;    // suited
    }
  }
  return count;
}

std::wstring CHandGridWnd::LabelFor(int first_rank, int second_rank) {
  const int high = (first_rank > second_rank) ? first_rank : second_rank;
  const int low  = (first_rank > second_rank) ? second_rank : first_rank;
  std::wstring label;
  label += kRankChars[high - kTwoRank];
  label += kRankChars[low - kTwoRank];
  if (first_rank > second_rank)      label += L's';
  else if (first_rank < second_rank) label += L'o';
  return label;
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

void CHandGridWnd::GetMetrics(float* cell, float* gap, float* origin_x, float* origin_y) const {
  CRect client;
  GetClientRect(&client);
  const float scale = DpiScale(m_hWnd);
  *gap = 4.0f * scale;
  const float side = (float)((client.Width() < client.Height()) ? client.Width() : client.Height());
  *cell = (side - (kCells - 1) * (*gap)) / (float)kCells;
  if (*cell < 4.0f) *cell = 4.0f;
  const float total = kCells * (*cell) + (kCells - 1) * (*gap);
  *origin_x = ((float)client.Width()  - total) / 2.0f;
  *origin_y = ((float)client.Height() - total) / 2.0f;
}

RectF CHandGridWnd::CellRect(int row, int col) const {
  float cell, gap, ox, oy;
  GetMetrics(&cell, &gap, &ox, &oy);
  return RectF(ox + col * (cell + gap), oy + row * (cell + gap), cell, cell);
}

bool CHandGridWnd::CellAt(CPoint point, bool lenient, int* row, int* col) const {
  float cell, gap, ox, oy;
  GetMetrics(&cell, &gap, &ox, &oy);
  const float pitch = cell + gap;
  const float x = (float)point.x - ox;
  const float y = (float)point.y - oy;
  if (x < 0 || y < 0) return false;
  const int c = (int)floorf(x / pitch);
  const int r = (int)floorf(y / pitch);
  if (c >= kCells || r >= kCells) return false;
  if (!lenient) {
    if (x - c * pitch > cell || y - r * pitch > cell) return false;
  }
  *row = r;
  *col = c;
  return true;
}

void CHandGridWnd::InvalidateCell(int row, int col) {
  if (!m_hWnd) return;
  RectF r = CellRect(row, col);
  CRect rc((int)floorf(r.X) - 2, (int)floorf(r.Y) - 2,
           (int)ceilf(r.X + r.Width) + 2, (int)ceilf(r.Y + r.Height) + 2);
  InvalidateRect(&rc, FALSE);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void CHandGridWnd::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  const Palette& c = Theme::Colors();
  const float s = DpiScale(m_hWnd);

  SolidBrush background(Rgba(c.window_bg));
  g.FillRectangle(&background, RectF(0, 0, (float)paint.Width(), (float)paint.Height()));

  const bool focused = (::GetFocus() == m_hWnd);
  const float radius = 6.0f * s;
  for (int row = 0; row < kCells; ++row) {
    for (int col = 0; col < kCells; ++col) {
      const int first  = RankOfIndex(row);
      const int second = RankOfIndex(col);
      const bool selected = _selected[first][second];
      const bool hot = (row == _hot_row && col == _hot_col);
      const RectF r = CellRect(row, col);

      DWORD text = c.text_dim;
      if (selected) {
        FillRoundRect(g, r, radius, Rgba(c.accent));
        if (hot) FillRoundRect(g, r, radius, WithAlpha(0xFFFFFFFF, 0x24));
        text = c.accent_text;
      } else {
        FillRoundRect(g, r, radius, Rgba(hot ? c.surface_hover : c.surface));
        StrokeRoundRect(g, r, radius, Rgba(hot ? c.border_strong : c.border), 1.0f);
        if (hot) text = c.text;
      }
      if (focused && _keyboard_focus && row == _focus_row && col == _focus_col) {
        RectF f(r.X + 2 * s, r.Y + 2 * s, r.Width - 4 * s, r.Height - 4 * s);
        StrokeRoundRect(g, f, radius - 2 * s, Rgba(selected ? c.accent_text : c.accent), 1.5f);
      }
      DrawLabel(g, LabelFor(first, second), modernui::GetFont(13.0f * s, selected), r,
                Rgba(text), HAlign::Center, false);
    }
  }
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

bool CHandGridWnd::ApplyCell(int row, int col) {
  const int first  = RankOfIndex(row);
  const int second = RankOfIndex(col);
  if (_selected[first][second] == _drag_value) return false;
  _selected[first][second] = _drag_value;
  InvalidateCell(row, col);
  return true;
}

bool CHandGridWnd::ApplyLine(CPoint from, CPoint to) {
  // The mouse can move several cells between two WM_MOUSEMOVEs:
  // walk along the line, so that no hand gets skipped.
  float cell, gap, ox, oy;
  GetMetrics(&cell, &gap, &ox, &oy);
  const float dx = (float)(to.x - from.x);
  const float dy = (float)(to.y - from.y);
  const float length = sqrtf(dx * dx + dy * dy);
  int steps = (int)ceilf(length / (cell / 3.0f));
  if (steps < 1) steps = 1;
  bool changed = false;
  for (int i = 1; i <= steps; ++i) {
    CPoint p((int)(from.x + dx * i / steps + 0.5f), (int)(from.y + dy * i / steps + 0.5f));
    int row, col;
    if (CellAt(p, true, &row, &col)) {
      _hot_row = row;
      _hot_col = col;
      if (ApplyCell(row, col)) changed = true;
    }
  }
  return changed;
}

void CHandGridWnd::BeginDrag(CPoint point, int button) {
  _keyboard_focus = false;
  SetFocus();
  int row, col;
  if (!CellAt(point, false, &row, &col)) return;
  _dragging = true;
  _drag_button = button;
  _last_point = point;
  _focus_row = row;
  _focus_col = col;
  // The first hand decides: selected -> we deselect, otherwise we select.
  _drag_value = !_selected[RankOfIndex(row)][RankOfIndex(col)];
  SetCapture();
  if (ApplyCell(row, col)) NotifyChanged();
}

void CHandGridWnd::EndDrag(int button) {
  if (_dragging && _drag_button == button) {
    _dragging = false;
    _drag_button = 0;
    if (GetCapture() == this) ReleaseCapture();
  }
}

void CHandGridWnd::OnLButtonDown(UINT, CPoint point) { BeginDrag(point, 1); }
void CHandGridWnd::OnRButtonDown(UINT, CPoint point) { BeginDrag(point, 2); }
void CHandGridWnd::OnLButtonUp(UINT, CPoint)         { EndDrag(1); }
void CHandGridWnd::OnRButtonUp(UINT, CPoint)         { EndDrag(2); }

void CHandGridWnd::OnCaptureChanged(CWnd*) {
  _dragging = false;
  _drag_button = 0;
}

void CHandGridWnd::OnMouseMove(UINT nFlags, CPoint point) {
  if (!_tracking) {
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&tme);
    _tracking = true;
  }
  if (_dragging) {
    const bool button_down = (_drag_button == 1) ? ((nFlags & MK_LBUTTON) != 0)
                                                 : ((nFlags & MK_RBUTTON) != 0);
    if (!button_down) {
      EndDrag(_drag_button);   // the button was released outside of our window
    } else {
      const bool changed = ApplyLine(_last_point, point);
      _last_point = point;
      Invalidate(FALSE);       // hover highlight moved
      if (changed) NotifyChanged();
      return;
    }
  }
  int row = -1, col = -1;
  if (!CellAt(point, false, &row, &col)) { row = -1; col = -1; }
  if (row != _hot_row || col != _hot_col) {
    if (_hot_row >= 0) InvalidateCell(_hot_row, _hot_col);
    _hot_row = row;
    _hot_col = col;
    if (_hot_row >= 0) InvalidateCell(_hot_row, _hot_col);
  }
}

LRESULT CHandGridWnd::OnMouseLeave(WPARAM, LPARAM) {
  _tracking = false;
  if (!_dragging && _hot_row >= 0) {
    InvalidateCell(_hot_row, _hot_col);
    _hot_row = _hot_col = -1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------

UINT CHandGridWnd::OnGetDlgCode() {
  return DLGC_WANTARROWS | DLGC_WANTCHARS;
}

void CHandGridWnd::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  int row = _focus_row, col = _focus_col;
  switch (nChar) {
    case VK_LEFT:  if (col > 0)          --col; break;
    case VK_RIGHT: if (col < kCells - 1) ++col; break;
    case VK_UP:    if (row > 0)          --row; break;
    case VK_DOWN:  if (row < kCells - 1) ++row; break;
    case VK_HOME:  col = 0;          break;
    case VK_END:   col = kCells - 1; break;
    default:
      CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
      return;
  }
  _keyboard_focus = true;
  InvalidateCell(_focus_row, _focus_col);
  _focus_row = row;
  _focus_col = col;
  InvalidateCell(_focus_row, _focus_col);
}

void CHandGridWnd::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (nChar == ' ') {
    const int first  = RankOfIndex(_focus_row);
    const int second = RankOfIndex(_focus_col);
    _selected[first][second] = !_selected[first][second];
    _keyboard_focus = true;
    InvalidateCell(_focus_row, _focus_col);
    NotifyChanged();
    return;
  }
  CWnd::OnChar(nChar, nRepCnt, nFlags);
}

void CHandGridWnd::OnSetFocus(CWnd* pOld) {
  CWnd::OnSetFocus(pOld);
  Invalidate(FALSE);
}

void CHandGridWnd::OnKillFocus(CWnd* pNew) {
  CWnd::OnKillFocus(pNew);
  Invalidate(FALSE);
}

void CHandGridWnd::NotifyChanged() {
  CWnd* parent = GetParent();
  if (parent != NULL) {
    parent->SendMessage(WMU_HANDGRID_CHANGED, 0, (LPARAM)m_hWnd);
  }
}

}  // namespace modernglue
