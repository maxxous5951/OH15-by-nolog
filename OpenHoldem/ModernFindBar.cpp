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
//******************************************************************************

#include "stdafx.h"
#include "ModernFindBar.h"

#include "ModernControls.h"
#include "ModernUI/ModernGfx.h"

namespace modernglue {

using namespace modernui;
using namespace Gdiplus;

// ---------------------------------------------------------------------------
// CFindEdit
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CFindEdit, CEdit)
  ON_WM_GETDLGCODE()
  ON_WM_KEYDOWN()
  ON_WM_CHAR()
END_MESSAGE_MAP()

UINT CFindEdit::OnGetDlgCode() {
  return DLGC_WANTALLKEYS | DLGC_HASSETSEL;
}

void CFindEdit::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  if (nChar == VK_RETURN || nChar == VK_ESCAPE || nChar == VK_TAB) {
    CWnd* parent = GetParent();
    if (parent != NULL) {
      parent->SendMessage(WMU_FINDBAR_KEY, nChar, (GetKeyState(VK_SHIFT) < 0) ? 1 : 0);
    }
    return;
  }
  CEdit::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CFindEdit::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags) {
  // No beep for the keys that the bar handles
  if (nChar == VK_RETURN || nChar == VK_ESCAPE || nChar == VK_TAB) return;
  CEdit::OnChar(nChar, nRepCnt, nFlags);
}

// ---------------------------------------------------------------------------
// CFindBar
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CFindBar, CWnd)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_LBUTTONDOWN()
  ON_WM_LBUTTONUP()
  ON_WM_MOUSEMOVE()
  ON_MESSAGE(WM_MOUSELEAVE, &CFindBar::OnMouseLeave)
  ON_MESSAGE(WMU_FINDBAR_KEY, &CFindBar::OnEditKey)
  ON_WM_CTLCOLOR()
  ON_EN_CHANGE(kFindEditId, &CFindBar::OnFindTextChanged)
  ON_EN_SETFOCUS(kFindEditId, &CFindBar::OnEditFocusChanged)
  ON_EN_KILLFOCUS(kFindEditId, &CFindBar::OnEditFocusChanged)
  ON_EN_SETFOCUS(kReplaceEditId, &CFindBar::OnEditFocusChanged)
  ON_EN_KILLFOCUS(kReplaceEditId, &CFindBar::OnEditFocusChanged)
END_MESSAGE_MAP()

CFindBar::CFindBar() {
  _hot = _pressed = -1;
  _total = _current = 0;
  _replace_mode = false;
  _match_case = false;
  _whole_word = false;
  _not_found = false;
  _tracking = false;
  _suppress_change = false;
  _editor_rect = CRect(0, 0, 0, 0);
  _find_field = _replace_field = _counter_rect = RectF(0, 0, 0, 0);
}

BOOL CFindBar::CreateBar(CWnd* parent, UINT id) {
  LPCTSTR class_name = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
  if (!CWnd::CreateEx(0, class_name, "", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                      CRect(0, 0, 480, 60), parent, id)) {
    return FALSE;
  }
  const float s = DpiScale(m_hWnd);
  _font.CreateFont(-(int)(13 * s + 0.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

  const DWORD edit_style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
  _find_edit.Create(edit_style, CRect(0, 0, 10, 10), this, kFindEditId);
  _replace_edit.Create(edit_style, CRect(0, 0, 10, 10), this, kReplaceEditId);
  _find_edit.SetFont(&_font);
  _replace_edit.SetFont(&_font);
  _find_edit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"Find");
  _replace_edit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"Replace");
  _replace_edit.ShowWindow(SW_HIDE);
  ApplyTheme();
  return TRUE;
}

