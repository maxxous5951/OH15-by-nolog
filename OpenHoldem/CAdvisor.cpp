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
const int  kObservationHeartbeatsLimit = 12;    // then we give up to detect the action

// Amounts are compared with a small tolerance
const double kEpsilon = 0.005;

// Raise / allin are the same "family" when comparing with the real action
bool SameFamily(AdvisorActionKind a, AdvisorActionKind b) {
  if (a == b) return true;
  const bool a_aggressive = (a == kAdvisorActionRaise || a == kAdvisorActionAllin);
  const bool b_aggressive = (b == kAdvisorActionRaise || b == kAdvisorActionAllin);
  return a_aggressive && b_aggressive;
}

std::wstring ToWideString(const CString& text) {
  return std::wstring(CT2W((LPCTSTR)text));
}

// Betpot-functions in the order of the autoplayer, with their names
struct BetpotName { int function; const char* text; };
const BetpotName kBetpots[] = {
  { k_autoplayer_function_betpot_2_1, "2x pot" },
  { k_autoplayer_function_betpot_1_1, "pot" },
  { k_autoplayer_function_betpot_3_4, "3/4 pot" },
  { k_autoplayer_function_betpot_2_3, "2/3 pot" },
  { k_autoplayer_function_betpot_1_2, "1/2 pot" },
  { k_autoplayer_function_betpot_1_3, "1/3 pot" },
  { k_autoplayer_function_betpot_1_4, "1/4 pot" },
};

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
  _reset_requested = false;
  _waiting_for_user_action = false;
  _observation_heartbeats = 0;
  _turn_user_bet = 0;
  _turn_call = 0;
  _turn_user_balance = 0;
  _turn_pot = 0;
  _turn_betround = 0;
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
  _reset_requested = true;
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

CString CAdvisor::ActionText(AdvisorActionKind kind, double amount) {
  switch (kind) {
    case kAdvisorActionFold:  return "FOLD";
    case kAdvisorActionCheck: return "CHECK";
    case kAdvisorActionCall:
      return (amount > kEpsilon) ? ("CALL " + MoneyText(amount)) : CString("CALL");
    case kAdvisorActionRaise:
      return (amount > kEpsilon) ? ("RAISE to " + MoneyText(amount)) : CString("RAISE");
    case kAdvisorActionAllin: return "ALL-IN";
    default: return "";
  }
}

bool CAdvisor::ItIsMyTurnAndWeKnowTheAnswer() {
  return p_engine_container->symbol_engine_userchair()->userchair_confirmed()
    && p_engine_container->symbol_engine_autoplayer()->ismyturn()
    && p_engine_container->symbol_engine_autoplayer()->isfinalanswer();
}

