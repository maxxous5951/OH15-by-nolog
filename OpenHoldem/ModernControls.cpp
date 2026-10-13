//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: MFC glue: themed versions of the standard controls.
//
//******************************************************************************

#include "stdafx.h"
#include "ModernControls.h"

#include "ModernUI/ModernGfx.h"

namespace modernglue {

using namespace modernui;
using namespace Gdiplus;

// ---------------------------------------------------------------------------
// CModernButton
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CModernButton, CButton)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_MOUSEMOVE()
  ON_MESSAGE(WM_MOUSELEAVE, &CModernButton::OnMouseLeave)
  ON_WM_SETFOCUS()
  ON_WM_KILLFOCUS()
  ON_WM_ENABLE()
END_MESSAGE_MAP()

void CModernButton::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  const Palette& c = Theme::Colors();
  const float s = DpiScale(m_hWnd);
  const float w = (float)paint.Width(), h = (float)paint.Height();

  // The parent's background shows through the rounded corners.
  SolidBrush bg(Rgba(c.window_bg));
  g.FillRectangle(&bg, RectF(0, 0, w, h));

  const bool enabled = (IsWindowEnabled() != FALSE);
  const bool checked = (GetCheck() == BST_CHECKED);
  const bool pushed  = enabled && ((GetState() & BST_PUSHED) != 0);
  const bool hot     = enabled && _hot;
  const bool focused = (::GetFocus() == m_hWnd);

  RectF r(0.5f, 0.5f, w - 1.0f, h - 1.0f);
  const float radius = 8.0f * s;
  DWORD text = c.text;
  if (!enabled) {
    FillRoundRect(g, r, radius, Rgba(c.surface));
    StrokeRoundRect(g, r, radius, Rgba(c.border), 1.0f);
    text = c.text_disabled;
  } else if (_primary) {
    FillRoundRect(g, r, radius, Rgba(c.accent));
    if (hot || pushed) FillRoundRect(g, r, radius, WithAlpha(0xFFFFFFFF, pushed ? 0x30 : 0x1C));
    text = c.accent_text;
  } else if (checked) {
    // Toggled push-like check box ("Auto"): green, like the active flags.
    FillRoundRect(g, r, radius, Rgba(c.success_soft));
    StrokeRoundRect(g, r, radius, Rgba(c.success), 1.5f);
    text = c.success;
  } else {
    DWORD fill = pushed ? c.surface_pressed : (hot ? c.surface_hover : c.surface);
    FillRoundRect(g, r, radius, Rgba(fill));
    StrokeRoundRect(g, r, radius, Rgba(c.border_strong), 1.0f);
  }
  if (focused && enabled) {
    RectF f(r.X + 2, r.Y + 2, r.Width - 4, r.Height - 4);
    StrokeRoundRect(g, f, radius - 2, WithAlpha(_primary ? c.accent_text : c.accent, 0xB0), 1.2f);
  }

  CString caption;
  GetWindowText(caption);
  // Strip the mnemonic marker: "&Close" -> "Close" (one "&&" stays "&").
  std::wstring label;
  std::wstring raw = ToWide((LPCTSTR)caption);
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == L'&') {
      if (i + 1 < raw.size() && raw[i + 1] == L'&') { label += L'&'; ++i; }
      continue;
    }
    label += raw[i];
  }
  DrawLabel(g, label, modernui::GetFont(12.5f * s, _primary || checked), r, Rgba(text), HAlign::Center, true);
}

void CModernButton::OnMouseMove(UINT nFlags, CPoint point) {
  if (!_tracking) {
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&tme);
    _tracking = true;
  }
  if (!_hot) { _hot = true; Invalidate(FALSE); }
  CButton::OnMouseMove(nFlags, point);
}

LRESULT CModernButton::OnMouseLeave(WPARAM, LPARAM) {
  _tracking = false;
  if (_hot) { _hot = false; Invalidate(FALSE); }
  return 0;
}

void CModernButton::OnSetFocus(CWnd* pOld)  { CButton::OnSetFocus(pOld);  Invalidate(FALSE); }
void CModernButton::OnKillFocus(CWnd* pNew) { CButton::OnKillFocus(pNew); Invalidate(FALSE); }
void CModernButton::OnEnable(BOOL bEnable)  { CButton::OnEnable(bEnable); Invalidate(FALSE); }

// ---------------------------------------------------------------------------
// CModernCheck
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CModernCheck, CButton)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_MOUSEMOVE()
  ON_MESSAGE(WM_MOUSELEAVE, &CModernCheck::OnMouseLeave)
  ON_WM_SETFOCUS()
  ON_WM_KILLFOCUS()
  ON_WM_ENABLE()
