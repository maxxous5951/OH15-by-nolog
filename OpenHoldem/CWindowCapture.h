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
//
// Two methods are supported (Preferences -> Auto-connector -> Window capture):
//   * Original: BitBlt directly from the device-context of the window.
//     The window has to be visible and must not be covered by other windows.
//   * PrintWindow: the Windows-API PrintWindow renders the client-area
//     of the window into a bitmap. Works for covered windows and for
//     DirectX/GPU-rendered windows. One snapshot per heartbeat
//     (UpdateSnapshot), all scraping of that heartbeat reads from it.
//
// If PrintWindow fails (e.g. minimized window) we silently fall back
// to the original method for that heartbeat.
//
// Threading: the snapshot is updated and used by the heartbeat-thread only.
//
//******************************************************************************

#ifndef INC_CWINDOWCAPTURE_H
#define INC_CWINDOWCAPTURE_H

class CWindowCapture {
 public:
  CWindowCapture();
  ~CWindowCapture();
 public:
  // True if the user selected PrintWindow in the preferences
  bool UsePrintWindow();
  // Returns the device-context to read the client-area-pixels from:
  //   * the snapshot-DC (PrintWindow-mode, valid snapshot)
  //   * else a normal DC of the window (original method).
  // Has to be paired with ReleaseSourceDC().
  HDC AcquireSourceDC(HWND window);
  void ReleaseSourceDC(HWND window, HDC source_dc);
  // Takes a fresh snapshot in PrintWindow-mode,
  // frees the snapshot in original mode. Returns true if the snapshot is valid.
  bool UpdateSnapshot(HWND window);
  void DeleteSnapshot();
  // One-shot rendering of the client-area of any window
  // into a bitmap selected into destination_dc (of the right size).
  // Used by the tablepoint-checker for not yet connected windows.
  bool PrintClientArea(HWND window, HDC destination_dc);
 private:
  HDC     _snapshot_dc;
  HBITMAP _snapshot_bitmap;
  HBITMAP _stock_bitmap;      // the bitmap that was selected initially
  int     _snapshot_width;
  int     _snapshot_height;
  bool    _snapshot_valid;
  bool    _last_print_window_failed;  // to avoid flooding the log
};

extern CWindowCapture window_capture;

#endif // INC_CWINDOWCAPTURE_H
