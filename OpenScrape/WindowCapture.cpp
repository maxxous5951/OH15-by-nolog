//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: see WindowCapture.h
//
//******************************************************************************

#include "stdafx.h"
#include "WindowCapture.h"
#include "registry.h"

// Not defined for old Windows-SDK-targets
#ifndef PW_CLIENTONLY
#define PW_CLIENTONLY 0x00000001
#endif
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

// PW_CLIENTONLY: origin (0, 0) = top-left of the client area, exactly like GetDC(window)
// PW_RENDERFULLCONTENT: also DirectX/GPU content (Windows 8.1+, ignored by older versions)
const UINT kPrintWindowFlags = PW_CLIENTONLY | PW_RENDERFULLCONTENT;

bool CopyWithPrintWindow(HWND window, HDC dest_dc, int width, int height, int src_x, int src_y) {
  RECT client = { 0 };
  ::GetClientRect(window, &client);
  const int client_width = client.right - client.left;
  const int client_height = client.bottom - client.top;
  if (client_width <= 0 || client_height <= 0) {
    return false;   // e.g. minimized
  }
  HDC screen_dc = ::CreateDC("DISPLAY", NULL, NULL, NULL);
  HDC memory_dc = ::CreateCompatibleDC(screen_dc);
  HBITMAP bitmap = ::CreateCompatibleBitmap(screen_dc, client_width, client_height);
  bool success = false;
  if (memory_dc != NULL && bitmap != NULL) {
    HBITMAP old_bitmap = (HBITMAP)::SelectObject(memory_dc, bitmap);
    if (::PrintWindow(window, memory_dc, kPrintWindowFlags)) {
      ::BitBlt(dest_dc, 0, 0, width, height, memory_dc, src_x, src_y, SRCCOPY);
      success = true;
    }
    ::SelectObject(memory_dc, old_bitmap);
  }
  if (bitmap != NULL) ::DeleteObject(bitmap);
  if (memory_dc != NULL) ::DeleteDC(memory_dc);
  ::DeleteDC(screen_dc);
  return success;
}

}  // namespace

void CopyFromWindow(HWND window, HDC dest_dc, int width, int height, int src_x, int src_y) {
  Registry reg;
  reg.read_reg();
  if (reg.capture_method == kCaptureMethodPrintWindow) {
    if (CopyWithPrintWindow(window, dest_dc, width, height, src_x, src_y)) {
      return;
    }
    // Fallback: original method
  }
  HDC window_dc = ::GetDC(window);
  ::BitBlt(dest_dc, 0, 0, width, height, window_dc, src_x, src_y, SRCCOPY);
  ::ReleaseDC(window, window_dc);
}
