//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Advisor mode, see CAdvisor.h
//
//******************************************************************************

#include "stdafx.h"
#include "CAdvisor.h"

#include <math.h>
#include "AllinAdjustment.h"
#include "BetpotCalculations.h"
#include "CAutoConnector.h"
#include "CAutoplayer.h"
#include "CAutoplayerFunctions.h"
#include "CBetroundCalculator.h"
#include "CDisplayAffinity.h"
#include "CEngineContainer.h"
#include "CFlagsToolbar.h"
#include "CFunctionCollection.h"
#include "CSymbolEngineActiveDealtPlaying.h"
#include "CSymbolEngineAutoplayer.h"
#include "CSymbolEngineChipAmounts.h"
#include "CSymbolEngineTableLimits.h"
#include "CSymbolEngineUserchair.h"
#include "CTableState.h"
#include "MainFrm.h"
#include "ModernUI/ModernGfx.h"
#include "ModernUI/ModernTheme.h"
#include "resource.h"

using namespace modernui;
using namespace Gdiplus;

CAdvisor *p_advisor = NULL;

namespace {

const UINT WMU_ADVISOR_UPDATE = WM_APP + 0x0470;
const UINT_PTR kAdvisorTimerId = 1;
const UINT kAdvisorTimerMilliseconds = 30;      // follows the table smoothly

// Amounts are compared with a small tolerance
const double kEpsilon = 0.005;

std::wstring ToWideString(const CString& text) {
  return std::wstring(CT2W((LPCTSTR)text));
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CAdvisor, CWnd)
  ON_WM_CREATE()
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_TIMER()
  ON_WM_LBUTTONDOWN()
  ON_WM_MOUSEMOVE()
  ON_WM_LBUTTONUP()
  ON_WM_CAPTURECHANGED()
  ON_WM_SETCURSOR()
  ON_WM_DESTROY()
  ON_MESSAGE(WMU_ADVISOR_UPDATE, &CAdvisor::OnAdvisorUpdate)
  ON_MESSAGE(modernui::WMU_THEME_CHANGED, &CAdvisor::OnThemeChanged)
END_MESSAGE_MAP()

CAdvisor::CAdvisor() {
  modernui::GdiplusAcquire();
  _dragging = false;
  _table_window = NULL;
  _last_table_rect.SetRectEmpty();
  _visible_state = false;
  _created = false;
}

CAdvisor::~CAdvisor() {
  if (m_hWnd != NULL) {
    DestroyWindow();
  }
  modernui::GdiplusRelease();
}

bool CAdvisor::CreateAdvisorWindow(CWnd* main_window) {
  if (_created) return true;
  LPCTSTR class_name = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
    ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
  // Tool window: no taskbar button. No-activate: clicking / dragging it
  // must never take the focus away from the poker table.
  // Layered: slightly translucent.
  const BOOL ok = CWnd::CreateEx(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
    class_name, "OpenHoldem Advisor", WS_POPUP, CRect(0, 0, 10, 10), NULL, 0);
  _created = (ok != FALSE);
  return _created;
}

int CAdvisor::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CWnd::OnCreate(lpCreateStruct) == -1) return -1;
  ::SetLayeredWindowAttributes(m_hWnd, 0, 244, LWA_ALPHA);
  modernui::Theme::RegisterWindow(m_hWnd);
  CDisplayAffinity::ApplyToWindow(m_hWnd);
  SetTimer(kAdvisorTimerId, kAdvisorTimerMilliseconds, NULL);
  return 0;
}

void CAdvisor::OnDestroy() {
  KillTimer(kAdvisorTimerId);
  modernui::Theme::UnregisterWindow(m_hWnd);
  CWnd::OnDestroy();
  _created = false;
}

// ---------------------------------------------------------------------------
// Switching the mode on and off (GUI thread)
// ---------------------------------------------------------------------------

bool CAdvisor::IsEnabled() {
  return Preferences()->advisor_enabled();
}

