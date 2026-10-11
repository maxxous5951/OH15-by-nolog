//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Capturing the pixels of the poker-table-window.
//   See CWindowCapture.h for details.
//
//******************************************************************************

#include "stdafx.h"
#include "CWindowCapture.h"

// Flags of PrintWindow(), not defined for old Windows-SDK-targets
#ifndef PW_CLIENTONLY
#define PW_CLIENTONLY 0x00000001
#endif
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

// PW_CLIENTONLY:       origin (0, 0) is the top-left corner of the client-area,
//                      exactly like GetDC(window), so all coordinates stay valid
// PW_RENDERFULLCONTENT: also captures DirectX/GPU-composed content
//                      (Windows 8.1 and newer, ignored by older versions)
static const UINT kPrintWindowFlags = PW_CLIENTONLY | PW_RENDERFULLCONTENT;

CWindowCapture window_capture;

CWindowCapture::CWindowCapture() {
  _snapshot_dc = NULL;
  _snapshot_bitmap = NULL;
  _stock_bitmap = NULL;
  _snapshot_width = 0;
  _snapshot_height = 0;
  _snapshot_valid = false;
  _last_print_window_failed = false;
}

CWindowCapture::~CWindowCapture() {
  DeleteSnapshot();
}

bool CWindowCapture::UsePrintWindow() {
  return (Preferences()->capture_method() == k_CaptureMethod_PrintWindow);
}

HDC CWindowCapture::AcquireSourceDC(HWND window) {
  if (_snapshot_valid && (_snapshot_dc != NULL) && UsePrintWindow()) {
    return _snapshot_dc;
  }
  return GetDC(window);
}

void CWindowCapture::ReleaseSourceDC(HWND window, HDC source_dc) {
  if ((source_dc != NULL) && (source_dc == _snapshot_dc)) {
    // Persistent memory-DC, not a DC of the window
    return;
  }
  ReleaseDC(window, source_dc);
}

bool CWindowCapture::UpdateSnapshot(HWND window) {
  if (!UsePrintWindow()) {
    // Original method: free the memory of the snapshot
    DeleteSnapshot();
    return false;
  }
  _snapshot_valid = false;
  if ((window == NULL) || !IsWindow(window)) {
    return false;
  }
  RECT client_rect = { 0 };
  GetClientRect(window, &client_rect);
  int width = client_rect.right - client_rect.left;
  int height = client_rect.bottom - client_rect.top;
  if ((width <= 0) || (height <= 0)) {
    // E.g. minimized window -> original method as fallback
    return false;
  }
  HDC hdc_screen = CreateDC("DISPLAY", NULL, NULL, NULL);
  if (_snapshot_dc == NULL) {
    _snapshot_dc = CreateCompatibleDC(hdc_screen);
  }
  if (_snapshot_dc == NULL) {
    DeleteDC(hdc_screen);
    return false;
  }
  if ((_snapshot_bitmap == NULL) || (width != _snapshot_width) || (height != _snapshot_height)) {
    // First snapshot or window resized
    HBITMAP new_bitmap = CreateCompatibleBitmap(hdc_screen, width, height);
    if (new_bitmap == NULL) {
      DeleteDC(hdc_screen);
      return false;
    }
    HBITMAP previous_bitmap = (HBITMAP)SelectObject(_snapshot_dc, new_bitmap);
    if (_snapshot_bitmap == NULL) {
      _stock_bitmap = previous_bitmap;
    }
    else {
      DeleteObject(previous_bitmap);
    }
    _snapshot_bitmap = new_bitmap;
    _snapshot_width = width;
    _snapshot_height = height;
  }
  DeleteDC(hdc_screen);
  BOOL success = PrintWindow(window, _snapshot_dc, kPrintWindowFlags);
  if (!success) {
    if (!_last_print_window_failed) {
      write_log(k_always_log_errors,
        "[CWindowCapture] PrintWindow failed, using original capture method as fallback\n");
    }
    _last_print_window_failed = true;
    return false;
  }
  if (_last_print_window_failed) {
    write_log(Preferences()->debug_scraper(),
      "[CWindowCapture] PrintWindow works again\n");
  }
  _last_print_window_failed = false;
  _snapshot_valid = true;
  write_log(Preferences()->debug_scraper(),
    "[CWindowCapture] PrintWindow snapshot %dx%d\n", width, height);
  return true;
}

void CWindowCapture::DeleteSnapshot() {
  _snapshot_valid = false;
  if (_snapshot_dc != NULL) {
    if (_stock_bitmap != NULL) {
      SelectObject(_snapshot_dc, _stock_bitmap);
    }
    DeleteDC(_snapshot_dc);
    _snapshot_dc = NULL;
  }
  if (_snapshot_bitmap != NULL) {
    DeleteObject(_snapshot_bitmap);
    _snapshot_bitmap = NULL;
  }
  _stock_bitmap = NULL;
  _snapshot_width = 0;
  _snapshot_height = 0;
}

bool CWindowCapture::PrintClientArea(HWND window, HDC destination_dc) {
  if ((window == NULL) || (destination_dc == NULL)) {
    return false;
  }
  return (PrintWindow(window, destination_dc, kPrintWindowFlags) != FALSE);
}
