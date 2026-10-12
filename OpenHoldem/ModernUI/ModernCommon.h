//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Common includes for the modern UI layer ("ModernUI").
//
//   The ModernUI classes are plain Win32 + GDI+ (no MFC), so that they
//   can be compiled and tested without the rest of OpenHoldem.
//   Only the thin glue (ModernFrameSupport.*) knows about MFC.
//
//******************************************************************************

#ifndef INC_MODERNUI_COMMON_H
#define INC_MODERNUI_COMMON_H

// The modern UI needs at least Windows 10 APIs (DWM, per-window DPI).
#if !defined(WINVER) || (WINVER < 0x0A00)
#undef  WINVER
#define WINVER 0x0A00
#endif
#if !defined(_WIN32_WINNT) || (_WIN32_WINNT < 0x0A00)
#undef  _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <windows.h>
#include <objidl.h>      // required by gdiplus.h
#include <gdiplus.h>
#include <string>
#include <vector>

namespace modernui {

// Private window messages
const UINT WMU_THEME_CHANGED = WM_APP + 0x0420;

// Converts an ANSI (system code page) string to UTF-16.
std::wstring ToWide(const char* s);
inline std::wstring ToWide(const std::string& s) { return ToWide(s.c_str()); }

}  // namespace modernui

#endif  // INC_MODERNUI_COMMON_H