void CFindBar::ApplyTheme() {
  if (m_hWnd == NULL) return;
  const Palette& c = Theme::Colors();
  _brush_field.DeleteObject();
  _brush_field.CreateSolidBrush(ToColorRef(c.surface));
  ApplyControlTheme(_find_edit.GetSafeHwnd());
  ApplyControlTheme(_replace_edit.GetSafeHwnd());
  Invalidate(FALSE);
  _find_edit.Invalidate();
  _replace_edit.Invalidate();
}

CString CFindBar::FindText() {
  CString text;
  _find_edit.GetWindowText(text);
  return text;
}

CString CFindBar::ReplaceText() {
  CString text;
  _replace_edit.GetWindowText(text);
  return text;
}

// ---------------------------------------------------------------------------
// Show / hide / position
// ---------------------------------------------------------------------------

void CFindBar::ShowBar(const CString& initial_text) {
  if (!initial_text.IsEmpty()) {
    _suppress_change = true;
    _find_edit.SetWindowText(initial_text);
    _suppress_change = false;
  }
  _not_found = false;
  if (!_editor_rect.IsRectNull()) PositionIn(_editor_rect);
  ShowWindow(SW_SHOW);
  SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  _find_edit.SetFocus();
  _find_edit.SetSel(0, -1);
  RefreshCount();
}

void CFindBar::HideBar() {
  if (m_hWnd == NULL || !IsWindowVisible()) return;
  ShowWindow(SW_HIDE);
  if (_events.closed) _events.closed();
}

int CFindBar::BarHeight(float s) const {
  const float pad = 8 * s, row = 30 * s, gap = 6 * s;
  return (int)((_replace_mode ? (pad + row + gap + row + pad) : (pad + row + pad)) + 0.5f);
}

void CFindBar::PositionIn(const CRect& editor_rect) {
  if (m_hWnd == NULL) return;
  _editor_rect = editor_rect;
  const float s = DpiScale(m_hWnd);
  const int margin = (int)(10 * s + 0.5f);
  const int scrollbar = (int)(18 * s + 0.5f);
  int width = (int)(520 * s + 0.5f);
  const int available = editor_rect.Width() - 2 * margin - scrollbar;
  if (width > available) width = available;
  if (width < (int)(320 * s)) width = (int)(320 * s);
  const int height = BarHeight(s);
  const int x = editor_rect.right - margin - scrollbar - width;
  const int y = editor_rect.top + margin;
  SetWindowPos(&CWnd::wndTop, x, y, width, height, SWP_NOACTIVATE);

  // Rounded corners
  const int radius = (int)(8 * s + 0.5f);
  HRGN region = ::CreateRoundRectRgn(0, 0, width + 1, height + 1, radius * 2, radius * 2);
  SetWindowRgn(region, TRUE);   // the system owns the region from now on
  Layout();
}