void CAdvisor::SetEnabled(bool enabled) {
  Preferences()->SetValue(k_prefs_advisor_enabled, enabled ? 1.0 : 0.0);
  RefreshFromPreferences();
}

void CAdvisor::RefreshFromPreferences() {
  // Keep the toolbar button in sync (preferences dialog, startup)
  if (p_flags_toolbar != NULL) {
    p_flags_toolbar->CheckButton(ID_MAIN_TOOLBAR_ADVISOR, IsEnabled());
  }
  if (!IsEnabled()) {
    Reset();
  }
  if (m_hWnd != NULL) {
    UpdateVisibilityAndPosition();
  }
}

void CAdvisor::Reset() {
  {
    CSLock lock(_data_critsec);
    _data = AdvisorData();
  }
  if (m_hWnd != NULL && ::IsWindow(m_hWnd)) {
    ::PostMessage(m_hWnd, WMU_ADVISOR_UPDATE, 0, 0);
  }
}

// ---------------------------------------------------------------------------
// Heartbeat thread: the recommendation and the action of the user
// ---------------------------------------------------------------------------

CString CAdvisor::MoneyText(double amount) {
  CString text;
  if (fabs(amount - floor(amount + 0.5)) < kEpsilon) {
    text.Format("%.0f", amount);
  } else {
    text.Format("%.2f", amount);
  }
  return text;
}

void CAdvisor::ClassifyAction(const CString& raw, double betsize, double to_call,
    AdvisorActionKind* kind, CString* text) {
  // "raw" is what the autoplayer-log says: "f$call", "f$betsize = 12.50",
  // "f$betpot_2_3", "no action", ...
  CString name = raw;
  name.Trim();
  name.MakeLower();
  const int equals = name.Find('=');
  if (equals > 0) name = name.Left(equals);
  name.Trim();
  if (name.Left(2) == "f$") name = name.Mid(2);
  const bool facing_a_bet = (to_call > kEpsilon);
  *kind = kAdvisorActionNone;
  *text = "";
  if (name == "fold") {
    *kind = kAdvisorActionFold;  *text = "FOLD";
  } else if (name == "check") {
    *kind = kAdvisorActionCheck; *text = "CHECK";
  } else if (name == "call") {
    *kind = kAdvisorActionCall;
    *text = facing_a_bet ? ("CALL " + MoneyText(to_call)) : CString("CALL");
  } else if (name == "allin") {
    *kind = kAdvisorActionAllin; *text = "ALL-IN";
  } else if (name == "raise") {
    *kind = kAdvisorActionRaise; *text = facing_a_bet ? "RAISE" : "BET";
  } else if (name == "betsize") {
    *kind = kAdvisorActionRaise;
    CString verb = facing_a_bet ? "RAISE to " : "BET ";
    *text = (betsize > kEpsilon) ? (verb + MoneyText(betsize)) : CString(facing_a_bet ? "RAISE" : "BET");
  } else if (name.Left(7) == "betpot_") {
    *kind = kAdvisorActionRaise;
    CString fraction = name.Mid(7);
    CString label = fraction;
    if (fraction == "2_1") label = "2x pot";
    else if (fraction == "1_1") label = "pot";
    else if (fraction == "3_4") label = "3/4 pot";
    else if (fraction == "2_3") label = "2/3 pot";
    else if (fraction == "1_2") label = "1/2 pot";
    else if (fraction == "1_3") label = "1/3 pot";
    else if (fraction == "1_4") label = "1/4 pot";
    *text = (facing_a_bet ? "RAISE " : "BET ") + label;
  } else {
    *text = "No action";
  }
}