void CAdvisor::EvaluateBestAction(AdvisorData* data) {
  // Same evaluation and same priorities as the autoplayer,
  // but nothing gets clicked.
  p_autoplayer_functions->CalcPrimaryFormulas();
  CSymbolEngineChipAmounts *chips = p_engine_container->symbol_engine_chip_amounts();
  const double to_call = chips->call();
  const bool facing_a_bet = (to_call > kEpsilon);

  data->best_kind = kAdvisorActionNone;
  data->best_text = "";
  // 1) all-in
  if (p_function_collection->EvaluateAutoplayerFunction(k_autoplayer_function_allin)) {
    data->best_kind = kAdvisorActionAllin;
    data->best_text = ActionText(kAdvisorActionAllin, 0);
  }
  // 2) pot-sized bets / raises
  if (data->best_kind == kAdvisorActionNone) {
    for (int i = 0; i < (int)(sizeof(kBetpots) / sizeof(kBetpots[0])); ++i) {
      const int function = kBetpots[i].function;
      if (!p_autoplayer_functions->GetAutoplayerFunctionValue(function)) continue;
      if (ChangeBetPotActionToAllin(function)) {
        data->best_kind = kAdvisorActionAllin;
        data->best_text = ActionText(kAdvisorActionAllin, 0);
      } else {
        const double amount = BetsizeForBetpot(function);
        data->best_kind = kAdvisorActionRaise;
        CString text = facing_a_bet ? "RAISE to " : "BET ";
        text += MoneyText(amount);
        text += CString("  (") + kBetpots[i].text + ")";
        data->best_text = text;
      }
      break;
    }
  }
  // 3) user-defined betsize
  if (data->best_kind == kAdvisorActionNone) {
    const double betsize = p_function_collection->EvaluateAutoplayerFunction(k_autoplayer_function_betsize);
    if (betsize > 0) {
      if (ChangeBetsizeToAllin(betsize)) {
        data->best_kind = kAdvisorActionAllin;
        data->best_text = ActionText(kAdvisorActionAllin, 0);
      } else {
        data->best_kind = kAdvisorActionRaise;
        data->best_text = (facing_a_bet ? "RAISE to " : "BET ") + MoneyText(betsize);
      }
    }
  }
  // 4) the elementary actions
  if (data->best_kind == kAdvisorActionNone) {
    for (int i = k_autoplayer_function_raise; i <= k_autoplayer_function_fold; ++i) {
      if (i == k_autoplayer_function_check && facing_a_bet) continue;  // impossible
      if (!p_function_collection->Evaluate(k_standard_function_names[i])) continue;
      switch (i) {
        case k_autoplayer_function_raise:
          data->best_kind = kAdvisorActionRaise;
          data->best_text = facing_a_bet ? "RAISE" : "BET";
          break;
        case k_autoplayer_function_call:
          data->best_kind = kAdvisorActionCall;
          data->best_text = ActionText(kAdvisorActionCall, to_call);
          break;
        case k_autoplayer_function_check:
          data->best_kind = kAdvisorActionCheck;
          data->best_text = ActionText(kAdvisorActionCheck, 0);
          break;
        case k_autoplayer_function_fold:
          data->best_kind = kAdvisorActionFold;
          data->best_text = ActionText(kAdvisorActionFold, 0);
          break;
      }
      break;
    }
  }
  if (data->best_kind == kAdvisorActionNone) {
    data->best_text = "No action";
  }
  data->detail_text = "Pot " + MoneyText(chips->pot());
  if (facing_a_bet) {
    data->detail_text += "   |   To call " + MoneyText(to_call);
  }
}

void CAdvisor::StartObservingUserAction() {
  CSymbolEngineChipAmounts *chips = p_engine_container->symbol_engine_chip_amounts();
  _waiting_for_user_action = true;
  _observation_heartbeats = 0;
  _turn_user_bet = p_table_state->User()->_bet.GetValue();
  _turn_user_balance = p_table_state->User()->_balance.GetValue();
  _turn_call = chips->call();
  _turn_pot = chips->pot();
  _turn_betround = p_betround_calculator->betround();
  // A new decision: forget what we saw last time
  CSLock lock(_data_critsec);
  _data.taken_kind = kAdvisorActionNone;
  _data.taken_text = "";
}

bool CAdvisor::ObserveUserAction(bool force) {
  // Called while it is not (or no longer) our turn. We compare the situation
  // with the one at the beginning of the turn.
  ++_observation_heartbeats;
  AdvisorActionKind kind = kAdvisorActionNone;
  CString text;
  if (p_engine_container->symbol_engine_userchair()->userchair_confirmed()) {
    CPlayer *user = p_table_state->User();
    const double bet_now = user->_bet.GetValue();
    const double balance_now = user->_balance.GetValue();
    const bool still_playing = (p_engine_container->symbol_engine_active_dealt_playing()->playersplayingbits()
      & p_engine_container->symbol_engine_userchair()->userchairbit()) != 0;
    const bool betround_changed = (p_betround_calculator->betround() != _turn_betround);
    const double total_to_call = _turn_user_bet + _turn_call;
    // Required number of heartbeats before we trust the observation:
    // a changed bet is very reliable, "nothing happened" is not.
    int required = 0;
    if (!betround_changed && bet_now > _turn_user_bet + kEpsilon) {
      required = 1;
      const bool went_allin = (balance_now <= kEpsilon) && (_turn_user_balance > kEpsilon);
      if (bet_now <= total_to_call + kEpsilon && _turn_call > kEpsilon) {
        kind = kAdvisorActionCall;
        text = ActionText(kind, bet_now - _turn_user_bet);
      } else if (went_allin) {
        kind = kAdvisorActionAllin;
        text = ActionText(kind, 0);
      } else {
        kind = kAdvisorActionRaise;
        text = ((_turn_call > kEpsilon) ? "RAISE to " : "BET ") + MoneyText(bet_now);
      }
    } else if (!still_playing) {
      required = 2;
      kind = kAdvisorActionFold;
      text = ActionText(kind, 0);
    } else if (betround_changed) {
      // The chips were already swept into the pot: the action closed the round
      required = 2;
      if (_turn_call > kEpsilon) {
        kind = kAdvisorActionCall;
        text = ActionText(kind, _turn_call);
      } else {
        kind = kAdvisorActionCheck;
        text = ActionText(kind, 0);
      }
    } else if (_turn_call <= kEpsilon) {
      required = 3;
      kind = kAdvisorActionCheck;
      text = ActionText(kind, 0);
    }
    if (kind != kAdvisorActionNone && _observation_heartbeats < required && !force) {
      return false;   // wait a bit longer
    }
  }
  if (kind == kAdvisorActionNone) {
    if (!force && _observation_heartbeats <= kObservationHeartbeatsLimit) {
      return false;
    }
    text = "not detected";
  }
  _waiting_for_user_action = false;
  CSLock lock(_data_critsec);
  _data.taken_kind = kind;
  _data.taken_text = text;
  return true;
}