void CFindBar::SetReplaceMode(bool replace_mode) {
  _replace_mode = replace_mode;
  if (!_editor_rect.IsRectNull()) PositionIn(_editor_rect); else Layout();
  if (_replace_mode) _replace_edit.SetFocus(); else _find_edit.SetFocus();
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void CFindBar::Layout() {
  if (m_hWnd == NULL) return;
  CRect client;
  GetClientRect(&client);
  const float s = DpiScale(m_hWnd);
  const float w = (float)client.Width();
  const float pad = 8 * s, row = 30 * s, btn = 28 * s, gap = 4 * s;
  const float y1 = pad;
  _elements.clear();

  float x = pad;
  _elements.push_back({ kToggleReplace, RectF(x, y1, 22 * s, row) });
  x += 22 * s + gap;

  // Right part, from the right edge towards the left
  float xr = w - pad;
  xr -= btn; _elements.push_back({ kClose, RectF(xr, y1, btn, row) });
  xr -= btn; _elements.push_back({ kNext,  RectF(xr, y1, btn, row) });
  xr -= btn; _elements.push_back({ kPrev,  RectF(xr, y1, btn, row) });
  xr -= gap;
  xr -= btn; _elements.push_back({ kWord,  RectF(xr, y1, btn, row) });
  xr -= btn; _elements.push_back({ kCase,  RectF(xr, y1, btn, row) });
  xr -= gap;
  const float counter_width = 76 * s;
  xr -= counter_width;
  _counter_rect = RectF(xr, y1, counter_width, row);
  xr -= gap;
  _find_field = RectF(x, y1, xr - x, row);

  const float field_pad = 8 * s;
  const int edit_height = (int)(18 * s + 0.5f);
  _find_edit.MoveWindow((int)(_find_field.X + field_pad),
                        (int)(_find_field.Y + (row - edit_height) / 2),
                        (int)(_find_field.Width - 2 * field_pad), edit_height);

  if (_replace_mode) {
    const float y2 = y1 + row + 6 * s;
    const float w_all = 84 * s, w_one = 72 * s;
    const float x_all = w - pad - w_all;
    const float x_one = x_all - gap - w_one;
    _elements.push_back({ kReplaceOne, RectF(x_one, y2, w_one, row) });
    _elements.push_back({ kReplaceAll, RectF(x_all, y2, w_all, row) });
    _replace_field = RectF(x, y2, x_one - gap - x, row);
    _replace_edit.MoveWindow((int)(_replace_field.X + field_pad),
                             (int)(_replace_field.Y + (row - edit_height) / 2),
                             (int)(_replace_field.Width - 2 * field_pad), edit_height);
    _replace_edit.ShowWindow(SW_SHOW);
  } else {
    _replace_edit.ShowWindow(SW_HIDE);
  }
  Invalidate(FALSE);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void CFindBar::PaintField(Graphics& g, const RectF& field, bool focused, bool error) {
  const Palette& c = Theme::Colors();
  const float s = DpiScale(m_hWnd);
  FillRoundRect(g, field, 6 * s, Rgba(c.surface));
  DWORD border = c.border_strong;
  if (error) border = c.danger;
  else if (focused) border = c.accent;
  StrokeRoundRect(g, field, 6 * s, Rgba(border), focused || error ? 1.5f : 1.0f);
}

static void DrawChevron(Graphics& g, const Color& color, float cx, float cy, float s, int direction) {
  // direction: 0 = right, 1 = down, 2 = up
  Pen pen(color, 1.7f * s);
  pen.SetLineJoin(LineJoinRound);
  pen.SetStartCap(LineCapRound);
  pen.SetEndCap(LineCapRound);
  PointF p[3];
  const float a = 4.0f * s, b = 2.2f * s;
  if (direction == 0)      { p[0] = PointF(cx - b, cy - a); p[1] = PointF(cx + b, cy);     p[2] = PointF(cx - b, cy + a); }
  else if (direction == 1) { p[0] = PointF(cx - a, cy - b); p[1] = PointF(cx,     cy + b); p[2] = PointF(cx + a, cy - b); }
  else                     { p[0] = PointF(cx - a, cy + b); p[1] = PointF(cx,     cy - b); p[2] = PointF(cx + a, cy + b); }
  g.DrawLines(&pen, p, 3);
}

void CFindBar::PaintElement(Graphics& g, const Element& e, bool hot, bool pressed) {
  const Palette& c = Theme::Colors();
  const float s = DpiScale(m_hWnd);
  const RectF& r = e.rect;
  const float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2;
  const float radius = 6 * s;

  switch (e.kind) {
    case kToggleReplace: {
      if (hot) FillRoundRect(g, r, radius, Rgba(pressed ? c.surface_pressed : c.surface_hover));
      DrawChevron(g, Rgba(hot ? c.text : c.text_dim), cx, cy, s, _replace_mode ? 1 : 0);
      break;
    }
    case kCase:
    case kWord: {
      const bool on = (e.kind == kCase) ? _match_case : _whole_word;
      if (on) {
        FillRoundRect(g, r, radius, Rgba(c.accent_soft));
        StrokeRoundRect(g, r, radius, Rgba(c.accent), 1.0f);
      } else if (hot) {
        FillRoundRect(g, r, radius, Rgba(pressed ? c.surface_pressed : c.surface_hover));
      }
      DrawLabel(g, e.kind == kCase ? L"Aa" : L"|ab|", modernui::GetFont(12.0f * s, on), r,
                Rgba(on ? c.accent : (hot ? c.text : c.text_dim)), HAlign::Center, false);
      break;
    }
    case kPrev:
    case kNext: {
      if (hot) FillRoundRect(g, r, radius, Rgba(pressed ? c.surface_pressed : c.surface_hover));
      DrawChevron(g, Rgba(hot ? c.text : c.text_dim), cx, cy, s, e.kind == kPrev ? 2 : 1);
      break;
    }
    case kClose: {
      if (hot) FillRoundRect(g, r, radius, Rgba(pressed ? c.surface_pressed : c.surface_hover));
      RectF icon(cx - 7 * s, cy - 7 * s, 14 * s, 14 * s);
      modernui::DrawIcon(g, Icon::Close, icon, Rgba(hot ? c.text : c.text_dim), 2.0f);
      break;
    }
    case kReplaceOne:
    case kReplaceAll: {
      FillRoundRect(g, r, radius, Rgba(pressed ? c.surface_pressed : (hot ? c.surface_hover : c.surface)));
      StrokeRoundRect(g, r, radius, Rgba(c.border_strong), 1.0f);
      DrawLabel(g, e.kind == kReplaceOne ? L"Replace" : L"Replace all", modernui::GetFont(12.5f * s, false),
                r, Rgba(c.text), HAlign::Center, true);
      break;
    }
  }
}

void CFindBar::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  const Palette& c = Theme::Colors();
  const float s = DpiScale(m_hWnd);
  const float w = (float)paint.Width(), h = (float)paint.Height();

  SolidBrush background(Rgba(c.menu_bg));
  g.FillRectangle(&background, RectF(0, 0, w, h));
  StrokeRoundRect(g, RectF(0.5f, 0.5f, w - 1, h - 1), 8 * s, Rgba(c.menu_border), 1.0f);

  const HWND focus = ::GetFocus();
  PaintField(g, _find_field, focus == _find_edit.GetSafeHwnd(), _not_found);
  if (_replace_mode) {
    PaintField(g, _replace_field, focus == _replace_edit.GetSafeHwnd(), false);
  }

  // "3 of 12", "No results"
  std::wstring counter;
  DWORD counter_color = c.text_dim;
  if (!FindText().IsEmpty()) {
    wchar_t buffer[64];
    if (_total <= 0) {
      counter = L"No results";
      counter_color = c.danger;
    } else if (_current > 0) {
      swprintf_s(buffer, L"%d of %d", _current, _total);
      counter = buffer;
    } else {
      swprintf_s(buffer, L"%d found", _total);
      counter = buffer;
    }
  }
  DrawLabel(g, counter, modernui::GetFont(12.0f * s, false), _counter_rect,
            Rgba(counter_color), HAlign::Center, true);

  for (size_t i = 0; i < _elements.size(); ++i) {
    PaintElement(g, _elements[i], (int)i == _hot, (int)i == _pressed && (int)i == _hot);
  }
}

HBRUSH CFindBar::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
  if (nCtlColor == CTLCOLOR_EDIT && _brush_field.GetSafeHandle() != NULL) {
    const Palette& c = Theme::Colors();
    pDC->SetTextColor(ToColorRef(c.text));
    pDC->SetBkColor(ToColorRef(c.surface));
    return (HBRUSH)_brush_field.GetSafeHandle();
  }
  return CWnd::OnCtlColor(pDC, pWnd, nCtlColor);
}

