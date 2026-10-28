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

#include <algorithm>
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
#include "ModernUI/ModernGfx.h"
#include "ModernUI/ModernTheme.h"

using namespace modernui;
using namespace Gdiplus;
using namespace ohstats;

namespace {

const BYTE kHudAlpha = 236;
const UINT_PTR kPreviewTimerId = 1;
const DWORD kDarkText = 0xFF0B0F14;

std::wstring Wide(const std::string& text) {
  return modernui::ToWide(text);
}

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

// Last line of the column: "Maniac - 119 hands"
std::string ColumnFooter(const HudText& t) {
  return t.type + " - " + (t.values.empty() ? std::string("0") : t.values.back()) + " hands";
}

int ClampStyle(int style) {
  return (style < 0 || style >= kHudNumberOfStyles) ? (int)kHudStylePill : style;
}

bool FindRegion(const char* pattern, int chair, CRect* rect) {
  if (p_tablemap == NULL) return false;
  CString name;
  name.Format(pattern, chair);
  RMapCI it = p_tablemap->r$()->find(name.GetString());
  if (it == p_tablemap->r$()->end()) return false;
  if (it->second.right <= it->second.left || it->second.bottom <= it->second.top) return false;
  rect->SetRect((int)it->second.left, (int)it->second.top, (int)it->second.right, (int)it->second.bottom);
  return true;
}

// Name + balance of the player (box of the seat), relative to the table client area
bool FindSeat(int chair, CRect* seat) {
  CRect name, balance;
  bool has_name = FindRegion("p%dname", chair, &name) || FindRegion("u%dname", chair, &name);
  bool has_balance = FindRegion("p%dbalance", chair, &balance) || FindRegion("u%dbalance", chair, &balance);
  if (has_name && has_balance) { seat->UnionRect(name, balance); return true; }
  if (has_name) { *seat = name; return true; }
  if (has_balance) { *seat = balance; return true; }
  return false;
}

HudRect ToHudRect(const CRect& r) {
  return HudRect(r.left, r.top, r.right, r.bottom);
}

// Everything on the table that a label must not cover: cards, bets, dealer button, board, pot
void CollectObstacles(std::vector<HudRect>* obstacles) {
  if (p_tablemap == NULL) return;
  for (RMapCI it = p_tablemap->r$()->begin(); it != p_tablemap->r$()->end(); ++it) {
    CString name = it->first;
    name.MakeLower();
    bool obstacle = false;
    if (name.Left(6) == "c0card" || name.Left(5) == "c0pot") {
      obstacle = true;
    } else if (name.GetLength() > 2 && (name[0] == 'p' || name[0] == 'u') && isdigit((unsigned char)name[1])) {
      const CString rest = name.Mid(2);
      obstacle = (rest.Left(4) == "card" || rest.Left(3) == "bet" || rest.Left(6) == "dealer");
    }
    if (!obstacle) continue;
    if (it->second.right <= it->second.left || it->second.bottom <= it->second.top) continue;
    obstacles->push_back(HudRect((int)it->second.left, (int)it->second.top,
                                 (int)it->second.right, (int)it->second.bottom));
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// Sizes and painting of the four styles
// ---------------------------------------------------------------------------

void HudLabelSize(Graphics& g, int style, const HudText& t, float s, bool expanded,
                  float* width, float* height) {
  const Font* font = modernui::GetFont(11.5f * s, true);
  const Font* small_font = modernui::GetFont(10.0f * s, false);
  const float compact_w = MeasureLabel(g, Wide(t.compact), font);
  switch (ClampStyle(style)) {
    case kHudStyleTab: {
      const float type_w = MeasureLabel(g, Wide(t.type), small_font);
      *width = (std::max)(104.0f * s, (std::max)(compact_w, type_w) + 16 * s);
      *height = 34 * s;
      break;
    }
    case kHudStyleColumn: {
      if (t.names.empty()) {
        *width = compact_w + 20 * s;
        *height = 22 * s;
      } else {
        // one row per statistic (without the "Hands" row at the end), then "Maniac - 119 hands"
        const size_t rows = t.names.size() - 1;
        float value_w = 0;
        for (size_t i = 0; i < rows; ++i) {
          value_w = (std::max)(value_w, MeasureLabel(g, Wide(t.values[i]), font));
        }
        const float footer_w = MeasureLabel(g, Wide(ColumnFooter(t)), small_font) + 18 * s;
        *width = (std::max)((std::max)(104.0f * s, 52 * s + value_w + 12 * s), footer_w);
        *height = (float)(rows + 1) * 14 * s + 10 * s;
      }
      break;
    }
    case kHudStyleBadge: {
      const float badge_w = MeasureLabel(g, Wide(t.badge), font) + 16 * s;
      *width = badge_w;
      *height = 17 * s;
      if (expanded && !t.names.empty()) {
        *width = (std::max)(badge_w, 134.0f * s);
        *height = 17 * s + 3 * s + (float)t.names.size() * 14 * s + 12 * s;
      }
      break;
    }
    default:
      *width = compact_w + 26 * s;
      *height = 19 * s;
      break;
  }
}

void DrawHudLabel(Graphics& g, int style, const HudText& t, float s, bool expanded,
                  float w, float h) {
  const Palette& c = Theme::Colors();
  const Font* font = modernui::GetFont(11.5f * s, true);
  const Font* small_font = modernui::GetFont(10.0f * s, false);
  const DWORD colour = t.colour;
  switch (ClampStyle(style)) {
    case kHudStyleTab: {
      const RectF box(0, 0, w, h);
      FillRoundRect(g, box, 7 * s, Rgba(c.surface));
      StrokeRoundRect(g, box, 7 * s, Rgba(c.border_strong), 1.0f);
      FillRoundRect(g, RectF(1 * s, 0, w - 2 * s, 4 * s), 2 * s, Rgba(colour));
      DrawLabel(g, Wide(t.compact), font, RectF(4 * s, 5 * s, w - 8 * s, 15 * s),
                Rgba(c.text), HAlign::Center, false);
      DrawLabel(g, Wide(t.type), small_font, RectF(4 * s, 19 * s, w - 8 * s, 13 * s),
                Rgba(colour), HAlign::Center, false);
      break;
    }
    case kHudStyleColumn: {
      const RectF box(0, 0, w, h);
      FillRoundRect(g, box, 6 * s, Rgba(c.surface));
      StrokeRoundRect(g, box, 6 * s, Rgba(c.border_strong), 1.0f);
      FillRoundRect(g, RectF(0, 0, 5 * s, h), 2.5f * s, Rgba(colour));
      if (t.names.empty()) {
        DrawLabel(g, Wide(t.compact), font, RectF(9 * s, 0, w - 12 * s, h), Rgba(c.text), HAlign::Left, false);
        break;
      }
      const size_t rows = t.names.size() - 1;
      for (size_t i = 0; i < rows; ++i) {
        const float y = 5 * s + (float)i * 14 * s;
        DrawLabel(g, Wide(t.names[i]), small_font, RectF(10 * s, y, 44 * s, 14 * s),
                  Rgba(c.text_dim), HAlign::Left, false);
        DrawLabel(g, Wide(t.values[i]), font, RectF(50 * s, y - 1 * s, w - 58 * s, 15 * s),
                  Rgba(c.text), HAlign::Right, false);
      }
      DrawLabel(g, Wide(ColumnFooter(t)), small_font,
                RectF(10 * s, 5 * s + (float)rows * 14 * s, w - 14 * s, 14 * s),
                Rgba(colour), HAlign::Left, false);
      break;
    }
    case kHudStyleBadge: {
      const float badge_h = 17 * s;
      float badge_w = MeasureLabel(g, Wide(t.badge), font) + 16 * s;
      if (!expanded || t.names.empty()) badge_w = w;
      FillRoundRect(g, RectF(0, 0, badge_w, badge_h), 5 * s, Rgba(colour));
      DrawLabel(g, Wide(t.badge), font, RectF(0, 0, badge_w, badge_h), Rgba(kDarkText), HAlign::Center, false);
      if (expanded && !t.names.empty()) {
        const RectF card(0, badge_h + 3 * s, w, h - badge_h - 3 * s);
        FillRoundRect(g, card, 7 * s, Rgba(c.surface));
        StrokeRoundRect(g, card, 7 * s, WithAlpha(colour, 0xC0), 1.2f);
        for (size_t i = 0; i < t.names.size(); ++i) {
          const float y = card.Y + 6 * s + (float)i * 14 * s;
          DrawLabel(g, Wide(t.names[i]), small_font, RectF(9 * s, y, 60 * s, 14 * s),
                    Rgba(c.text_dim), HAlign::Left, false);
          DrawLabel(g, Wide(t.values[i]), font, RectF(60 * s, y - 1 * s, w - 69 * s, 15 * s),
                    Rgba(c.text), HAlign::Right, false);
        }
      }
      break;
    }
    default: {
      const RectF box(0, 0, w, h);
      FillRoundRect(g, box, h / 2, Rgba(c.surface));
      StrokeRoundRect(g, box, h / 2, WithAlpha(colour, 0xB0), 1.2f);
      FillCircle(g, 9 * s, h / 2, 3.5f * s, Rgba(colour));
      DrawLabel(g, Wide(t.compact), font, RectF(17 * s, 0, w - 21 * s, h), Rgba(c.text), HAlign::Left, false);
      break;
    }
  }
}

// ---------------------------------------------------------------------------
// One label (a small window)
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CHudLabel, CWnd)
  ON_WM_CREATE()
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_DESTROY()
  ON_WM_MOUSEMOVE()
  ON_MESSAGE(WM_MOUSELEAVE, &CHudLabel::OnMouseLeave)
  ON_MESSAGE(modernui::WMU_THEME_CHANGED, &CHudLabel::OnThemeChanged)
END_MESSAGE_MAP()

CHudLabel::CHudLabel() : _style(kHudStylePill), _size(0, 0), _size_valid(false),
  _expanded(false), _tracking(false), _click_through(true) {
}

CHudLabel::~CHudLabel() {
  if (m_hWnd != NULL) DestroyWindow();
}

bool CHudLabel::CreateLabel() {
  if (m_hWnd != NULL) return true;
  LPCTSTR class_name = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
    ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
  // Never activated, no taskbar button. Click-through, except for the badge style (mouse-over).
  const BOOL ok = CWnd::CreateEx(
    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
    class_name, "OpenHoldem HUD", WS_POPUP, CRect(0, 0, 10, 10), NULL, 0);
  _click_through = true;
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

void CHudLabel::SetClickThrough(bool click_through) {
  if (m_hWnd == NULL || click_through == _click_through) return;
  LONG_PTR ex = ::GetWindowLongPtr(m_hWnd, GWL_EXSTYLE);
  if (click_through) ex |= WS_EX_TRANSPARENT; else ex &= ~(LONG_PTR)WS_EX_TRANSPARENT;
  ::SetWindowLongPtr(m_hWnd, GWL_EXSTYLE, ex);
  _click_through = click_through;
}

void CHudLabel::SetContent(int style, const HudText& text) {
  style = ClampStyle(style);
  const bool same = (style == _style && text.compact == _text.compact && text.badge == _text.badge
    && text.type == _text.type && text.colour == _text.colour && text.values == _text.values
    && text.names == _text.names);
  if (same) return;
  _style = style;
  _text = text;
  _size_valid = false;
  if (m_hWnd) Invalidate(FALSE);
}

CSize CHudLabel::LabelSize() {
  if (!_size_valid && m_hWnd != NULL) {
    CClientDC dc(this);
    Graphics g(dc.GetSafeHdc());
    SetupQuality(g);
    float w = 0, h = 0;
    HudLabelSize(g, _style, _text, DpiScale(m_hWnd), false, &w, &h);
    _size = CSize((int)(w + 0.5f), (int)(h + 0.5f));
    _size_valid = true;
  }
  return _size;
}

void CHudLabel::ApplyRoundedRegion(int width, int height) {
  const float s = DpiScale(m_hWnd);
  int radius = height;                                   // pill: diameter = height
  if (_style == kHudStyleTab) radius = (int)(14 * s);
  else if (_style == kHudStyleColumn) radius = (int)(12 * s);
  else if (_style == kHudStyleBadge) radius = (int)(10 * s);
  HRGN region = ::CreateRoundRectRgn(0, 0, width + 1, height + 1, radius, radius);
  SetWindowRgn(region, TRUE);   // the window owns the region afterwards
}

// Badge style: the badge grows into a card with all the stats while the mouse is over it
void CHudLabel::Resize(bool expanded) {
  CClientDC dc(this);
  Graphics g(dc.GetSafeHdc());
  SetupQuality(g);
  float w = 0, h = 0;
  HudLabelSize(g, _style, _text, DpiScale(m_hWnd), expanded, &w, &h);
  CRect current;
  GetWindowRect(&current);
  const int width = (int)(w + 0.5f), height = (int)(h + 0.5f);
  SetWindowPos(NULL, current.left, current.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
  ApplyRoundedRegion(width, height);
  _expanded = expanded;
  Invalidate(FALSE);
}

void CHudLabel::OnMouseMove(UINT nFlags, CPoint point) {
  if (_style == kHudStyleBadge && !_expanded && !_text.names.empty()) {
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
    ::TrackMouseEvent(&tme);
    _tracking = true;
    Resize(true);
  }
  CWnd::OnMouseMove(nFlags, point);
}

LRESULT CHudLabel::OnMouseLeave(WPARAM, LPARAM) {
  _tracking = false;
  if (_expanded) Resize(false);
  return 0;
}

void CHudLabel::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  SetupQuality(g);
  DrawHudLabel(g, _style, _text, DpiScale(m_hWnd), _expanded, (float)paint.Width(), (float)paint.Height());
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
  _obstacles.clear();
}

// Heartbeat thread
void CAdvisorHud::OnHeartbeat() {
  HudChairData fresh[kHudChairs];
  std::vector<HudRect> obstacles;
  const bool want = Preferences()->advisor_enabled() && Preferences()->hud_enabled()
    && p_autoconnector != NULL && p_autoconnector->IsConnectedToAnything()
    && p_table_state != NULL && p_betround_calculator != NULL;
  CStatsDatabase* database = want ? StatsDatabase() : NULL;
  if (database != NULL) {
    int betround = p_betround_calculator->betround();
    if (betround < kBetroundPreflop || betround > kBetroundRiver) betround = kBetroundPreflop;
    const std::vector<std::string> stats = ParseHudStatList(HudStatsOfBetround(betround));
    const bool show_names = Preferences()->hud_show_names();
    CSymbolEngineUserchair* userchair_engine = p_engine_container->symbol_engine_userchair();
    const int userchair = (userchair_engine != NULL && userchair_engine->userchair_confirmed())
      ? userchair_engine->userchair() : -1;
    CollectObstacles(&obstacles);
    for (int chair = 0; chair < kHudChairs; ++chair) {
      CRect seat;
      const bool has_seat = FindSeat(chair, &seat);
      CPlayer* player = p_table_state->Player(chair);
      const bool labelled = has_seat && chair != userchair && player != NULL && !player->name().IsEmpty();
      if (!labelled) {
        // No label, but nobody else may cover this seat
        if (has_seat) obstacles.push_back(ToHudRect(seat));
        continue;
      }
      HudChairData& d = fresh[chair];
      d.seat = seat;
      const double hands = database->GetStatByName(chair, "hands");
      if (hands < 0) {
        d.text = BuildHudText(stats, std::vector<double>(), -1, -1, -1, show_names);
      } else {
        std::vector<double> values;
        for (size_t i = 0; i < stats.size(); ++i) {
          values.push_back(database->GetStatByName(chair, stats[i]));
        }
        d.text = BuildHudText(stats, values, hands, database->GetStatByName(chair, "vpip"),
                              database->GetStatByName(chair, "pfr"), show_names);
      }
      d.visible = !d.text.compact.empty();
    }
  }
  CSLock lock(_critsec);
  for (int i = 0; i < kHudChairs; ++i) _data[i] = fresh[i];
  _obstacles = obstacles;
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
  std::vector<HudRect> obstacles;
  {
    CSLock lock(_critsec);
    for (int i = 0; i < kHudChairs; ++i) data[i] = _data[i];
    obstacles = _obstacles;
  }
  const int style = ClampStyle(Preferences()->hud_style());
  // Labels: content and size
  std::vector<HudPlaceItem> items(kHudChairs);
  for (int chair = 0; chair < kHudChairs; ++chair) {
    CHudLabel& label = _labels[chair];
    const HudChairData& d = data[chair];
    if (!d.visible || d.seat.IsRectEmpty()) continue;
    if (label.m_hWnd == NULL && !label.CreateLabel()) continue;
    if (new_table || ::GetWindow(label.m_hWnd, GW_OWNER) != table) {
      ::SetWindowLongPtr(label.m_hWnd, GWLP_HWNDPARENT, (LONG_PTR)table);
    }
    label.SetContent(style, d.text);
    label.SetClickThrough(style != kHudStyleBadge);
    const CSize size = label.LabelSize();
    items[chair].seat = ToHudRect(d.seat);
    items[chair].width = size.cx;
    items[chair].height = size.cy;
    items[chair].style = style;
    items[chair].valid = true;
  }
  // Where labels may be: the table window; optionally a bit outside of it (on this monitor)
  const HudRect inner(0, 0, client.Width(), client.Height());
  HudRect outer = inner;
  if (Preferences()->hud_outside()) {
    const float s = DpiScale(table);
    outer = HudRect(-(int)(160 * s), -(int)(40 * s), client.Width() + (int)(160 * s), client.Height() + (int)(40 * s));
    MONITORINFO monitor = { sizeof(monitor) };
    if (::GetMonitorInfo(::MonitorFromWindow(table, MONITOR_DEFAULTTONEAREST), &monitor)) {
      outer.l = (std::max)(outer.l, (int)(monitor.rcMonitor.left - origin.x));
      outer.t = (std::max)(outer.t, (int)(monitor.rcMonitor.top - origin.y));
      outer.r = (std::min)(outer.r, (int)(monitor.rcMonitor.right - origin.x));
      outer.b = (std::min)(outer.b, (int)(monitor.rcMonitor.bottom - origin.y));
      // the table window itself is always allowed
      outer.l = (std::min)(outer.l, inner.l); outer.t = (std::min)(outer.t, inner.t);
      outer.r = (std::max)(outer.r, inner.r); outer.b = (std::max)(outer.b, inner.b);
    }
  }
  const std::vector<HudRect> placed = PlaceHudLabels(items, obstacles, inner, outer);
  for (int chair = 0; chair < kHudChairs; ++chair) {
    CHudLabel& label = _labels[chair];
    if (!items[chair].valid) {
      if (_shown[chair] && label.m_hWnd != NULL) label.ShowWindow(SW_HIDE);
      _shown[chair] = false;
      continue;
    }
    if (label.IsExpanded()) continue;      // mouse-over card of the badge style: leave it alone
    const CRect target(origin.x + placed[chair].l, origin.y + placed[chair].t,
                       origin.x + placed[chair].r, origin.y + placed[chair].b);
    if (!_shown[chair] || _last_rect[chair] != target) {
      const bool resized = (_last_rect[chair].Size() != target.Size());
      label.SetWindowPos(NULL, target.left, target.top, target.Width(), target.Height(),
                         SWP_NOZORDER | SWP_NOACTIVATE);
      if (resized || !_shown[chair]) {
        // Rounded corners; the shape depends on the style
        const float s = DpiScale(label.m_hWnd);
        int radius = target.Height();
        if (style == kHudStyleTab) radius = (int)(14 * s);
        else if (style == kHudStyleColumn) radius = (int)(12 * s);
        else if (style == kHudStyleBadge) radius = (int)(10 * s);
        HRGN region = ::CreateRoundRectRgn(0, 0, target.Width() + 1, target.Height() + 1, radius, radius);
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

// ---------------------------------------------------------------------------
// Example table for the preferences (English): the selected style with sample players
// ---------------------------------------------------------------------------

namespace {

const float kPreviewWidth = 640.0f, kPreviewHeight = 300.0f;   // logical units

struct PreviewSeat {
  const char* name;
  const char* balance;
  float x, y;          // top left of the seat, logical units
  bool  hero;
};

}  // namespace

BEGIN_MESSAGE_MAP(CHudPreviewWnd, CWnd)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_TIMER()
  ON_WM_DESTROY()
END_MESSAGE_MAP()

CHudPreviewWnd::CHudPreviewWnd() : _last_style(-1) {
  modernui::GdiplusAcquire();
}

CHudPreviewWnd::~CHudPreviewWnd() {
  if (m_hWnd != NULL) DestroyWindow();
  modernui::GdiplusRelease();
}

bool CHudPreviewWnd::CreatePreview(CWnd* parent, const CRect& rect, std::function<int()> style_getter) {
  _style_getter = style_getter;
  LPCTSTR class_name = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
  if (!CWnd::Create(class_name, "", WS_CHILD | WS_VISIBLE, rect, parent, 0)) return false;
  SetTimer(kPreviewTimerId, 150, NULL);
  return true;
}

void CHudPreviewWnd::OnDestroy() {
  KillTimer(kPreviewTimerId);
  CWnd::OnDestroy();
}

void CHudPreviewWnd::OnTimer(UINT_PTR id) {
  if (id == kPreviewTimerId) {
    const int style = _style_getter ? _style_getter() : 0;
    if (style != _last_style) Invalidate(FALSE);
    return;
  }
  CWnd::OnTimer(id);
}

int CHudPreviewWnd::PreferredHeight(int width) {
  const float u = DpiScale(m_hWnd != NULL ? m_hWnd : ::GetDesktopWindow());
  const float factor = (std::min)(1.0f, (float)width / (kPreviewWidth * u));
  return (int)(kPreviewHeight * u * factor + 0.5f);
}

void CHudPreviewWnd::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  SetupQuality(g);
  const Palette& c = Theme::Colors();
  const float u = DpiScale(m_hWnd);
  const float width = (float)paint.Width(), height = (float)paint.Height();
  const float factor = (std::min)(1.0f, width / (kPreviewWidth * u));
  const float k = u * factor;                              // logical unit -> pixels
  const float ox = (std::max)(0.0f, (width - kPreviewWidth * k) / 2);
  auto X = [&](float v) { return ox + v * k; };
  auto Y = [&](float v) { return v * k; };
  const int style = ClampStyle(_style_getter ? _style_getter() : 0);
  _last_style = style;

  FillRoundRect(g, RectF(0, 0, width, height), 10 * u, Rgba(c.surface_hover));
  // Table (the "table window" of the example: labels may stick out of it)
  FillRoundRect(g, RectF(X(112), Y(30), 416 * k, 240 * k), 120 * k, Rgba(c.felt_rim));
  FillRoundRect(g, RectF(X(120), Y(38), 400 * k, 224 * k), 112 * k, Rgba(c.felt));

  const PreviewSeat seats[6] = {
    { "Quillan",   "1114", 280,  40, false },
    { "Katina",    "1070", 424,  92, false },
    { "Catherine", "127",  424, 186, false },
    { "Hero",      "1122", 280, 236, true },
    { "Thad",      "1560", 136, 186, false },
    { "Iliana",    "995",  136,  92, false },
  };
  struct Sample { double v[3]; double hands; } samples[6] = {
    { { 0.22, 0.17, 0.06 }, 412 }, { { 0.924, 0.71, 0.12 }, 119 }, { { 0.41, 0.09, 0.02 }, 7 },
    { { 0, 0, 0 }, -1 }, { { 0.11, 0.08, 0.04 }, 240 }, { { 0.33, 0.26, 0.09 }, 55 }
  };
  const float seat_w = 84, seat_h = 34;

  std::vector<HudRect> obstacles;
  std::vector<HudPlaceItem> items(6);
  std::vector<HudText> texts(6);
  std::vector<std::string> stats;
  stats.push_back("vpip"); stats.push_back("pfr"); stats.push_back("3bet");
  for (int i = 0; i < 6; ++i) {
    const PreviewSeat& seat = seats[i];
    const RectF box(X(seat.x), Y(seat.y), seat_w * k, seat_h * k);
    FillRoundRect(g, box, 7 * k, Rgba(c.seat_bg));
    StrokeRoundRect(g, box, 7 * k, Rgba(c.seat_border), 1.5f);
    DrawLabel(g, Wide(seat.name), modernui::GetFont(11.5f * k, true), RectF(box.X, box.Y + 2 * k, box.Width, 15 * k),
              Rgba(c.seat_name), HAlign::Center, true);
    DrawLabel(g, Wide(seat.balance), modernui::GetFont(11.0f * k, false), RectF(box.X, box.Y + 17 * k, box.Width, 15 * k),
              Rgba(c.seat_balance), HAlign::Center, true);
    // Two cards above the seat, a bet chip towards the middle
    for (int card = 0; card < 2; ++card) {
      FillRoundRect(g, RectF(X(seat.x + 26 + card * 16), Y(seat.y - 24), 14 * k, 21 * k), 2 * k, Rgba(c.card_back_top));
    }
    const float bet_x = seat.x + (seat.x < 300 ? 90.0f : -30.0f), bet_y = seat.y + 22;
    FillCircle(g, X(bet_x + 10), Y(bet_y + 10), 10 * k, Rgba(c.chip_bg));
    obstacles.push_back(HudRect((int)X(seat.x + 26), (int)Y(seat.y - 24), (int)X(seat.x + 26 + 30), (int)Y(seat.y - 2)));
    obstacles.push_back(HudRect((int)X(bet_x), (int)Y(bet_y), (int)X(bet_x + 20), (int)Y(bet_y + 20)));
    items[i].seat = HudRect((int)box.X, (int)box.Y, (int)(box.X + box.Width), (int)(box.Y + box.Height));
    if (seat.hero) obstacles.push_back(items[i].seat);   // nobody may cover the hero seat
    if (!seat.hero) {
      std::vector<double> values(samples[i].v, samples[i].v + 3);
      texts[i] = BuildHudText(stats, values, samples[i].hands, samples[i].v[0], samples[i].v[1], false);
      if (samples[i].hands < 0) texts[i] = BuildHudText(stats, std::vector<double>(), -1, -1, -1, false);
      float w = 0, h = 0;
      HudLabelSize(g, style, texts[i], k, false, &w, &h);
      items[i].width = (int)(w + 0.5f);
      items[i].height = (int)(h + 0.5f);
      items[i].style = style;
      items[i].valid = true;
    }
  }
  // The board / pot in the middle
  DrawLabel(g, L"Pot: 27", modernui::GetFont(12.0f * k, true), RectF(X(270), Y(132), 100 * k, 18 * k),
            Rgba(c.info_accent), HAlign::Center, false);
  obstacles.push_back(HudRect((int)X(240), (int)Y(120), (int)X(400), (int)Y(170)));

  // The table window of the example: the labels may stick out of it (like in the real HUD)
  const HudRect inner((int)X(100), (int)Y(14), (int)X(540), (int)Y(286));
  const HudRect outer(0, 0, (int)width, (int)height);
  const std::vector<HudRect> placed = PlaceHudLabels(items, obstacles, inner,
    Preferences()->hud_outside() ? outer : inner);
  for (int i = 0; i < 6; ++i) {
    if (!items[i].valid) continue;
    const bool expanded = (style == kHudStyleBadge && i == 1);   // mouse-over example
    float w = 0, h = 0;
    HudLabelSize(g, style, texts[i], k, expanded, &w, &h);
    GraphicsState state = g.Save();
    g.TranslateTransform((float)placed[i].l, (float)placed[i].t);
    DrawHudLabel(g, style, texts[i], k, expanded, w, h);
    g.Restore(state);
  }
  // Caption
  const char* captions[kHudNumberOfStyles] = {
    "Example: pill (preflop: VPIP/PFR/3-bet and number of hands)",
    "Example: tab under the seat (stats, player type)",
    "Example: column outside of the table (player type below)",
    "Example: badge; all stats of the betround when the mouse is over it (Katina)"
  };
  DrawLabel(g, Wide(captions[style]), modernui::GetFont(11.0f * u, false),
            RectF(10 * u, height - 20 * u, width - 20 * u, 16 * u), Rgba(c.text_dim), HAlign::Left, true);
}
