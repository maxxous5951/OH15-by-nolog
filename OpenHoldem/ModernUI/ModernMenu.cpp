//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Themed replacement for native Win32 menus.
//
//******************************************************************************

#include "ModernMenu.h"
#include <cwctype>

namespace modernui {

using namespace Gdiplus;

// ===========================================================================
// Model
// ===========================================================================

void ParseMenuText(const std::wstring& raw, std::wstring* text, wchar_t* mnemonic, std::wstring* shortcut) {
  std::wstring label = raw;
  std::wstring accel;
  size_t tab = raw.find(L'\t');
  if (tab != std::wstring::npos) {
    label = raw.substr(0, tab);
    accel = raw.substr(tab + 1);
    // Some entries contain stray blanks ("\t Shift-F3").
    while (!accel.empty() && accel[0] == L' ') accel.erase(0, 1);
  }
  std::wstring clean;
  wchar_t mn = 0;
  for (size_t i = 0; i < label.size(); ++i) {
    if (label[i] == L'&') {
      if (i + 1 < label.size()) {
        if (label[i + 1] == L'&') { clean += L'&'; ++i; }
        else if (mn == 0) mn = (wchar_t)towupper(label[i + 1]);
      }
      continue;
    }
    clean += label[i];
  }
  if (text) *text = clean;
  if (mnemonic) *mnemonic = mn;
  if (shortcut) *shortcut = accel;
}

namespace {

void ReadItems(HMENU hmenu, std::vector<MenuItem>* items) {
  int count = GetMenuItemCount(hmenu);
  for (int i = 0; i < count; ++i) {
    MenuItem item;
    MENUITEMINFOW info;
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE;
    if (!GetMenuItemInfoW(hmenu, (UINT)i, TRUE, &info)) continue;
    if (info.fType & MFT_SEPARATOR) {
      item.separator = true;
      items->push_back(item);
      continue;
    }
    wchar_t buffer[256] = {0};
    GetMenuStringW(hmenu, (UINT)i, buffer, 255, MF_BYPOSITION);
    ParseMenuText(buffer, &item.text, &item.mnemonic, &item.shortcut);
    item.id = (int)info.wID;
    item.enabled = !(info.fState & (MFS_DISABLED | MFS_GRAYED));
    item.checked = (info.fState & MFS_CHECKED) != 0;
    items->push_back(item);
  }
}

}  // namespace

bool LoadMenuBarFromResource(HINSTANCE module, UINT resource_id, MenuBar* out) {
  HMENU hmenu = LoadMenuW(module, MAKEINTRESOURCEW(resource_id));
  if (hmenu == nullptr) return false;
  int count = GetMenuItemCount(hmenu);
  for (int i = 0; i < count; ++i) {
    wchar_t buffer[256] = {0};
    GetMenuStringW(hmenu, (UINT)i, buffer, 255, MF_BYPOSITION);
    Menu menu;
    ParseMenuText(buffer, &menu.text, &menu.mnemonic, nullptr);
    HMENU sub = GetSubMenu(hmenu, i);
    if (sub != nullptr) {
      ReadItems(sub, &menu.items);
    } else {
      menu.command = (int)GetMenuItemID(hmenu, i);
    }
    out->push_back(menu);
  }
  DestroyMenu(hmenu);
  return true;
}

bool LoadPopupFromResource(HINSTANCE module, UINT resource_id, Menu* out) {
  HMENU hmenu = LoadMenuW(module, MAKEINTRESOURCEW(resource_id));
  if (hmenu == nullptr) return false;
  HMENU sub = GetSubMenu(hmenu, 0);
  if (sub != nullptr) ReadItems(sub, &out->items);
  DestroyMenu(hmenu);
  return sub != nullptr;
}

// ===========================================================================
// Layout and painting
// ===========================================================================

MenuLayout LayoutMenu(Graphics& g, const Menu& menu, float s) {
  MenuLayout layout;
  const float pad_y = 6 * s, item_h = 30 * s, sep_h = 9 * s;
  const float text_x = 40 * s, right_pad = 18 * s, gap = 32 * s;
  const Font* font = GetFont(13.0f * s, false);
  float max_text = 0, max_shortcut = 0;
  for (size_t i = 0; i < menu.items.size(); ++i) {
    const MenuItem& it = menu.items[i];
    if (it.separator) continue;
    float t = MeasureLabel(g, it.text, font);
    float a = MeasureLabel(g, it.shortcut, font);
    if (t > max_text) max_text = t;
    if (a > max_shortcut) max_shortcut = a;
  }
  float width = text_x + max_text + (max_shortcut > 0 ? gap + max_shortcut : 0) + right_pad;
  if (width < 200 * s) width = 200 * s;
  float y = pad_y;
  for (size_t i = 0; i < menu.items.size(); ++i) {
    float h = menu.items[i].separator ? sep_h : item_h;
    layout.rects.push_back(RectF(0, y, width, h));
    y += h;
  }
  layout.width = width;
  layout.height = y + pad_y;
  return layout;
}

void PaintMenu(Graphics& g, const Menu& menu, const MenuLayout& layout, int hover, float s, const Palette& c) {
  SolidBrush bg(Rgba(c.menu_bg));
  g.FillRectangle(&bg, RectF(0, 0, layout.width, layout.height));
  const Font* font = GetFont(13.0f * s, false);
  for (size_t i = 0; i < menu.items.size(); ++i) {
    const MenuItem& it = menu.items[i];
    const RectF& r = layout.rects[i];
    if (it.separator) {
      Pen pen(Rgba(c.menu_sep), 1.0f);
      float y = r.Y + r.Height / 2;
      g.DrawLine(&pen, 12 * s, y, layout.width - 12 * s, y);
      continue;
    }
    if ((int)i == hover && it.enabled) {
      FillRoundRect(g, RectF(r.X + 5 * s, r.Y + 1 * s, r.Width - 10 * s, r.Height - 2 * s), 6 * s, Rgba(c.menu_hover));
    }
    DWORD text_color = it.enabled ? c.text : c.text_disabled;
    DWORD dim_color  = it.enabled ? c.text_dim : c.text_disabled;
    // Icon column
    RectF icon_rect(r.X + 13 * s, r.Y + (r.Height - 16 * s) / 2, 16 * s, 16 * s);
    if (it.checked) {
      if (it.radio) FillCircle(g, icon_rect.X + 8 * s, icon_rect.Y + 8 * s, 3.5f * s, Rgba(c.accent));
      else DrawIcon(g, Icon::Check, icon_rect, Rgba(c.accent), 2.2f);
    } else if (it.icon != Icon::None) {
      DrawIcon(g, it.icon, icon_rect, Rgba(dim_color), 2.0f);
    }
    DrawLabel(g, it.text, font, RectF(r.X + 40 * s, r.Y, r.Width - 40 * s - 18 * s, r.Height), Rgba(text_color));
    if (!it.shortcut.empty()) {
      DrawLabel(g, it.shortcut, font, RectF(r.X, r.Y, r.Width - 18 * s, r.Height), Rgba(dim_color), HAlign::Right, false);
    }
  }
  // 1 px border
  Pen border(Rgba(c.menu_border), 1.0f);
  g.DrawRectangle(&border, 0.5f, 0.5f, layout.width - 1, layout.height - 1);
}

// ===========================================================================
// Popup window + modal loop
// ===========================================================================

namespace {

const wchar_t* kPopupClass = L"OpenHoldemModernPopup";

class Popup;
Popup* g_active_popup = nullptr;

class Popup {
 public:
  Popup(HWND owner, Menu* menu, const BarContext* bar)
      : owner_(owner), menu_(menu), bar_(bar), hwnd_(nullptr), hover_(-1),
        done_(false), scale_(DpiScale(owner)) {}

