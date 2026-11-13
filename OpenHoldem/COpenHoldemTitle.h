//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose:
//
//******************************************************************************

#include "CSpaceOptimizedGlobalObject.h"

class COpenHoldemTitle : public CSpaceOptimizedGlobalObject
{
public:
	COpenHoldemTitle();
	~COpenHoldemTitle();
public:
	CString GetTitle();
	void SetUserDefinedOpenHoldemTitle(CString new_title);
	// One of the 15 "Window Label" preferences, picked at launch so that it is
	// unique among the OpenHoldem windows currently running (see MainFrm.cpp).
	// When set, it becomes the REAL window text (see UpdateTitle()): what shows in
	// the taskbar, Alt+Tab, and anywhere else Windows reports this window's title.
	// Empty if no label is configured or all 15 are already taken.
	void SetWindowLabel(CString label) { window_label = label; }
	CString GetWindowLabel() { return window_label; }
	void UpdateTitle();
private:
	CString FullTitle();			// normal title with table-name, casino and limits
private:
	CString user_defined_title;		// defined via hopper-messages
	CString simple_title;			// executale-name
	CString window_label;			// see SetWindowLabel()
};

extern COpenHoldemTitle *p_openholdem_title;
