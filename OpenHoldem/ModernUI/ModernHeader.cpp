//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Title bar, menu bar, toolbar and flags of the main window.
//
//******************************************************************************

#include "ModernHeader.h"
#include <cstdio>

namespace modernui {

using namespace Gdiplus;

namespace {

const wchar_t* kHeaderClass = L"OpenHoldemModernHeader";
const wchar_t* kStatusClass = L"OpenHoldemModernStatusbar";

// Measuring needs a Graphics object, even before a window exists.
struct ScreenGraphics {
  HDC dc;
  Graphics* g;
  ScreenGraphics() : dc(GetDC(nullptr)), g(new Graphics(dc)) { SetupQuality(*g); }
  ~ScreenGraphics() { delete g; ReleaseDC(nullptr, dc); }
};

}  // namespace

ModernHeader::ModernHeader()
    : hwnd_(nullptr), parent_(nullptr), scale_(1.0f), width_(0), relayout_(true),
      connected_(false), app_icon_(nullptr), title_h_(0), menu_h_(0), tool_h_(0), flags_h_(0),
      hot_(-1), pressed_(-1), open_menu_(-1), tracking_leave_(false) {
  GdiplusAcquire();
  Rebuild();
}

ModernHeader::~ModernHeader() {
  delete app_icon_;
  GdiplusRelease();
}

bool ModernHeader::Create(HWND parent, int control_id) {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &ModernHeader::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.lpszClassName = kHeaderClass;
    RegisterClassExW(&wc);
    registered = true;
  }
  parent_ = parent;
  hwnd_ = CreateWindowExW(0, kHeaderClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                          0, 0, 100, 100, parent, (HMENU)(INT_PTR)control_id,
                          GetModuleHandleW(nullptr), this);
  return hwnd_ != nullptr;
}

void ModernHeader::SetAppIcon(HICON icon) {
  delete app_icon_;
  app_icon_ = icon ? Bitmap::FromHICON(icon) : nullptr;
  Invalidate();
}

void ModernHeader::SetToolbarButtons(const std::vector<ToolButtonDef>& defs) {
  tool_defs_ = defs;
  Rebuild();
}

void ModernHeader::SetFlagCommandIds(const int* ids, int count) {
  flag_ids_.assign(ids, ids + count);
  Rebuild();
}

void ModernHeader::Rebuild() {
  std::vector<Element> old = elements_;
  elements_.clear();
  auto make = [](Kind kind, int id) {
    Element e;
    e.kind = kind; e.id = id; e.rect = RectF(0, 0, 0, 0);
    e.enabled = true; e.checked = false; e.success_tint = false; e.checkable = false;
    e.group_start = false; e.icon = Icon::None;
    return e;
  };
  for (size_t i = 0; i < menus_.size(); ++i) {
    Element e = make(kMenu, (int)i);
    e.label = menus_[i].text;
    elements_.push_back(e);
  }
  for (size_t i = 0; i < tool_defs_.size(); ++i) {
    const ToolButtonDef& d = tool_defs_[i];
    Element e = make(kTool, d.id);
    e.icon = d.icon; e.label = d.label; e.checkable = d.checkable;
    e.success_tint = d.success_tint; e.group_start = d.group_start;
    elements_.push_back(e);
  }
  for (size_t i = 0; i < flag_ids_.size(); ++i) {
    Element e = make(kFlag, flag_ids_[i]);
    wchar_t buf[8];
    swprintf(buf, 8, L"%d", (int)i);
    e.label = buf; e.checkable = true;
    elements_.push_back(e);
  }
  elements_.push_back(make(kTheme, 0));
  elements_.push_back(make(kMinimize, 0));
  elements_.push_back(make(kClose, 0));
  // Keep the state of buttons that already existed.
  for (size_t i = 0; i < elements_.size(); ++i) {
    for (size_t k = 0; k < old.size(); ++k) {
      if (old[k].kind == elements_[i].kind && old[k].id == elements_[i].id) {
        elements_[i].enabled = old[k].enabled;
        elements_[i].checked = old[k].checked;
      }
    }
  }
  hot_ = pressed_ = open_menu_ = -1;
  relayout_ = true;
  Invalidate();
}

ModernHeader::Element* ModernHeader::FindElement(Kind kind, int id) {
  for (size_t i = 0; i < elements_.size(); ++i)
    if (elements_[i].kind == kind && elements_[i].id == id) return &elements_[i];
  return nullptr;
}

