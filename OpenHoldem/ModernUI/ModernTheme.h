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
//   * Palettes are plain ARGB values (0xAARRGGBB, directly usable as
//     Gdiplus::Color).
//   * Mode "System" follows the Windows setting "Choose your default
//     app mode" (HKCU\...\Themes\Personalize\AppsUseLightTheme) and
//     reacts live when the user changes it.
//   * The chosen mode is persisted in HKCU\Software\OpenHoldem\ModernUI.
//
//******************************************************************************

#ifndef INC_MODERNUI_THEME_H
#define INC_MODERNUI_THEME_H

#include "ModernCommon.h"

namespace modernui {

enum class ThemeMode { System = 0, Light = 1, Dark = 2 };

struct Palette {
  bool  dark;
  // General surfaces
  DWORD window_bg, surface, surface_hover, surface_pressed, border, border_strong;
  // Text
  DWORD text, text_dim, text_disabled;
  // Accent colours (blue = primary, green = success / "on")
  DWORD accent, accent_text, accent_soft;
  DWORD success, success_soft, danger, warning;
  // Menus
  DWORD menu_bg, menu_border, menu_hover, menu_sep;
  // Poker table
  DWORD felt, felt_line, felt_rim, info_bg, info_text, info_accent, dashed;
  DWORD seat_bg, seat_border, seat_out_bg, seat_name, seat_balance, seat_out_text;
  DWORD chip_bg, chip_text;
  DWORD card_face, card_spade, card_club, card_diamond, card_heart;
  DWORD card_back_top, card_back_bottom, card_back_edge, card_back_inner;
  DWORD dealer_bg, dealer_border, dealer_text;
  // Indicator badges (F C K R A / T I O L P)
  DWORD badge_off_bg, badge_off_text, badge_on_bg, badge_on_text;
  // Status pills
  DWORD pill_blue_bg, pill_blue_text, pill_green_bg, pill_green_text;
};

class Theme {
 public:
  // Persisted user choice
  static ThemeMode Mode();
  static void SetMode(ThemeMode mode);          // saves + notifies
  // Ini-file for the theme mode (section [ModernUI]). Call once at startup,
  // before the theme is used. Without it the mode is not persisted.
  static void SetSettingsFile(const wchar_t* ini_file_path);
  // Effective result (System is resolved to Light or Dark)
  static bool IsDark();
  static const Palette& Colors();
  // Call when Windows broadcasts WM_SETTINGCHANGE / WM_THEMECHANGED.
  static void OnSystemSettingChanged();
  // Windows that want to be notified (they get WMU_THEME_CHANGED and are repainted).
  static void RegisterWindow(HWND top_level_window);
  static void UnregisterWindow(HWND top_level_window);
  // Dark/light title bar, border and rounded corners via DWM (no-op on old Windows).
  static void ApplyFrameAttributes(HWND hwnd);

  static const Palette& DarkPalette();
  static const Palette& LightPalette();
};

// Helpers to turn the ARGB values into GDI+ colours.
inline Gdiplus::Color Rgba(DWORD argb) { return Gdiplus::Color(argb); }
inline Gdiplus::Color WithAlpha(DWORD argb, BYTE alpha) {
  return Gdiplus::Color(alpha, (BYTE)(argb >> 16), (BYTE)(argb >> 8), (BYTE)argb);
}
inline COLORREF ToColorRef(DWORD argb) {
  return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

}  // namespace modernui

#endif  // INC_MODERNUI_THEME_H
