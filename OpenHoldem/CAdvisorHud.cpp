//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: HUD of the advisor mode, see CAdvisorHud.h
//
//******************************************************************************

#include "stdafx.h"
#include "CAdvisorHud.h"

#include <string>
#include <vector>
#include "CAutoConnector.h"
#include "CBetroundCalculator.h"
#include "CDisplayAffinity.h"
#include "CEngineContainer.h"
#include "CPlayer.h"
#include "CStatsDatabase.h"
#include "CSymbolengineUserchair.h"
#include "CTableState.h"
#include "HudFormat.h"
#include "ModernUI/ModernGfx.h"
#include "ModernUI/ModernTheme.h"

using namespace modernui;
using namespace Gdiplus;

namespace {

CString ToCString(const std::string& text) {
  return CString(text.c_str());
}


// Statistics of the preferences for the betround (1 = preflop ... 4 = river)
std::string HudStatsOfBetround(int betround) {
  switch (betround) {
    case kBetroundFlop:  return Preferences()->hud_stats_flop();
    case kBetroundTurn:  return Preferences()->hud_stats_turn();
    case kBetroundRiver: return Preferences()->hud_stats_river();
    default:             return Preferences()->hud_stats_preflop();
  }
}

// Region above which the label is placed: balance of the player, else his name
bool FindChairRegion(int chair, CRect* rect) {
  if (p_tablemap == NULL) return false;
  const char* patterns[] = { "p%dbalance", "u%dbalance", "p%dname", "u%dname" };
  for (int i = 0; i < 4; ++i) {
    CString name;
    name.Format(patterns[i], chair);
    RMapCI it = p_tablemap->r$()->find(name.GetString());
    if (it != p_tablemap->r$()->end()) {
      rect->SetRect((int)it->second.left, (int)it->second.top,
                    (int)it->second.right, (int)it->second.bottom);
      return true;
    }
  }
  return false;
}

const BYTE kHudAlpha = 232;

}  // namespace

// ---------------------------------------------------------------------------
// One label
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CHudLabel, CWnd)
  ON_WM_CREATE()
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_DESTROY()
  ON_MESSAGE(modernui::WMU_THEME_CHANGED, &CHudLabel::OnThemeChanged)
END_MESSAGE_MAP()

CHudLabel::CHudLabel() : _colour(0xFF8A8F98) {
}

CHudLabel::~CHudLabel() {
  if (m_hWnd != NULL) DestroyWindow();
}

bool CHudLabel::CreateLabel() {
  if (m_hWnd != NULL) return true;
  LPCTSTR class_name = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
    ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
  // Click-through (transparent), never activated, no taskbar button
  const BOOL ok = CWnd::CreateEx(
    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
    class_name, "OpenHoldem HUD", WS_POPUP, CRect(0, 0, 10, 10), NULL, 0);
  return ok != FALSE;
}

int CHudLabel::OnCreate(LPCREATESTRUCT lpCreateStruct) {
  if (CWnd::OnCreate(lpCreateStruct) == -1) return -1;
  ::SetLayeredWindowAttributes(m_hWnd, 0, kHudAlpha, LWA_ALPHA);
  modernui::Theme::RegisterWindow(m_hWnd);
  CDisplayAffinity::ApplyToWindow(m_hWnd);
  return 0;
}

void CHudLabel::OnDestroy() {
  modernui::Theme::UnregisterWindow(m_hWnd);
  CWnd::OnDestroy();
}

LRESULT CHudLabel::OnThemeChanged(WPARAM, LPARAM) {
  if (m_hWnd) Invalidate(FALSE);
  return 0;
}

void CHudLabel::SetContent(const CString& text, DWORD colour) {
  if (text == _text && colour == _colour) return;
  _text = text;
  _colour = colour;
  if (m_hWnd) Invalidate(FALSE);
}

int CHudLabel::Height() {
  return (int)(19 * DpiScale(m_hWnd) + 0.5f);
}

float CHudLabel::MeasureWidth() {
  const float s = DpiScale(m_hWnd);
  CClientDC dc(this);
  Graphics g(dc.GetSafeHdc());
  SetupQuality(g);
  const std::wstring text(CT2W((LPCTSTR)_text));
  // dot + padding + text
  return MeasureLabel(g, text, modernui::GetFont(11.5f * s, true)) + 26 * s;
}

void CHudLabel::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  SetupQuality(g);
  const Palette& c = Theme::Colors();
  const float s = DpiScale(m_hWnd);
  const float w = (float)paint.Width(), h = (float)paint.Height();
  const RectF box(0, 0, w, h);
  FillRoundRect(g, box, h / 2, Rgba(c.surface));
  StrokeRoundRect(g, box, h / 2, WithAlpha(_colour, 0xB0), 1.2f);
  FillCircle(g, 9 * s, h / 2, 3.5f * s, Rgba(_colour));
  const std::wstring text(CT2W((LPCTSTR)_text));
  DrawLabel(g, text, modernui::GetFont(11.5f * s, true),
            RectF(17 * s, 0, w - 21 * s, h), Rgba(c.text), HAlign::Left, false);
}

// ---------------------------------------------------------------------------
// The HUD
// ---------------------------------------------------------------------------

CAdvisorHud::CAdvisorHud() {
  modernui::GdiplusAcquire();
  _table_window = NULL;
  for (int i = 0; i < kHudChairs; ++i) {
    _shown[i] = false;
    _last_rect[i].SetRectEmpty();
  }
}

CAdvisorHud::~CAdvisorHud() {
  for (int i = 0; i < kHudChairs; ++i) {
    if (_labels[i].m_hWnd != NULL) _labels[i].DestroyWindow();
  }
  modernui::GdiplusRelease();
}

