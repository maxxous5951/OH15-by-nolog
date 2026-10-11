//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Optionally hide the windows of OpenHoldem from screenshots and
//   screen capture (Preferences -> Auto-connector -> Screenshot protection)
//   by means of the Windows-API SetWindowDisplayAffinity.
//
//   * Windows 10 (2004) and newer: WDA_EXCLUDEFROMCAPTURE,
//     the window is completely missing in the captured image.
//   * Windows 7 .. Windows 10 (older): WDA_MONITOR,
//     the window is shown as a black rectangle in the captured image.
//   * Older systems: the API does not exist, nothing happens.
//
//   The window stays normally visible on the monitor.
//   The setting affects only windows of this process (OpenHoldem.exe).
//   It has no influence on the scraping of the poker tables.
//
//******************************************************************************

#ifndef INC_CDISPLAYAFFINITY_H
#define INC_CDISPLAYAFFINITY_H

class CDisplayAffinity {
 public:
  // Call once from the GUI-thread, after the preferences have been loaded.
  // Installs a hook so that every window that gets activated later
  // (dialogs, message-boxes, ...) gets the current setting too,
  // and applies the setting to all existing windows.
  static void Install();
  static void Uninstall();
  // Apply the current preference to all top-level windows of this process.
  // To be called whenever the preference changes.
  static void Apply();
  // Apply the current preference to a single window
  static void ApplyToWindow(HWND window);
};

#endif // INC_CDISPLAYAFFINITY_H
