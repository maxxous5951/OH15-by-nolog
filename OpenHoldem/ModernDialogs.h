//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Themed (light / dark) replacements for classic Windows dialogs
//   * ModernMessageBox: like MessageBox(), but with the look of the new UI
//   * ShowAboutDialog:  "About this program"
//
//******************************************************************************

#ifndef INC_MODERNDIALOGS_H
#define INC_MODERNDIALOGS_H

namespace modernglue {

// Same flags and return values as ::MessageBox
// (MB_OK, MB_OKCANCEL, MB_YESNO, MB_YESNOCANCEL, MB_RETRYCANCEL,
//  MB_ABORTRETRYIGNORE, MB_ICON..., MB_DEFBUTTON..., MB_TOPMOST).
int ModernMessageBox(const char* message, const char* title, UINT flags, CWnd* parent = NULL);

void ShowAboutDialog(CWnd* parent = NULL);

}  // namespace modernglue

#endif  // INC_MODERNDIALOGS_H
