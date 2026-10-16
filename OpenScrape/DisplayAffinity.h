//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Hides the windows of OpenScrape from screenshots / screen capture
//   (SetWindowDisplayAffinity), exactly like in OpenHoldem.
//     * Windows 10 (2004) and newer: window missing in the captured image
//     * Windows 7 .. older Windows 10: window shown as black rectangle
//   The windows stay normally visible on the monitor.
//
//******************************************************************************

#ifndef INC_OPENSCRAPE_DISPLAYAFFINITY_H
#define INC_OPENSCRAPE_DISPLAYAFFINITY_H

class CDisplayAffinity {
 public:
  // Once at startup (GUI thread): reads the setting, installs a hook for all
  // windows that get activated later (dialogs, message boxes) and applies
  // the setting to the existing windows.
  static void Install();
  static void Uninstall();
  // Changes the setting (and saves it in the registry), applies it immediately
  static void SetBlockScreenshots(bool block);
  static bool BlockScreenshots();
  static void ApplyToWindow(HWND window);
 private:
  static void Apply();
};

#endif  // INC_OPENSCRAPE_DISPLAYAFFINITY_H
