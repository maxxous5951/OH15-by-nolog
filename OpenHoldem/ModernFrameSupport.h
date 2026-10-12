//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: The thin MFC glue between the main frame and the (MFC-free)
//   ModernUI classes: builds the themed menu bar from the existing menu
//   resource, keeps the classic ON_UPDATE_COMMAND_UI handlers working
//   and expands the list of recent files.
//
//******************************************************************************

#ifndef INC_MODERNFRAMESUPPORT_H
#define INC_MODERNFRAMESUPPORT_H

#include "ModernUI/ModernHeader.h"

namespace modernglue {

// Number of flags, same as kNumberOfFlags
const int kFlagButtons = 20;

// Child window IDs (outside of the ranges used by MFC control bars)
const int kHeaderControlId = 0xE9F0;
const int kStatusbarControlId = 0xE9F1;

// Fills menu bar, toolbar buttons, flag buttons and application icon.
void ConfigureHeader(modernui::ModernHeader* header, CFrameWnd* frame);

// Formula editor: compact header (title bar, menu bar from IDR_FORMULA_MENU,
// toolbar). The dialog is the target of all commands.
void ConfigureFormulaHeader(modernui::ModernHeader* header);
modernui::Icon IconForFormulaCommand(int command_id);

// The command IDs of the 20 flag buttons (ID_NUMBER0 ... ID_NUMBER19 are not consecutive).
const int* FlagCommandIds();

// Icon shown in front of a menu entry (Icon::None if there is none).
modernui::Icon IconForCommand(int command_id);

}  // namespace modernglue

#endif  // INC_MODERNFRAMESUPPORT_H
