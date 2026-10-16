//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: see DisplayAffinity.h
//
//******************************************************************************

#include "stdafx.h"
#include "DisplayAffinity.h"
#include "registry.h"

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif
#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

// SetWindowDisplayAffinity exists since Windows 7 only -> load dynamically
typedef BOOL (WINAPI *SetWindowDisplayAffinityFunction)(HWND, DWORD);

static HHOOK s_hook = NULL;
static bool  s_block_screenshots = false;

static SetWindowDisplayAffinityFunction GetSetWindowDisplayAffinity() {
  static bool initialized = false;
  static SetWindowDisplayAffinityFunction function = NULL;
  if (!initialized) {
    HMODULE user32 = ::GetModuleHandle("user32.dll");
    if (user32 != NULL) {
      function = (SetWindowDisplayAffinityFunction)::GetProcAddress(user32, "SetWindowDisplayAffinity");
    }
    initialized = true;
  }
  return function;
}

// A window gets activated -> apply the current setting (covers all later created windows)
static LRESULT CALLBACK DisplayAffinityCbtProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code == HCBT_ACTIVATE) {
    CDisplayAffinity::ApplyToWindow((HWND)wParam);
  }
  return ::CallNextHookEx(s_hook, code, wParam, lParam);
}

static BOOL CALLBACK ApplyToWindowOfThisProcess(HWND window, LPARAM) {
  DWORD process_id = 0;
  ::GetWindowThreadProcessId(window, &process_id);
  if (process_id == ::GetCurrentProcessId()) {
    CDisplayAffinity::ApplyToWindow(window);
  }
  return TRUE;
}

void CDisplayAffinity::ApplyToWindow(HWND window) {
  SetWindowDisplayAffinityFunction set_affinity = GetSetWindowDisplayAffinity();
  if (set_affinity == NULL || !::IsWindow(window)) {
    return;
  }
  // Only top-level windows are supported
  if (::GetWindowLong(window, GWL_STYLE) & WS_CHILD) {
    return;
  }
  static DWORD working_mode = 0;   // 0 = unknown yet
  if (!s_block_screenshots) {
    set_affinity(window, WDA_NONE);
    return;
  }
  if (working_mode != 0) {
    set_affinity(window, working_mode);
    return;
  }
  if (set_affinity(window, WDA_EXCLUDEFROMCAPTURE)) {
    working_mode = WDA_EXCLUDEFROMCAPTURE;
  } else if (set_affinity(window, WDA_MONITOR)) {
    working_mode = WDA_MONITOR;
  }
}

void CDisplayAffinity::Apply() {
  ::EnumWindows(ApplyToWindowOfThisProcess, 0);
}

bool CDisplayAffinity::BlockScreenshots() {
  return s_block_screenshots;
}

void CDisplayAffinity::SetBlockScreenshots(bool block) {
  s_block_screenshots = block;
  Registry reg;
  reg.read_reg();
  reg.block_screenshots = block ? 1 : 0;
  reg.write_reg();
  Apply();
}

void CDisplayAffinity::Install() {
  Registry reg;
  reg.read_reg();
  s_block_screenshots = (reg.block_screenshots != 0);
  if (s_hook == NULL) {
    s_hook = ::SetWindowsHookEx(WH_CBT, DisplayAffinityCbtProc, NULL, ::GetCurrentThreadId());
  }
  Apply();
}

void CDisplayAffinity::Uninstall() {
  if (s_hook != NULL) {
    ::UnhookWindowsHookEx(s_hook);
    s_hook = NULL;
  }
}