void CAdvisor::OnDecision(CString best_action, CString action_taken,
    double pot, double to_call, double betsize) {
  if (!Preferences()->advisor_enabled()) return;
  AdvisorData fresh;
  ClassifyAction(best_action, betsize, to_call, &fresh.best_kind, &fresh.best_text);
  ClassifyAction(action_taken, betsize, to_call, &fresh.taken_kind, &fresh.taken_text);
  CString detail = "Pot " + MoneyText(pot);
  if (to_call > kEpsilon) {
    detail += "   |   To call " + MoneyText(to_call);
  }
  {
    CSLock lock(_data_critsec);
    _data.best_kind = fresh.best_kind;
    _data.best_text = fresh.best_text;
    _data.taken_kind = fresh.taken_kind;
    _data.taken_text = fresh.taken_text;
    _data.detail_text = detail;
    _data.my_turn = true;
  }
  if (m_hWnd != NULL && ::IsWindow(m_hWnd)) {
    ::PostMessage(m_hWnd, WMU_ADVISOR_UPDATE, 0, 0);
  }
}

void CAdvisor::OnHeartbeat() {
  if (!Preferences()->advisor_enabled()) {
    return;
  }
  const bool my_turn = p_engine_container->symbol_engine_userchair()->userchair_confirmed()
    && p_engine_container->symbol_engine_autoplayer()->ismyturn();
  const bool engaged = (p_autoplayer != NULL && p_autoplayer->autoplayer_engaged());
  bool changed = false;
  {
    CSLock lock(_data_critsec);
    if (_data.my_turn != my_turn || _data.autoplayer_engaged != engaged) {
      _data.my_turn = my_turn;
      _data.autoplayer_engaged = engaged;
      changed = true;
    }
  }
  if (changed && m_hWnd != NULL && ::IsWindow(m_hWnd)) {
    ::PostMessage(m_hWnd, WMU_ADVISOR_UPDATE, 0, 0);
  }
}

// ---------------------------------------------------------------------------
// GUI thread: position, visibility, dragging
// ---------------------------------------------------------------------------

LRESULT CAdvisor::OnAdvisorUpdate(WPARAM, LPARAM) {
  if (m_hWnd) Invalidate(FALSE);
  return 0;
}

LRESULT CAdvisor::OnThemeChanged(WPARAM, LPARAM) {
  if (m_hWnd) Invalidate(FALSE);
  return 0;
}

CSize CAdvisor::WindowSize() {
  const float s = modernui::DpiScale(m_hWnd);
  return CSize((int)(268 * s + 0.5f), (int)(172 * s + 0.5f));
}

bool CAdvisor::TableClientRectOnScreen(CRect* rect) {
  if (_table_window == NULL || !::IsWindow(_table_window)) return false;
  CRect client;
  if (!::GetClientRect(_table_window, &client)) return false;
  POINT top_left = { 0, 0 };
  if (!::ClientToScreen(_table_window, &top_left)) return false;
  rect->SetRect(top_left.x, top_left.y, top_left.x + client.Width(), top_left.y + client.Height());
  return !rect->IsRectEmpty();
}

CPoint CAdvisor::CornerPosition(const CRect& table, int corner) {
  const float s = modernui::DpiScale(m_hWnd);
  const int margin = (int)(12 * s + 0.5f);
  const CSize size = WindowSize();
  const bool left = (corner == kAdvisorTopLeft || corner == kAdvisorBottomLeft);
  const bool top = (corner == kAdvisorTopLeft || corner == kAdvisorTopRight);
  int x = left ? table.left + margin : table.right - margin - size.cx;
  int y = top ? table.top + margin : table.bottom - margin - size.cy;
  // The table can be smaller than the advisor
  if (x < table.left) x = table.left;
  if (y < table.top) y = table.top;
  return CPoint(x, y);
}

int CAdvisor::NearestCorner(const CRect& table, const CRect& advisor) {
  const bool right = advisor.CenterPoint().x > table.CenterPoint().x;
  const bool bottom = advisor.CenterPoint().y > table.CenterPoint().y;
  if (bottom) return right ? kAdvisorBottomRight : kAdvisorBottomLeft;
  return right ? kAdvisorTopRight : kAdvisorTopLeft;
}

