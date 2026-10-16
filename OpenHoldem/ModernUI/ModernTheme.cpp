//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Light / dark / system theme for the modern UI.
//
//******************************************************************************

#include "ModernTheme.h"
#include <algorithm>
#include <string>
#include <wchar.h>

namespace modernui {

std::wstring ToWide(const char* s) {
  if (s == nullptr || *s == '\0') return std::wstring();
  int n = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
  if (n <= 1) return std::wstring();
  std::wstring w((size_t)n - 1, L'\0');
  MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], n);
  return w;
}

namespace {

// The theme mode is stored in an ini-file, not in the registry
const wchar_t* kIniSection  = L"ModernUI";
const wchar_t* kIniKeyMode  = L"ThemeMode";
std::wstring g_ini_file;
const wchar_t* kRegKeyWin   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
const wchar_t* kRegValLight = L"AppsUseLightTheme";

ThemeMode g_mode = ThemeMode::System;
bool g_mode_loaded = false;
bool g_effective_dark = true;
std::vector<HWND> g_windows;

// Does Windows currently want dark apps? Defaults to dark if unknown,
// because dark is the signature look of this UI.
bool SystemPrefersDark() {
  DWORD value = 0, size = sizeof(value);
  LONG rc = RegGetValueW(HKEY_CURRENT_USER, kRegKeyWin, kRegValLight,
                         RRF_RT_REG_DWORD, nullptr, &value, &size);
  if (rc != ERROR_SUCCESS) return true;
  return value == 0;
}

void EnsureLoaded() {
  if (g_mode_loaded) return;
  g_mode_loaded = true;
  if (!g_ini_file.empty()) {
    const int value = (int)GetPrivateProfileIntW(kIniSection, kIniKeyMode, -1, g_ini_file.c_str());
    if (value >= 0 && value <= 2) {
      g_mode = (ThemeMode)value;
    }
  }
  g_effective_dark = (g_mode == ThemeMode::Dark) ||
                     (g_mode == ThemeMode::System && SystemPrefersDark());
}

void Save() {
  if (g_ini_file.empty()) return;
  wchar_t text[16];
  swprintf_s(text, L"%d", (int)g_mode);
  WritePrivateProfileStringW(kIniSection, kIniKeyMode, text, g_ini_file.c_str());
}

void Broadcast() {
  // Iterate over a copy: windows may unregister while handling the message.
  std::vector<HWND> copy = g_windows;
  for (HWND h : copy) {
    if (!IsWindow(h)) continue;
    Theme::ApplyFrameAttributes(h);
    SendMessageW(h, WMU_THEME_CHANGED, 0, 0);
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME);
  }
}

bool Recompute() {
  bool dark = (g_mode == ThemeMode::Dark) ||
              (g_mode == ThemeMode::System && SystemPrefersDark());
  bool changed = (dark != g_effective_dark);
  g_effective_dark = dark;
  return changed;
}

}  // namespace

const Palette& Theme::DarkPalette() {
  // Colours taken from the design mockup.
  static const Palette p = {
    true,
    /* window_bg     */ 0xFF18191C, /* surface */ 0xFF202226, /* surface_hover */ 0xFF2A2D33,
    /* surface_press */ 0xFF33363D, /* border  */ 0xFF2C2F35, /* border_strong */ 0xFF34373E,
    /* text          */ 0xFFE8EAEE, /* dim     */ 0xFF969CA8, /* disabled      */ 0xFF5B606A,
    /* accent        */ 0xFF6096FF, /* accent_text */ 0xFFFFFFFF, /* accent_soft */ 0xFF243458,
    /* success       */ 0xFF4AC878, /* success_soft */ 0xFF1C402C, /* danger */ 0xFFF0626B, /* warning */ 0xFFFACC15,
    /* menu_bg       */ 0xFF202226, /* menu_border */ 0xFF34373E, /* menu_hover */ 0xFF2F3238, /* menu_sep */ 0xFF2C2F35,
    /* felt          */ 0xFF133A2C, /* felt_line */ 0xFF3B5F51, /* felt_rim */ 0xFF2C2F35,
    /* info_bg       */ 0xFF0B2119, /* info_text */ 0xFFE8EAEE, /* info_accent */ 0xFFFACC15, /* dashed */ 0xFF2F5A49,
    /* seat_bg       */ 0xFF202226, /* seat_border */ 0xFF6096FF, /* seat_out_bg */ 0xFF27292F,
    /* seat_name     */ 0xFFE8EAEE, /* seat_balance */ 0xFF4AC878, /* seat_out_text */ 0xFF969CA8,
    /* chip_bg       */ 0xFF071610, /* chip_text */ 0xFFFACC15,
    /* card_face     */ 0xFFFFFFFF, /* spade */ 0xFF111318, /* club */ 0xFF16A34A, /* diamond */ 0xFF2563EB, /* heart */ 0xFFDC2626,
    /* back_top      */ 0xFF3A55CC, /* back_bottom */ 0xFF2A3FA6, /* back_edge */ 0xFF22348C, /* back_inner */ 0xFF6F86E0,
    /* dealer_bg     */ 0xFFFFFFFF, /* dealer_border */ 0xFFF1D048, /* dealer_text */ 0xFF111318,
    /* badge_off_bg  */ 0xFF2C2F35, /* badge_off_text */ 0xFF969CA8, /* badge_on_bg */ 0xFF6096FF, /* badge_on_text */ 0xFFFFFFFF,
    /* pill_blue_bg  */ 0xFF243458, /* pill_blue_text */ 0xFF6096FF, /* pill_green_bg */ 0xFF1C402C, /* pill_green_text */ 0xFF4AC878,
  };
  return p;
}