  TrackResult Run(int x, int y, int anchor_h, bool select_first) {
    RegisterClassOnce();
    // Measure with a screen based Graphics.
    HDC screen = GetDC(nullptr);
    {
      Graphics g(screen);
      SetupQuality(g);
      layout_ = LayoutMenu(g, *menu_, scale_);
    }
    ReleaseDC(nullptr, screen);
    int w = (int)(layout_.width + 0.5f), h = (int)(layout_.height + 0.5f);

    // Keep the popup on the screen.
    POINT pt = { x, y };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi; mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    if (y + h > mi.rcWork.bottom) {
      int above = y - anchor_h - h;
      y = (above >= mi.rcWork.top) ? above : mi.rcWork.bottom - h;
    }
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;

    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kPopupClass, L"",
                            WS_POPUP, x, y, w, h, owner_, nullptr, GetModuleHandleW(nullptr), this);
    if (hwnd_ == nullptr) return result_;
    Theme::ApplyFrameAttributes(hwnd_);
    if (select_first) hover_ = NextEnabled(-1, +1);
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd_);
    SetCapture(hwnd_);

    Popup* previous = g_active_popup;
    g_active_popup = this;
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD, &Popup::KeyboardHook, nullptr, GetCurrentThreadId());

    MSG msg;
    while (!done_) {
      BOOL got = GetMessageW(&msg, nullptr, 0, 0);
      if (got == 0) { PostQuitMessage((int)msg.wParam); break; }   // re-post WM_QUIT
      if (got < 0) break;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      if (!done_ && GetCapture() != hwnd_) Finish(TrackResult::Dismissed, 0, -1);
      if (!done_ && !BelongsToThisProcess(GetForegroundWindow())) Finish(TrackResult::Dismissed, 0, -1);
    }