const ModernHeader::Element* ModernHeader::FindElement(Kind kind, int id) const {
  for (size_t i = 0; i < elements_.size(); ++i)
    if (elements_[i].kind == kind && elements_[i].id == id) return &elements_[i];
  return nullptr;
}

void ModernHeader::SetTitle(const std::wstring& title) {
  if (title == title_) return;
  title_ = title;
  Invalidate();
}

void ModernHeader::SetConnected(bool connected) {
  if (connected == connected_) return;
  connected_ = connected;
  relayout_ = true;
  Invalidate();
}

void ModernHeader::EnableButton(int id, bool enabled) {
  Element* e = FindElement(kTool, id);
  if (!e) e = FindElement(kFlag, id);
  if (e && e->enabled != enabled) { e->enabled = enabled; Invalidate(); }
}

void ModernHeader::CheckButton(int id, bool checked) {
  Element* e = FindElement(kTool, id);
  if (!e) e = FindElement(kFlag, id);
  if (e && e->checked != checked) { e->checked = checked; Invalidate(); }
}

bool ModernHeader::IsButtonEnabled(int id) const {
  const Element* e = FindElement(kTool, id);
  if (!e) e = FindElement(kFlag, id);
  return e ? e->enabled : false;
}

bool ModernHeader::IsButtonChecked(int id) const {
  const Element* e = FindElement(kTool, id);
  if (!e) e = FindElement(kFlag, id);
  return e ? e->checked : false;
}

int ModernHeader::Height(float s) const {
  return (int)((38 + 38 + 62 + 38) * s + 0.5f);
}