END_MESSAGE_MAP()

void CModernCheck::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  const Palette& c = Theme::Colors();
  const float s = DpiScale(m_hWnd);
  const float w = (float)paint.Width(), h = (float)paint.Height();

  SolidBrush bg(Rgba(c.window_bg));
  g.FillRectangle(&bg, RectF(0, 0, w, h));

  const bool enabled = (IsWindowEnabled() != FALSE);
  const bool checked = (GetCheck() == BST_CHECKED);
  const bool pushed  = enabled && ((GetState() & BST_PUSHED) != 0);
  const bool hot     = enabled && _hot;
  const bool focused = (::GetFocus() == m_hWnd);
  const DWORD type = GetStyle() & BS_TYPEMASK;
  const bool radio = (type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON);

  const float box = 18.0f * s;
  const RectF b(2.0f * s, (h - box) / 2.0f, box, box);
  const float cx = b.X + box / 2.0f, cy = b.Y + box / 2.0f;

  const BYTE alpha = enabled ? 0xFF : 0x70;
  if (checked) {
    const Color fill = WithAlpha(c.accent, alpha);
    if (radio) {
      FillCircle(g, cx, cy, box / 2.0f, fill);
      FillCircle(g, cx, cy, 3.6f * s, WithAlpha(c.accent_text, alpha));
    } else {
      FillRoundRect(g, b, 5.0f * s, fill);
      Pen pen(WithAlpha(c.accent_text, alpha), 2.0f * s);
      pen.SetLineJoin(LineJoinRound);
      pen.SetStartCap(LineCapRound);
      pen.SetEndCap(LineCapRound);
      PointF p[3] = { PointF(b.X + 4.6f * s, cy + 0.2f * s),
                      PointF(b.X + 7.7f * s, cy + 3.3f * s),
                      PointF(b.X + 13.4f * s, cy - 3.3f * s) };
      g.DrawLines(&pen, p, 3);
    }
    if (hot || pushed) {
      if (radio) FillCircle(g, cx, cy, box / 2.0f, WithAlpha(0xFFFFFFFF, pushed ? 0x30 : 0x18));
      else FillRoundRect(g, b, 5.0f * s, WithAlpha(0xFFFFFFFF, pushed ? 0x30 : 0x18));
    }
  } else {
    const DWORD fill = !enabled ? c.surface : (pushed ? c.surface_pressed : (hot ? c.surface_hover : c.surface));
    const DWORD edge = !enabled ? c.border : (hot ? c.accent : c.border_strong);
    if (radio) {
      FillCircle(g, cx, cy, box / 2.0f, Rgba(fill));
      Pen pen(Rgba(edge), 1.4f * s);
      g.DrawEllipse(&pen, RectF(b.X + 0.7f * s, b.Y + 0.7f * s, box - 1.4f * s, box - 1.4f * s));
    } else {
      FillRoundRect(g, b, 5.0f * s, Rgba(fill));
      StrokeRoundRect(g, b, 5.0f * s, Rgba(edge), 1.4f);
    }
  }
  if (focused && enabled) {
    RectF f(b.X - 3.0f * s, b.Y - 3.0f * s, box + 6.0f * s, box + 6.0f * s);
    StrokeRoundRect(g, f, radio ? (box + 6.0f * s) / 2.0f : 8.0f * s, WithAlpha(c.accent, 0x90), 1.2f);
  }

  CString caption;
  GetWindowText(caption);
  std::wstring label;
  std::wstring raw = ToWide((LPCTSTR)caption);
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == L'&') {
      if (i + 1 < raw.size() && raw[i + 1] == L'&') { label += L'&'; ++i; }
      continue;
    }
    label += raw[i];
  }
  const float text_x = b.X + box + 10.0f * s;
  DrawLabel(g, label, modernui::GetFont(13.0f * s, false), RectF(text_x, 0, w - text_x, h),
            Rgba(enabled ? c.text : c.text_disabled), HAlign::Left, true);
}

void CModernCheck::OnMouseMove(UINT nFlags, CPoint point) {
  if (!_tracking) {
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&tme);
    _tracking = true;
  }
  if (!_hot) { _hot = true; Invalidate(FALSE); }
  CButton::OnMouseMove(nFlags, point);
}

LRESULT CModernCheck::OnMouseLeave(WPARAM, LPARAM) {
  _tracking = false;
  if (_hot) { _hot = false; Invalidate(FALSE); }
  return 0;
}