    if (hook) UnhookWindowsHookEx(hook);
    g_active_popup = previous;
    if (GetCapture() == hwnd_) ReleaseCapture();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    return result_;
  }

 private:
  static bool BelongsToThisProcess(HWND hwnd) {
    if (hwnd == nullptr) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
  }

  static void RegisterClassOnce() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = &Popup::WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.lpszClassName = kPopupClass;
    RegisterClassExW(&wc);
    registered = true;
  }

  void Finish(TrackResult::Kind kind, int command, int index) {
    if (done_) return;
    result_.kind = kind;
    result_.command = command;
    result_.index = index;
    done_ = true;
  }

  int ItemAt(int x, int y) const {
    for (size_t i = 0; i < layout_.rects.size(); ++i) {
      const RectF& r = layout_.rects[i];
      if (!menu_->items[i].separator && x >= r.X && x < r.X + r.Width && y >= r.Y && y < r.Y + r.Height) return (int)i;
    }
    return -1;
  }

  int NextEnabled(int from, int step) const {
    int n = (int)menu_->items.size();
    if (n == 0) return -1;
    int i = from;
    for (int k = 0; k < n; ++k) {
      i += step;
      if (i < 0) i = n - 1;
      if (i >= n) i = 0;
      if (!menu_->items[i].separator && menu_->items[i].enabled) return i;
    }
    return -1;
  }

  void Activate(int index) {
    if (index < 0 || index >= (int)menu_->items.size()) return;
    const MenuItem& it = menu_->items[index];
    if (it.separator || !it.enabled) return;
    Finish(TrackResult::Command, it.id, -1);
  }

  void SetHover(int index) {
    if (index == hover_) return;
    hover_ = index;
    InvalidateRect(hwnd_, nullptr, FALSE);
  }

  // Returns true if the key was consumed.
  bool OnKey(WPARAM vk) {
    switch (vk) {
      case VK_ESCAPE: Finish(TrackResult::Dismissed, 0, -1); return true;
      case VK_MENU:   Finish(TrackResult::Dismissed, 0, -1); return true;
      case VK_DOWN:   SetHover(NextEnabled(hover_, +1)); return true;
      case VK_UP:     SetHover(NextEnabled(hover_ < 0 ? 0 : hover_, -1)); return true;
      case VK_HOME:   SetHover(NextEnabled(-1, +1)); return true;
      case VK_END:    SetHover(NextEnabled((int)menu_->items.size(), -1)); return true;
      case VK_RETURN:
      case VK_SPACE:  Activate(hover_); return true;
      case VK_LEFT:   if (bar_) Finish(TrackResult::NavLeft, 0, -1); return true;
      case VK_RIGHT:  if (bar_) Finish(TrackResult::NavRight, 0, -1); return true;
      default: break;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
      for (size_t i = 0; i < menu_->items.size(); ++i) {
        const MenuItem& it = menu_->items[i];
        if (!it.separator && it.enabled && it.mnemonic == (wchar_t)vk) { Activate((int)i); return true; }
      }
      return true;   // swallow other letters while a menu is open
    }
    return false;
  }

  static LRESULT CALLBACK KeyboardHook(int code, WPARAM wparam, LPARAM lparam) {
    static WPARAM swallowed_vk = 0;
    if (code == HC_ACTION && g_active_popup != nullptr) {
      bool key_up = (lparam & 0x80000000) != 0;
      if (!key_up) {
        if (g_active_popup->OnKey(wparam)) { swallowed_vk = wparam; return 1; }
      } else if (swallowed_vk == wparam) {
        swallowed_vk = 0;
        return 1;
      }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
  }

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Popup* self = (Popup*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_NCCREATE) {
      CREATESTRUCTW* cs = (CREATESTRUCTW*)lparam;
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
      return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    if (self == nullptr) return DefWindowProcW(hwnd, msg, wparam, lparam);
    return self->HandleMessage(hwnd, msg, wparam, lparam);
  }

  LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
      case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
      case WM_ERASEBKGND:    return 1;
      case WM_PAINT: {
        BufferedPaint paint(hwnd);
        PaintMenu(paint.G(), *menu_, layout_, hover_, scale_, Theme::Colors());
        return 0;
      }
      case WM_MOUSEMOVE: {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        RECT rc; GetClientRect(hwnd, &rc);
        if (PtInRect(&rc, pt)) {
          SetHover(ItemAt(pt.x, pt.y));
        } else {
          SetHover(-1);
          if (bar_ && bar_->hit_test) {
            POINT screen = pt; ClientToScreen(hwnd, &screen);
            int idx = bar_->hit_test(screen);
            if (idx >= 0 && idx != bar_->current) Finish(TrackResult::SwitchTo, 0, idx);
          }
        }
        return 0;
      }
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN: {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        RECT rc; GetClientRect(hwnd, &rc);
        if (!PtInRect(&rc, pt)) {
          POINT screen = pt; ClientToScreen(hwnd, &screen);
          int idx = (bar_ && bar_->hit_test) ? bar_->hit_test(screen) : -1;
          if (idx >= 0 && idx != bar_->current) Finish(TrackResult::SwitchTo, 0, idx);
          else Finish(TrackResult::Dismissed, 0, -1);   // click on the same bar entry or elsewhere closes
        }
        return 0;
      }
      case WM_LBUTTONUP:
      case WM_RBUTTONUP: {
        POINT pt = { (short)LOWORD(lparam), (short)HIWORD(lparam) };
        RECT rc; GetClientRect(hwnd, &rc);
        if (PtInRect(&rc, pt)) Activate(ItemAt(pt.x, pt.y));
        return 0;
      }
      case WM_CAPTURECHANGED:
        if ((HWND)lparam != hwnd) Finish(TrackResult::Dismissed, 0, -1);
        return 0;
      case WM_CANCELMODE:
        Finish(TrackResult::Dismissed, 0, -1);
        return 0;
      default: break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
  }

  HWND owner_;
  Menu* menu_;
  const BarContext* bar_;
  HWND hwnd_;
  MenuLayout layout_;
  int hover_;
  bool done_;
  float scale_;
  TrackResult result_;
};

}  // namespace

TrackResult TrackPopup(HWND owner, Menu& menu, int x, int y, int anchor_height,
                       const BarContext* bar, bool select_first) {
  Popup popup(owner, &menu, bar);
  return popup.Run(x, y, anchor_height, select_first);
}

int TrackContextMenu(HWND owner, Menu& menu, POINT screen) {
  TrackResult r = TrackPopup(owner, menu, screen.x, screen.y, 0, nullptr, false);
  return r.kind == TrackResult::Command ? r.command : 0;
}

}  // namespace modernui