void CAdvisor::UpdateVisibilityAndPosition() {
  if (m_hWnd == NULL) return;
  // Visible only: advisor enabled, connected to a table, table visible
  bool want = Preferences()->advisor_enabled()
    && p_autoconnector != NULL && p_autoconnector->IsConnectedToAnything();
  HWND table = want ? p_autoconnector->attached_hwnd() : NULL;
  if (table == NULL || !::IsWindow(table) || !::IsWindowVisible(table) || ::IsIconic(table)) {
    want = false;
  }
  if (!want) {
    if (_visible_state) {
      ShowWindow(SW_HIDE);
      _visible_state = false;
    }
    _dragging = false;
    return;
  }
  if (table != _table_window) {
    // Owned by the table: stays on top of it, minimizes with it,
    // but does not cover other applications.
    _table_window = table;
    ::SetWindowLongPtr(m_hWnd, GWLP_HWNDPARENT, (LONG_PTR)table);
  }
  CRect table_client;
  if (!TableClientRectOnScreen(&table_client)) return;
  const CSize size = WindowSize();
  CRect current;
  GetWindowRect(&current);
  if (!_dragging) {
    const CPoint target = CornerPosition(table_client, Preferences()->advisor_corner());
    if (current.TopLeft() != target || current.Size() != size) {
      const bool resized = (current.Size() != size);
      SetWindowPos(NULL, target.x, target.y, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
      if (resized) {
        // Round corners (the window owns the region afterwards)
        const float s = modernui::DpiScale(m_hWnd);
        const int radius = (int)(28 * s + 0.5f);   // diameter of the corner circle
        HRGN region = ::CreateRoundRectRgn(0, 0, size.cx + 1, size.cy + 1, radius, radius);
        SetWindowRgn(region, TRUE);
      }
    }
  }
  _last_table_rect = table_client;
  if (!_visible_state) {
    ShowWindow(SW_SHOWNOACTIVATE);
    _visible_state = true;
  }
}

void CAdvisor::OnTimer(UINT_PTR id) {
  if (id == kAdvisorTimerId) {
    UpdateVisibilityAndPosition();
    return;
  }
  CWnd::OnTimer(id);
}

void CAdvisor::OnLButtonDown(UINT nFlags, CPoint point) {
  _dragging = true;
  _drag_offset = point;
  SetCapture();
}

void CAdvisor::OnMouseMove(UINT nFlags, CPoint point) {
  if (!_dragging) return;
  CPoint cursor;
  ::GetCursorPos(&cursor);
  SetWindowPos(NULL, cursor.x - _drag_offset.x, cursor.y - _drag_offset.y, 0, 0,
    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CAdvisor::OnLButtonUp(UINT nFlags, CPoint point) {
  if (!_dragging) return;
  _dragging = false;
  ::ReleaseCapture();
  CRect table_client;
  if (TableClientRectOnScreen(&table_client)) {
    CRect advisor;
    GetWindowRect(&advisor);
    // Snap to the nearest corner and remember it
    const int corner = NearestCorner(table_client, advisor);
    Preferences()->SetValue(k_prefs_advisor_corner, (double)corner);
    UpdateVisibilityAndPosition();
  }
}

void CAdvisor::OnCaptureChanged(CWnd* pWnd) {
  if (_dragging) {
    // Lost the capture (another window, Alt+Tab): drop the window where it is
    _dragging = false;
    CRect table_client;
    if (TableClientRectOnScreen(&table_client)) {
      UpdateVisibilityAndPosition();
    }
  }
  CWnd::OnCaptureChanged(pWnd);
}

BOOL CAdvisor::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) {
  ::SetCursor(::LoadCursor(NULL, IDC_SIZEALL));
  return TRUE;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void CAdvisor::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  SetupQuality(g);
  const Palette& c = Theme::Colors();
  const float s = DpiScale(m_hWnd);
  const float w = (float)paint.Width(), h = (float)paint.Height();

  AdvisorData d;
  {
    CSLock lock(_data_critsec);
    d = _data;
  }

  auto kind_colour = [&c](AdvisorActionKind kind) -> DWORD {
    switch (kind) {
      case kAdvisorActionFold:  return c.danger;
      case kAdvisorActionCheck: return c.accent;
      case kAdvisorActionCall:  return c.accent;
      case kAdvisorActionRaise: return c.success;
      case kAdvisorActionAllin: return c.warning;
      default:                  return c.text_dim;
    }
  };

  // Card
  FillRoundRect(g, RectF(0, 0, w, h), 14 * s, Rgba(c.surface));
  StrokeRoundRect(g, RectF(0, 0, w, h), 14 * s, Rgba(c.border_strong), 1.0f);

  // Header: icon, title, status
  DrawIcon(g, Icon::Lightbulb, RectF(14 * s, 11 * s, 17 * s, 17 * s), Rgba(c.accent), 2.0f);
  DrawLabel(g, L"Advisor", GetFont(13 * s, true), RectF(37 * s, 8 * s, 90 * s, 22 * s),
            Rgba(c.text), HAlign::Left, true);
  std::wstring status;
  DWORD status_colour = c.text_dim;
  if (d.autoplayer_engaged)  { status = L"Autoplayer on"; status_colour = c.warning; }
  else if (d.my_turn)        { status = L"Your turn";     status_colour = c.success; }
  else                       { status = L"Waiting";       status_colour = c.text_dim; }
  const Font* status_font = GetFont(11.5f * s, false);
  const float status_width = MeasureLabel(g, status, status_font);
  const float status_right = w - 16 * s;
  DrawLabel(g, status, status_font, RectF(status_right - status_width - 2 * s, 8 * s, status_width + 4 * s, 22 * s),
            Rgba(status_colour), HAlign::Right, false);
  FillCircle(g, status_right - status_width - 10 * s, 19 * s, 3.5f * s, Rgba(status_colour));
  {
    Pen separator(Rgba(c.border), 1.0f);
    g.DrawLine(&separator, 14 * s, 36 * s, w - 14 * s, 36 * s);
  }

  // Best action
  const Font* caption_font = GetFont(10.5f * s, true);
  DrawLabel(g, L"BEST ACTION", caption_font, RectF(16 * s, 44 * s, w - 32 * s, 16 * s),
            Rgba(c.text_dim), HAlign::Left, false);
  const RectF pill(12 * s, 62 * s, w - 24 * s, 40 * s);
  const bool has_best = (d.best_kind != kAdvisorActionNone);
  const bool active = d.my_turn && has_best;
  const DWORD best_colour = kind_colour(d.best_kind);
  FillRoundRect(g, pill, 10 * s, active ? WithAlpha(best_colour, 0x2E) : Rgba(c.surface_hover));
  if (active) {
    StrokeRoundRect(g, pill, 10 * s, WithAlpha(best_colour, 0x80), 1.2f);
  }
  CString best_text = d.best_text;
  if (best_text.IsEmpty()) best_text = d.autoplayer_engaged ? "Autoplayer plays" : "-";
  DrawLabel(g, ToWideString(best_text), GetFont(18 * s, true),
            RectF(pill.X + 12 * s, pill.Y, pill.Width - 24 * s, pill.Height),
            Rgba(has_best ? (d.my_turn ? best_colour : c.text_dim) : c.text_dim), HAlign::Left, true);

  // Action taken
  DrawLabel(g, L"ACTION TAKEN", caption_font, RectF(16 * s, 110 * s, w - 32 * s, 16 * s),
            Rgba(c.text_dim), HAlign::Left, false);
  CString taken_text = d.taken_text;
  const bool has_taken = (d.taken_kind != kAdvisorActionNone);
  if (taken_text.IsEmpty()) taken_text = "-";
  DWORD taken_colour = has_taken ? kind_colour(d.taken_kind) : c.text_dim;
  DrawLabel(g, ToWideString(taken_text), GetFont(15 * s, true),
            RectF(16 * s, 127 * s, w - 32 * s, 22 * s), Rgba(taken_colour), HAlign::Left, true);

  // Details (pot, amount to call)
  DrawLabel(g, ToWideString(d.detail_text), GetFont(10.5f * s, false),
            RectF(16 * s, 150 * s, w - 32 * s, 16 * s), Rgba(c.text_dim), HAlign::Left, true);
}