void CModernCheck::OnSetFocus(CWnd* pOld)  { CButton::OnSetFocus(pOld);  Invalidate(FALSE); }
void CModernCheck::OnKillFocus(CWnd* pNew) { CButton::OnKillFocus(pNew); Invalidate(FALSE); }
void CModernCheck::OnEnable(BOOL bEnable)  { CButton::OnEnable(bEnable); Invalidate(FALSE); }

// ---------------------------------------------------------------------------
// CModernTabCtrl
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CModernTabCtrl, CTabCtrl)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_MOUSEMOVE()
  ON_MESSAGE(WM_MOUSELEAVE, &CModernTabCtrl::OnMouseLeave)
END_MESSAGE_MAP()

void CModernTabCtrl::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  const Palette& c = Theme::Colors();
  const float s = DpiScale(m_hWnd);
  SolidBrush bg(Rgba(c.window_bg));
  g.FillRectangle(&bg, RectF(0, 0, (float)paint.Width(), (float)paint.Height()));

  const int count = GetItemCount();
  const int selected = GetCurSel();
  for (int i = 0; i < count; ++i) {
    CRect rc;
    if (!GetItemRect(i, &rc)) continue;
    if (rc.right < 0 || rc.left > paint.Width()) continue;   // scrolled out of view
    char buffer[256] = { 0 };
    TCITEM item;
    ZeroMemory(&item, sizeof(item));
    item.mask = TCIF_TEXT;
    item.pszText = buffer;
    item.cchTextMax = (int)sizeof(buffer) - 1;
    GetItem(i, &item);

    RectF r((float)rc.left + 1 * s, (float)rc.top + 2 * s,
            (float)rc.Width() - 2 * s, (float)rc.Height() - 4 * s);
    DWORD fg = c.text_dim;
    if (i == selected) {
      FillRoundRect(g, r, 7 * s, Rgba(c.accent_soft));
      fg = c.accent;
    } else if (i == _hot) {
      FillRoundRect(g, r, 7 * s, Rgba(c.surface_hover));
      fg = c.text;
    }
    DrawLabel(g, ToWide(buffer), modernui::GetFont(12.0f * s, i == selected), r, Rgba(fg), HAlign::Center, true);
  }
}

void CModernTabCtrl::OnMouseMove(UINT nFlags, CPoint point) {
  if (!_tracking) {
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&tme);
    _tracking = true;
  }
  TCHITTESTINFO info;
  info.pt = point;
  info.flags = 0;
  int hot = HitTest(&info);
  if (hot != _hot) { _hot = hot; Invalidate(FALSE); }
  CTabCtrl::OnMouseMove(nFlags, point);
}

LRESULT CModernTabCtrl::OnMouseLeave(WPARAM, LPARAM) {
  _tracking = false;
  if (_hot != -1) { _hot = -1; Invalidate(FALSE); }
  return 0;
}

// ---------------------------------------------------------------------------
// Helpers for native controls
// ---------------------------------------------------------------------------

void ApplyControlTheme(HWND hwnd) {
  // uxtheme is loaded dynamically: no extra import library needed.
  typedef HRESULT (WINAPI *SetWindowThemeFn)(HWND, LPCWSTR, LPCWSTR);
  static HMODULE uxtheme = LoadLibraryW(L"uxtheme.dll");
  if (uxtheme == NULL || hwnd == NULL) return;
  static SetWindowThemeFn set_theme = (SetWindowThemeFn)GetProcAddress(uxtheme, "SetWindowTheme");
  if (set_theme == NULL) return;
  char class_name[64] = { 0 };
  GetClassNameA(hwnd, class_name, sizeof(class_name) - 1);
  const bool dark = Theme::IsDark();
  if (_stricmp(class_name, "Edit") == 0) {
    set_theme(hwnd, dark ? L"DarkMode_CFD" : L"CFD", NULL);
  } else {
    // Trees, list boxes, Scintilla: dark scrollbars and selection colours
    set_theme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", NULL);
  }
}

void ApplyTreeColors(CTreeCtrl* tree) {
  if (tree == NULL || !::IsWindow(tree->GetSafeHwnd())) return;
  ApplyControlTheme(tree->GetSafeHwnd());
  const Palette& c = Theme::Colors();
  tree->SetBkColor(ToColorRef(c.surface));
  tree->SetTextColor(ToColorRef(c.text));
  tree->SetLineColor(ToColorRef(c.border_strong));
  tree->SetItemHeight((SHORT)(22.0f * DpiScale(tree->GetSafeHwnd()) + 0.5f));
}

void ApplyEditColors(CEdit* edit) {
  if (edit == NULL || !::IsWindow(edit->GetSafeHwnd())) return;
  ApplyControlTheme(edit->GetSafeHwnd());
  edit->Invalidate();
}

}  // namespace modernglue