void CAdvisor::OnHeartbeat() {
  if (_reset_requested) {
    _reset_requested = false;
    _waiting_for_user_action = false;
    _observation_heartbeats = 0;
  }
  if (!Preferences()->advisor_enabled()) {
    return;
  }
  bool changed = false;
  if (p_autoplayer != NULL && p_autoplayer->autoplayer_engaged()) {
    // The autoplayer takes the decisions, there is nothing to advise.
    CSLock lock(_data_critsec);
    if (!_data.autoplayer_engaged) {
      _data = AdvisorData();
      _data.autoplayer_engaged = true;
      changed = true;
    }
  } else if (p_engine_container->symbol_engine_userchair()->userchair_confirmed()
      && p_engine_container->symbol_engine_autoplayer()->ismyturn()) {
    if (!p_engine_container->symbol_engine_autoplayer()->isfinalanswer()) {
      // Still calculating (prwin simulation)
      CSLock lock(_data_critsec);
      _data.autoplayer_engaged = false;
      _data.my_turn = true;
      if (_data.best_text != "Analysing...") {
        _data.best_kind = kAdvisorActionNone;
        _data.best_text = "Analysing...";
        _data.detail_text = "";
        changed = true;
      }
    } else {
      if (_waiting_for_user_action && _observation_heartbeats > 0) {
        // Our turn again. Either a glitch of the scraper (nothing changed)
        // or already the next decision (something changed): conclude the old one first.
        CSymbolEngineChipAmounts *chips = p_engine_container->symbol_engine_chip_amounts();
        const bool something_changed =
          (p_betround_calculator->betround() != _turn_betround)
          || (fabs(p_table_state->User()->_bet.GetValue() - _turn_user_bet) > kEpsilon)
          || (fabs(chips->call() - _turn_call) > kEpsilon);
        if (something_changed) {
          ObserveUserAction(true);
        }
      }
      if (!_waiting_for_user_action) {
        StartObservingUserAction();
      }
      AdvisorData fresh;
      EvaluateBestAction(&fresh);
      CSLock lock(_data_critsec);
      _data.autoplayer_engaged = false;
      _data.my_turn = true;
      _data.best_kind = fresh.best_kind;
      _data.best_text = fresh.best_text;
      _data.detail_text = fresh.detail_text;
      changed = true;
    }
  } else {
    // Not our turn
    if (_waiting_for_user_action) {
      if (ObserveUserAction(false)) changed = true;
    }
    CSLock lock(_data_critsec);
    if (_data.my_turn || _data.autoplayer_engaged) {
      _data.my_turn = false;
      _data.autoplayer_engaged = false;
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
            RectF(16 * s, 127 * s, w - 64 * s, 22 * s), Rgba(taken_colour), HAlign::Left, true);
  if (has_taken && has_best) {
    // Did the user follow the advice?
    const bool followed = SameFamily(d.taken_kind, d.best_kind);
    DrawIcon(g, followed ? Icon::Check : Icon::About,
             RectF(w - 38 * s, 128 * s, 20 * s, 20 * s),
             Rgba(followed ? c.success : c.warning), 2.2f);
  }

  // Details (pot, amount to call)
  DrawLabel(g, ToWideString(d.detail_text), GetFont(10.5f * s, false),
            RectF(16 * s, 150 * s, w - 32 * s, 16 * s), Rgba(c.text_dim), HAlign::Left, true);
}