void CFindBar::OnEditFocusChanged() {
  Invalidate(FALSE);
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

int CFindBar::HitTest(CPoint point) const {
  for (size_t i = 0; i < _elements.size(); ++i) {
    if (_elements[i].rect.Contains((float)point.x, (float)point.y)) return (int)i;
  }
  return -1;
}

void CFindBar::OnMouseMove(UINT nFlags, CPoint point) {
  if (!_tracking) {
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&tme);
    _tracking = true;
  }
  const int hot = HitTest(point);
  if (hot != _hot) { _hot = hot; Invalidate(FALSE); }
  CWnd::OnMouseMove(nFlags, point);
}

LRESULT CFindBar::OnMouseLeave(WPARAM, LPARAM) {
  _tracking = false;
  if (_hot != -1) { _hot = -1; Invalidate(FALSE); }
  return 0;
}

void CFindBar::OnLButtonDown(UINT nFlags, CPoint point) {
  _pressed = HitTest(point);
  if (_pressed >= 0) SetCapture();
  Invalidate(FALSE);
  CWnd::OnLButtonDown(nFlags, point);
}

void CFindBar::OnLButtonUp(UINT nFlags, CPoint point) {
  const int pressed = _pressed;
  _pressed = -1;
  if (GetCapture() == this) ReleaseCapture();
  Invalidate(FALSE);
  if (pressed >= 0 && pressed == HitTest(point) && pressed < (int)_elements.size()) {
    Activate(_elements[pressed].kind);
  }
  CWnd::OnLButtonUp(nFlags, point);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void CFindBar::Activate(Kind kind) {
  switch (kind) {
    case kToggleReplace:
      SetReplaceMode(!_replace_mode);
      break;
    case kCase:
      _match_case = !_match_case;
      RunFind(true, true);
      Invalidate(FALSE);
      break;
    case kWord:
      _whole_word = !_whole_word;
      RunFind(true, true);
      Invalidate(FALSE);
      break;
    case kPrev:
      RunFind(false, false);
      break;
    case kNext:
      RunFind(true, false);
      break;
    case kClose:
      HideBar();
      break;
    case kReplaceOne:
      if (_events.replace_one && !FindText().IsEmpty()) {
        _events.replace_one(FindText(), ReplaceText(), _match_case, _whole_word);
        RefreshCount();
      }
      break;
    case kReplaceAll:
      if (_events.replace_all && !FindText().IsEmpty()) {
        _events.replace_all(FindText(), ReplaceText(), _match_case, _whole_word);
        _not_found = false;
        RefreshCount();
      }
      break;
  }
}

bool CFindBar::RunFind(bool forward, bool incremental) {
  const CString what = FindText();
  if (what.IsEmpty()) {
    _total = _current = 0;
    _not_found = false;
    Invalidate(FALSE);
    return false;
  }
  bool found = false;
  if (_events.find) found = _events.find(what, _match_case, _whole_word, forward, incremental);
  _not_found = !found;
  RefreshCount();
  return found;
}

void CFindBar::RefreshCount() {
  _total = _current = 0;
  const CString what = FindText();
  if (!what.IsEmpty() && _events.count) {
    _events.count(what, _match_case, _whole_word, &_total, &_current);
  }
  if (m_hWnd) Invalidate(FALSE);
}

void CFindBar::OnFindTextChanged() {
  if (_suppress_change) return;
  RunFind(true, true);   // incremental search while typing
}

LRESULT CFindBar::OnEditKey(WPARAM key, LPARAM shift) {
  const bool in_replace = (::GetFocus() == _replace_edit.GetSafeHwnd());
  switch (key) {
    case VK_RETURN:
      if (in_replace && _replace_mode) {
        Activate(kReplaceOne);
      } else {
        RunFind(shift == 0, false);
      }
      break;
    case VK_ESCAPE:
      HideBar();
      break;
    case VK_TAB:
      if (_replace_mode) {
        if (in_replace) _find_edit.SetFocus(); else _replace_edit.SetFocus();
      }
      break;
  }
  return 0;
}

}  // namespace modernglue