int ModernHeader::TitleBarHeight(float s) const {
  return (int)(38 * s + 0.5f);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

void ModernHeader::Layout(Graphics& g, int width, float s) {
  scale_ = s;
  width_ = width;
  title_h_ = 38 * s; menu_h_ = 38 * s; tool_h_ = 62 * s; flags_h_ = 38 * s;
  const float y1 = title_h_, y2 = y1 + menu_h_, y3 = y2 + tool_h_;
  separators_.clear();

  // --- title bar, from the right ---
  float x = (float)width;
  Element* close = FindElement(kClose, 0);
  Element* minimize = FindElement(kMinimize, 0);
  Element* theme = FindElement(kTheme, 0);
  if (close)    close->rect = RectF(x - 46 * s, 0, 46 * s, title_h_);
  x -= 46 * s;
  if (minimize) minimize->rect = RectF(x - 46 * s, 0, 46 * s, title_h_);
  x -= 46 * s + 12 * s;
  std::wstring pill = connected_ ? L"Connected" : L"Not connected";
  float pill_w = MeasureLabel(g, pill, GetFont(12.0f * s, false)) + 34 * s;
  pill_rect_ = RectF(x - pill_w, (title_h_ - 24 * s) / 2, pill_w, 24 * s);
  x -= pill_w + 8 * s;
  if (theme) theme->rect = RectF(x - 30 * s, (title_h_ - 30 * s) / 2, 30 * s, 30 * s);
  x -= 30 * s + 10 * s;
  title_text_rect_ = RectF(38 * s, 0, (x - 38 * s) > 0 ? (x - 38 * s) : 0, title_h_);

  // --- menu bar ---
  float mx = 10 * s;
  const Font* menu_font = GetFont(13.0f * s, false);
  for (size_t i = 0; i < elements_.size(); ++i) {
    Element& e = elements_[i];
    if (e.kind != kMenu) continue;
    float w = MeasureLabel(g, e.label, menu_font) + 26 * s;
    e.rect = RectF(mx, y1 + 4 * s, w, 30 * s);
    mx += w + 2 * s;
  }

  // --- toolbar ---
  // Natural widths first; if the window is too narrow the buttons shrink and
  // their labels get an ellipsis ("Shoot fra...").
  const Font* tool_font = GetFont(11.5f * s, false);
  std::vector<Element*> tools;
  std::vector<float> widths;
  float natural_total = 0, fixed_total = 20 * s;      // left + right margin
  for (size_t i = 0; i < elements_.size(); ++i) {
    Element& e = elements_[i];
    if (e.kind != kTool) continue;
    float w = MeasureLabel(g, e.label, tool_font) + 22 * s;
    if (w < 60 * s) w = 60 * s;
    tools.push_back(&e);
    widths.push_back(w);
    natural_total += w;
    fixed_total += 2 * s;                               // gap after each button
    if (e.group_start && tools.size() > 1) fixed_total += 15 * s;   // separator
  }
  float room = (float)width - fixed_total;
  float factor = (natural_total > room && natural_total > 0) ? room / natural_total : 1.0f;
  if (factor < 0.72f) factor = 0.72f;
  float tx = 10 * s;
  for (size_t i = 0; i < tools.size(); ++i) {
    Element& e = *tools[i];
    if (e.group_start && i > 0) {
      tx += 6 * s;
      separators_.push_back(tx);
      tx += 9 * s;
    }
    float w = widths[i] * factor;
    e.rect = RectF(tx, y2 + 3 * s, w, 56 * s);
    tx += w + 2 * s;
  }

  // --- flags ---
  flags_label_rect_ = RectF(16 * s, y3, 46 * s, flags_h_);
  int n = 0;
  for (size_t i = 0; i < elements_.size(); ++i) {
    Element& e = elements_[i];
    if (e.kind != kFlag) continue;
    e.rect = RectF(64 * s + n * 28 * s, y3 + (flags_h_ - 24 * s) / 2, 24 * s, 24 * s);
    ++n;
  }
  relayout_ = false;
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void ModernHeader::PaintTitleBar(Graphics& g, const Palette& c) {
  const float s = scale_;
  if (app_icon_) {
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.DrawImage(app_icon_, RectF(14 * s, (title_h_ - 16 * s) / 2, 16 * s, 16 * s));
  }
  DrawLabel(g, title_, GetFont(13.0f * s, true), title_text_rect_, Rgba(c.text), HAlign::Left, true);
  // Connection pill
  FillRoundRect(g, pill_rect_, pill_rect_.Height / 2, Rgba(connected_ ? c.success_soft : c.surface_hover));
  DWORD pill_fg = connected_ ? c.success : c.text_dim;
  FillCircle(g, pill_rect_.X + 12 * s, pill_rect_.Y + pill_rect_.Height / 2, 3 * s, Rgba(pill_fg));
  DrawLabel(g, connected_ ? L"Connected" : L"Not connected", GetFont(12.0f * s, false),
            RectF(pill_rect_.X + 22 * s, pill_rect_.Y, pill_rect_.Width - 26 * s, pill_rect_.Height),
            Rgba(pill_fg), HAlign::Left, false);
}

void ModernHeader::PaintElement(Graphics& g, const Palette& c, size_t index) {
  const float s = scale_;
  const Element& e = elements_[index];
  const bool hot = ((int)index == hot_) && e.enabled;
  const bool down = ((int)index == pressed_) && hot;
  RectF r = e.rect;
  switch (e.kind) {
    case kMenu: {
      bool open = (open_menu_ == e.id);
      if (open || hot) FillRoundRect(g, r, 7 * s, Rgba(open || down ? c.surface_pressed : c.surface_hover));
      DrawLabel(g, e.label, GetFont(13.0f * s, false), r, Rgba(c.text), HAlign::Center, false);
      break;
    }
    case kTool: {
      DWORD fg = e.enabled ? c.text : c.text_disabled;
      if (e.checked) {
        DWORD tint = e.success_tint ? c.success : c.accent;
        FillRoundRect(g, r, 10 * s, Rgba(e.success_tint ? c.success_soft : c.accent_soft));
        StrokeRoundRect(g, r, 10 * s, Rgba(tint), 1.6f * s);
        fg = tint;
      } else if (hot) {
        FillRoundRect(g, r, 10 * s, Rgba(down ? c.surface_pressed : c.surface_hover));
      }
      float icon = 20 * s;
      DrawIcon(g, e.icon, RectF(r.X + (r.Width - icon) / 2, r.Y + 9 * s, icon, icon), Rgba(fg), 1.9f);
      DrawLabel(g, e.label, GetFont(11.5f * s, false), RectF(r.X, r.Y + 31 * s, r.Width, 18 * s),
                Rgba(fg), HAlign::Center, true);
      break;
    }
    case kFlag: {
      if (e.checked) {
        FillRoundRect(g, r, 8 * s, Rgba(c.accent));
      } else {
        FillRoundRect(g, r, 8 * s, Rgba(hot ? c.surface_hover : c.surface));
        StrokeRoundRect(g, r, 8 * s, Rgba(c.border_strong), 1.0f * s);
      }
      DrawLabel(g, e.label, GetFont(11.5f * s, true), r,
                Rgba(e.checked ? c.accent_text : c.text_dim), HAlign::Center, false);
      break;
    }
    case kTheme: {
      if (hot) FillRoundRect(g, r, 8 * s, Rgba(down ? c.surface_pressed : c.surface_hover));
      float icon = 17 * s;
      DrawIcon(g, c.dark ? Icon::Moon : Icon::Sun,
               RectF(r.X + (r.Width - icon) / 2, r.Y + (r.Height - icon) / 2, icon, icon), Rgba(c.text), 2.0f);
      break;
    }
    case kMinimize:
    case kClose: {
      bool is_close = (e.kind == kClose);
      if (hot) {
        SolidBrush brush(Rgba(is_close ? 0xFFE5333D : c.surface_hover));
        g.FillRectangle(&brush, r);
      }
      float icon = 14 * s;
      DWORD fg = (hot && is_close) ? 0xFFFFFFFF : c.text;
      DrawIcon(g, is_close ? Icon::Close : Icon::Minimize,
               RectF(r.X + (r.Width - icon) / 2, r.Y + (r.Height - icon) / 2, icon, icon), Rgba(fg), 1.8f);
      break;
    }
  }
}

void ModernHeader::Paint(Graphics& g, int width, float s) {
  if (relayout_ || width != width_ || s != scale_) Layout(g, width, s);
  const Palette& c = Theme::Colors();
  SolidBrush bg(Rgba(c.surface));
  g.FillRectangle(&bg, RectF(0, 0, (float)width, Height(s) * 1.0f));
  PaintTitleBar(g, c);
  for (size_t i = 0; i < elements_.size(); ++i) PaintElement(g, c, i);

  // Separators between toolbar groups
  const float y2 = title_h_ + menu_h_;
  Pen sep(Rgba(c.border), 1.0f);
  for (size_t i = 0; i < separators_.size(); ++i) {
    g.DrawLine(&sep, separators_[i], y2 + 12 * s, separators_[i], y2 + tool_h_ - 12 * s);
  }
  // Flags row
  const float y3 = y2 + tool_h_;
  g.DrawLine(&sep, 0.0f, y3, (float)width, y3);
  DrawLabel(g, L"Flags", GetFont(12.0f * s, false), flags_label_rect_, Rgba(c.text_dim), HAlign::Left, false);
  // Bottom border
  float bottom = y3 + flags_h_ - 0.5f;
  g.DrawLine(&sep, 0.0f, bottom, (float)width, bottom);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

int ModernHeader::HitTest(int x, int y) const {
  for (size_t i = elements_.size(); i-- > 0;) {
    const RectF& r = elements_[i].rect;
    if (r.Width > 0 && x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height) return (int)i;
  }
  return -1;
}

bool ModernHeader::IsDragArea(int x, int y) const {
  return y >= 0 && y < title_h_ && HitTest(x, y) < 0;
}

void ModernHeader::SetHot(int index) {
  if (index == hot_) return;
  hot_ = index;
  Invalidate();
}

void ModernHeader::PostCommand(int id) {
  if (id != 0) PostMessageW(parent_, WM_COMMAND, MAKEWPARAM(id, 0), 0);
}

void ModernHeader::Activate(int i) {
  if (i < 0 || i >= (int)elements_.size()) return;
  Element& e = elements_[i];
  switch (e.kind) {
    case kTool:
    case kFlag:
      if (!e.enabled) return;
      if (e.checkable) e.checked = !e.checked;   // same as the old sticky toolbar buttons
      Invalidate();
      PostCommand(e.id);
      break;
    case kTheme:    ShowThemeMenu(); break;
    case kMinimize: PostMessageW(parent_, WM_SYSCOMMAND, SC_MINIMIZE, 0); break;
    case kClose:    PostMessageW(parent_, WM_SYSCOMMAND, SC_CLOSE, 0); break;
    default: break;
  }
}

void ModernHeader::OpenMenu(int menu_index, bool select_first) {
  if (menu_index < 0 || menu_index >= (int)menus_.size()) return;
  const int count = (int)menus_.size();
  BarContext bar;
  bar.hit_test = [this](POINT screen) -> int {
    POINT p = screen;
    ScreenToClient(hwnd_, &p);
    int i = HitTest(p.x, p.y);
    if (i < 0 || elements_[i].kind != kMenu) return -1;
    return menus_[elements_[i].id].command == 0 ? elements_[i].id : -1;
  };
  int idx = menu_index;
  for (;;) {
    Menu& menu = menus_[idx];
    if (menu.command != 0) { PostCommand(menu.command); break; }   // entry without popup
    open_menu_ = idx;
    InvalidateRect(hwnd_, nullptr, FALSE);
    UpdateWindow(hwnd_);
    const Element* e = FindElement(kMenu, idx);
    POINT p = { (LONG)e->rect.X, (LONG)(e->rect.Y + e->rect.Height) };
    ClientToScreen(hwnd_, &p);
    if (before_show_) before_show_(menu);
    if (icon_for_) {
      for (size_t k = 0; k < menu.items.size(); ++k)
        if (!menu.items[k].separator && menu.items[k].icon == Icon::None) menu.items[k].icon = icon_for_(menu.items[k].id);
    }
    bar.current = idx;
    TrackResult r = TrackPopup(hwnd_, menu, p.x, p.y + (int)(2 * scale_), (int)e->rect.Height, &bar, select_first);
    if (r.kind == TrackResult::Command) { PostCommand(r.command); break; }
    if (r.kind == TrackResult::NavLeft || r.kind == TrackResult::NavRight) {
      int step = (r.kind == TrackResult::NavLeft) ? count - 1 : 1;
      int next = idx;
      for (int k = 0; k < count; ++k) {
        next = (next + step) % count;
        if (menus_[next].command == 0) break;
      }
      idx = next;
      select_first = true;
      continue;
    }
    if (r.kind == TrackResult::SwitchTo) { idx = r.index; select_first = false; continue; }
    break;   // dismissed
  }
  open_menu_ = -1;
  Invalidate();
}

bool ModernHeader::OpenMenuByMnemonic(wchar_t ch) {
  ch = (wchar_t)towupper(ch);
  for (size_t i = 0; i < menus_.size(); ++i) {
    if (menus_[i].mnemonic == ch) { OpenMenu((int)i, true); return true; }
  }
  return false;
}

void ModernHeader::ShowThemeMenu() {
  Menu menu;
  ThemeMode mode = Theme::Mode();
  const wchar_t* names[3] = { L"Use system setting", L"Light", L"Dark" };
  Icon icons[3] = { Icon::Monitor, Icon::Sun, Icon::Moon };
  for (int i = 0; i < 3; ++i) {
    MenuItem it;
    it.id = 1 + i; it.text = names[i]; it.icon = icons[i];
    it.radio = true; it.checked = ((int)mode == i);
    menu.items.push_back(it);
  }
  const Element* e = FindElement(kTheme, 0);
  POINT p = { (LONG)(e->rect.X + e->rect.Width), (LONG)(e->rect.Y + e->rect.Height + 4 * scale_) };
  ClientToScreen(hwnd_, &p);
  p.x -= (LONG)(210 * scale_);          // right-align roughly under the button
  TrackResult r = TrackPopup(hwnd_, menu, p.x, p.y, (int)e->rect.Height, nullptr, false);
  if (r.kind == TrackResult::Command) Theme::SetMode((ThemeMode)(r.command - 1));
}

LRESULT CALLBACK ModernHeader::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  ModernHeader* self = (ModernHeader*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (msg == WM_NCCREATE) {
    CREATESTRUCTW* cs = (CREATESTRUCTW*)lparam;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
  return self->HandleMessage(hwnd, msg, wparam, lparam);
}

LRESULT ModernHeader::HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: {
      POINT p = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
      ScreenToClient(hwnd, &p);
      // The empty part of the title bar belongs to the frame (dragging).
      if (IsDragArea(p.x, p.y)) return HTTRANSPARENT;
      return HTCLIENT;
    }
    case WM_PAINT: {
      BufferedPaint paint(hwnd);
      Paint(paint.G(), paint.Width(), DpiScale(hwnd));
      return 0;
    }
    case WM_SIZE:
      relayout_ = true;
      Invalidate();
      return 0;
    case WMU_THEME_CHANGED:
      Invalidate();
      return 0;
    case WM_MOUSEMOVE: {
      SetHot(HitTest((short)LOWORD(lparam), (short)HIWORD(lparam)));
      if (!tracking_leave_) {
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        tracking_leave_ = true;
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      tracking_leave_ = false;
      SetHot(-1);
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: {
      int i = HitTest((short)LOWORD(lparam), (short)HIWORD(lparam));
      if (i < 0) return 0;
      if (elements_[i].kind == kMenu) { OpenMenu(elements_[i].id, false); return 0; }
      pressed_ = i;
      SetCapture(hwnd);
      Invalidate();
      return 0;
    }
    case WM_LBUTTONUP: {
      if (pressed_ >= 0) {
        int pressed = pressed_;
        pressed_ = -1;
        if (GetCapture() == hwnd) ReleaseCapture();
        int i = HitTest((short)LOWORD(lparam), (short)HIWORD(lparam));
        Invalidate();
        if (i == pressed) Activate(i);
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
      if (pressed_ >= 0) { pressed_ = -1; Invalidate(); }
      return 0;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
      return 0;   // never show a native context menu
    default: break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

// ===========================================================================
// Status bar
// ===========================================================================

int StatusbarHeight(float s) { return (int)(26 * s + 0.5f); }

ModernStatusbar::ModernStatusbar() : hwnd_(nullptr) { GdiplusAcquire(); }

bool ModernStatusbar::Create(HWND parent, int control_id) {
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &ModernStatusbar::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.lpszClassName = kStatusClass;
    RegisterClassExW(&wc);
    registered = true;
  }
  hwnd_ = CreateWindowExW(0, kStatusClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                          0, 0, 100, 26, parent, (HMENU)(INT_PTR)control_id,
                          GetModuleHandleW(nullptr), this);
  return hwnd_ != nullptr;
}

void ModernStatusbar::SetTexts(const std::wstring& action, const std::wstring& handrank, const std::wstring& prwin) {
  if (action == action_ && handrank == handrank_ && prwin == prwin_) return;
  action_ = action; handrank_ = handrank; prwin_ = prwin;
  if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

void ModernStatusbar::Paint(Graphics& g, int width, int height, float s, const Palette& c,
                            const std::wstring& action, const std::wstring& handrank, const std::wstring& prwin) {
  SolidBrush bg(Rgba(c.surface));
  g.FillRectangle(&bg, RectF(0, 0, (float)width, (float)height));
  Pen line(Rgba(c.border), 1.0f);
  g.DrawLine(&line, 0.0f, 0.5f, (float)width, 0.5f);
  DrawLabel(g, action, GetFont(12.5f * s, false), RectF(12 * s, 1, 210 * s, (float)height - 1), Rgba(c.text), HAlign::Left, true);
  float x = 232 * s;
  const float ph = 20 * s, py = 1 + ((float)height - 1 - ph) / 2;
  const Font* font = GetFont(12.0f * s, true);
  if (!handrank.empty()) {
    float w = MeasureLabel(g, handrank, font) + 30 * s;
    RectF r(x, py, w, ph);
    FillRoundRect(g, r, ph / 2, Rgba(c.pill_blue_bg));
    DrawLabel(g, handrank, font, r, Rgba(c.pill_blue_text), HAlign::Center, false);
    x += w + 8 * s;
  }
  if (!prwin.empty()) {
    float w = MeasureLabel(g, prwin, font) + 30 * s;
    RectF r(x, py, w, ph);
    FillRoundRect(g, r, ph / 2, Rgba(c.pill_green_bg));
    DrawLabel(g, prwin, font, r, Rgba(c.pill_green_text), HAlign::Center, false);
  }
}

LRESULT CALLBACK ModernStatusbar::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  ModernStatusbar* self = (ModernStatusbar*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
  if (msg == WM_NCCREATE) {
    CREATESTRUCTW* cs = (CREATESTRUCTW*)lparam;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
  if (!self) return DefWindowProcW(hwnd, msg, wparam, lparam);
  switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      BufferedPaint paint(hwnd);
      Paint(paint.G(), paint.Width(), paint.Height(), DpiScale(hwnd), Theme::Colors(),
            self->action_, self->handrank_, self->prwin_);
      return 0;
    }
    case WMU_THEME_CHANGED:
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    default: break;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace modernui