void CAdvisorHud::Reset() {
  CSLock lock(_critsec);
  for (int i = 0; i < kHudChairs; ++i) _data[i] = HudChairData();
}

// Heartbeat thread
void CAdvisorHud::OnHeartbeat() {
  HudChairData fresh[kHudChairs];
  const bool want = Preferences()->advisor_enabled() && Preferences()->hud_enabled()
    && p_autoconnector != NULL && p_autoconnector->IsConnectedToAnything()
    && p_table_state != NULL && p_betround_calculator != NULL;
  CStatsDatabase* database = want ? StatsDatabase() : NULL;
  if (database != NULL) {
    int betround = p_betround_calculator->betround();
    if (betround < kBetroundPreflop || betround > kBetroundRiver) betround = kBetroundPreflop;
    const std::vector<std::string> stats = ohstats::ParseHudStatList(HudStatsOfBetround(betround));
    const bool show_names = Preferences()->hud_show_names();
    CSymbolEngineUserchair* userchair_engine = p_engine_container->symbol_engine_userchair();
    const int userchair = (userchair_engine != NULL && userchair_engine->userchair_confirmed())
      ? userchair_engine->userchair() : -1;
    for (int chair = 0; chair < kHudChairs; ++chair) {
      if (chair == userchair) continue;
      CPlayer* player = p_table_state->Player(chair);
      if (player == NULL || player->name().IsEmpty()) continue;
      HudChairData& d = fresh[chair];
      if (!FindChairRegion(chair, &d.balance)) continue;
      const double hands = database->GetStatByName(chair, "hands");
      if (hands < 0) {
        // Not in the database (yet)
        d.text = "new";
        d.colour = ohstats::HudTypeColor(ohstats::kHudTypeUnknown);
      } else {
        std::vector<double> values;
        for (size_t i = 0; i < stats.size(); ++i) {
          values.push_back(database->GetStatByName(chair, stats[i]));
        }
        d.text = ToCString(ohstats::BuildHudLine(stats, values, hands, show_names));
        const double vpip = database->GetStatByName(chair, "vpip");
        const double pfr = database->GetStatByName(chair, "pfr");
        d.colour = ohstats::HudTypeColor(ohstats::ClassifyHudPlayer(vpip, pfr, hands));
      }
      d.visible = !d.text.IsEmpty();
    }
  }
  CSLock lock(_critsec);
  for (int i = 0; i < kHudChairs; ++i) _data[i] = fresh[i];
}

void CAdvisorHud::HideAll() {
  for (int i = 0; i < kHudChairs; ++i) {
    if (_shown[i] && _labels[i].m_hWnd != NULL) _labels[i].ShowWindow(SW_HIDE);
    _shown[i] = false;
  }
}

// GUI thread
void CAdvisorHud::OnGuiTimer() {
  bool want = Preferences()->advisor_enabled() && Preferences()->hud_enabled()
    && p_autoconnector != NULL && p_autoconnector->IsConnectedToAnything();
  HWND table = want ? p_autoconnector->attached_hwnd() : NULL;
  if (table == NULL || !::IsWindow(table) || !::IsWindowVisible(table) || ::IsIconic(table)) {
    HideAll();
    _table_window = NULL;
    return;
  }
  POINT origin = { 0, 0 };
  CRect client;
  if (!::GetClientRect(table, &client) || !::ClientToScreen(table, &origin)) {
    HideAll();
    return;
  }
  const bool new_table = (table != _table_window);
  if (new_table) {
    // Owned by the table: stays on top of it, minimizes with it.
    // Changing the owner of a visible window does not update its z-order: hide first.
    HideAll();
    _table_window = table;
  }
  HudChairData data[kHudChairs];
  {
    CSLock lock(_critsec);
    for (int i = 0; i < kHudChairs; ++i) data[i] = _data[i];
  }
  for (int chair = 0; chair < kHudChairs; ++chair) {
    const HudChairData& d = data[chair];
    CHudLabel& label = _labels[chair];
    if (!d.visible || d.balance.IsRectEmpty()) {
      if (_shown[chair] && label.m_hWnd != NULL) label.ShowWindow(SW_HIDE);
      _shown[chair] = false;
      continue;
    }
    if (label.m_hWnd == NULL && !label.CreateLabel()) continue;
    if (new_table || ::GetWindow(label.m_hWnd, GW_OWNER) != table) {
      ::SetWindowLongPtr(label.m_hWnd, GWLP_HWNDPARENT, (LONG_PTR)table);
    }
    label.SetContent(d.text, d.colour);
    const int w = (int)(label.MeasureWidth() + 0.5f);
    const int h = label.Height();
    const float s = DpiScale(label.m_hWnd);
    // Centered above the balance, inside the table
    int x = origin.x + (d.balance.left + d.balance.right) / 2 - w / 2;
    int y = origin.y + d.balance.top - h - (int)(2 * s + 0.5f);
    if (x < origin.x) x = origin.x;
    if (x + w > origin.x + client.Width()) x = origin.x + client.Width() - w;
    if (y < origin.y) y = origin.y;
    const CRect target(x, y, x + w, y + h);
    if (!_shown[chair] || _last_rect[chair] != target) {
      const bool resized = (_last_rect[chair].Size() != target.Size());
      label.SetWindowPos(NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
      if (resized) {
        HRGN region = ::CreateRoundRectRgn(0, 0, w + 1, h + 1, h, h);
        label.SetWindowRgn(region, TRUE);
      }
      _last_rect[chair] = target;
    }
    if (!_shown[chair]) {
      label.ShowWindow(SW_SHOWNOACTIVATE);
      label.SetWindowPos(&CWnd::wndTop, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
      _shown[chair] = true;
    }
  }
}