const Palette& Theme::LightPalette() {
  static const Palette p = {
    false,
    /* window_bg     */ 0xFFEEF0F4, /* surface */ 0xFFFFFFFF, /* surface_hover */ 0xFFE8EBF1,
    /* surface_press */ 0xFFDCE0E8, /* border  */ 0xFFDADEE5, /* border_strong */ 0xFFC5CAD3,
    /* text          */ 0xFF1B1E24, /* dim     */ 0xFF5F6773, /* disabled      */ 0xFFA3A9B3,
    /* accent        */ 0xFF2F6FED, /* accent_text */ 0xFFFFFFFF, /* accent_soft */ 0xFFDBE7FF,
    /* success       */ 0xFF1B9A52, /* success_soft */ 0xFFDCF5E6, /* danger */ 0xFFD92D3A, /* warning */ 0xFFB7791F,
    /* menu_bg       */ 0xFFFFFFFF, /* menu_border */ 0xFFD3D8E0, /* menu_hover */ 0xFFEAEEF5, /* menu_sep */ 0xFFE3E6EB,
    /* felt          */ 0xFF2E7D5B, /* felt_line */ 0xFF64A98B, /* felt_rim */ 0xFFC9CED6,
    /* info_bg       */ 0xFF1F5D44, /* info_text */ 0xFFFFFFFF, /* info_accent */ 0xFFFFD447, /* dashed */ 0xFF5AA384,
    /* seat_bg       */ 0xFFFFFFFF, /* seat_border */ 0xFF2F6FED, /* seat_out_bg */ 0xFFE4E7EC,
    /* seat_name     */ 0xFF1B1E24, /* seat_balance */ 0xFF1B9A52, /* seat_out_text */ 0xFF7B8390,
    /* chip_bg       */ 0xFF14281E, /* chip_text */ 0xFFFFD447,
    /* card_face     */ 0xFFFFFFFF, /* spade */ 0xFF111318, /* club */ 0xFF15803D, /* diamond */ 0xFF2563EB, /* heart */ 0xFFDC2626,
    /* back_top      */ 0xFF3A55CC, /* back_bottom */ 0xFF2A3FA6, /* back_edge */ 0xFF22348C, /* back_inner */ 0xFF6F86E0,
    /* dealer_bg     */ 0xFFFFFFFF, /* dealer_border */ 0xFFE0B72A, /* dealer_text */ 0xFF111318,
    /* badge_off_bg  */ 0xFFE1E5EB, /* badge_off_text */ 0xFF7B8390, /* badge_on_bg */ 0xFF2F6FED, /* badge_on_text */ 0xFFFFFFFF,
    /* pill_blue_bg  */ 0xFFDBE7FF, /* pill_blue_text */ 0xFF2F6FED, /* pill_green_bg */ 0xFFDCF5E6, /* pill_green_text */ 0xFF1B9A52,
  };
  return p;
}

ThemeMode Theme::Mode() {
  EnsureLoaded();
  return g_mode;
}

void Theme::SetSettingsFile(const wchar_t* ini_file_path) {
  g_ini_file = (ini_file_path != nullptr) ? ini_file_path : L"";
  g_mode_loaded = false;      // read the mode again from the new place
  g_mode = ThemeMode::System;
}

void Theme::SetMode(ThemeMode mode) {
  EnsureLoaded();
  g_mode = mode;
  Save();
  Recompute();
  Broadcast();
}

bool Theme::IsDark() {
  EnsureLoaded();
  return g_effective_dark;
}

const Palette& Theme::Colors() {
  return IsDark() ? DarkPalette() : LightPalette();
}

void Theme::OnSystemSettingChanged() {
  EnsureLoaded();
  if (g_mode != ThemeMode::System) return;
  if (Recompute()) Broadcast();
}

void Theme::RegisterWindow(HWND hwnd) {
  if (std::find(g_windows.begin(), g_windows.end(), hwnd) == g_windows.end()) {
    g_windows.push_back(hwnd);
  }
  ApplyFrameAttributes(hwnd);
}

void Theme::UnregisterWindow(HWND hwnd) {
  g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), hwnd), g_windows.end());
}

void Theme::ApplyFrameAttributes(HWND hwnd) {
  // dwmapi.dll is loaded dynamically: no extra import library required
  // and graceful degradation on systems without the attributes.
  typedef HRESULT (WINAPI *DwmSetWindowAttributeFn)(HWND, DWORD, LPCVOID, DWORD);
  static HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
  if (dwm == nullptr) return;
  static DwmSetWindowAttributeFn set_attr =
      (DwmSetWindowAttributeFn)GetProcAddress(dwm, "DwmSetWindowAttribute");
  if (set_attr == nullptr) return;
  const Palette& c = Colors();
  const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_ = 20;
  const DWORD DWMWA_WINDOW_CORNER_PREFERENCE_ = 33;
  const DWORD DWMWA_BORDER_COLOR_ = 34;
  const DWORD DWMWA_CAPTION_COLOR_ = 35;
  BOOL dark = c.dark ? TRUE : FALSE;
  set_attr(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_, &dark, sizeof(dark));
  DWORD round_corners = 2;  // DWMWCP_ROUND
  set_attr(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_, &round_corners, sizeof(round_corners));
  COLORREF border  = ToColorRef(c.border_strong);
  COLORREF caption = ToColorRef(c.surface);
  set_attr(hwnd, DWMWA_BORDER_COLOR_, &border, sizeof(border));
  set_attr(hwnd, DWMWA_CAPTION_COLOR_, &caption, sizeof(caption));
}

}  // namespace modernui
