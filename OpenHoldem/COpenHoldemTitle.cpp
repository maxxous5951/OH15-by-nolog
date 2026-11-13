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

#include "stdafx.h"
#include "COpenHoldemTitle.h"

#include <assert.h>
#include "CAutoConnector.h"
#include "CFunctionCollection.h"

#include "../CTablemap/CTablemap.h"
#include "CTableTitle.h"
#include "MainFrm.h"
#include "CFlagsToolbar.h"

COpenHoldemTitle *p_openholdem_title = NULL;

COpenHoldemTitle::COpenHoldemTitle() {
	user_defined_title = "";
	simple_title = PureExecutableFilename();
}

COpenHoldemTitle::~COpenHoldemTitle()
{}

CString COpenHoldemTitle::GetTitle() {
	// user-defined overwrites everything
	if (user_defined_title != "") 	{
		return user_defined_title;
	}
	if (Preferences()->simple_window_title()) {
		return simple_title;
	}	else {
		return FullTitle();
	}
}

CString COpenHoldemTitle::FullTitle() {
	assert(p_autoconnector != NULL);
	assert(p_function_collection != NULL);
	assert(p_tablemap != NULL);
  CString full_title;
  write_log(Preferences()->debug_alltherest(), "[COpenHoldemTitle] location Johnny_6\n");
	if (p_autoconnector->IsConnectedToAnything())	{
		full_title.Format("%s | %s | %s", p_function_collection->FormulaName(),
			p_tablemap->sitename(), p_table_title->Title());
	}	else {
		full_title.Format("%s", p_function_collection->FormulaName());
	}
	return full_title;
}

void COpenHoldemTitle::SetUserDefinedOpenHoldemTitle(CString new_title) {
	user_defined_title = new_title;
	UpdateTitle();
}

void COpenHoldemTitle::UpdateTitle() {
	if (PMainframe() == NULL)	{
		// Missing main window can happen very early during execution
		// if OpenHoldem creates a default document with default title
		// but the window does not yet exist.
		return;
	}
	// PostMessage(WMA_SETWINDOWTEXT, 0, (LPARAM)(GetTitle().GetString()));
	// can't be used, because that would call COpenHoldemHopperCommunication::OnSetWindowText
	// which would then call SetUserDefinedOpenHoldemTitle()
	// which would call UpdateTitle()
	// -> endless recursion
	static CString current_title;
	current_title = GetTitle();
	HWND main_window = PMainframe()->GetSafeHwnd();
	// The REAL OS window text (taskbar, Alt+Tab, the scraper's "window to capture" list,
	// anything using EnumWindows / GetWindowText) is the window label alone when one is
	// set, so that it identifies the window there too, not just in our own custom header.
	CString os_title = window_label.IsEmpty() ? current_title : window_label;
	SetWindowText(main_window, os_title);
	// SetWindowText() above just sent WM_SETTEXT, which CMainFrame forwards verbatim to
	// the custom header (see CMainFrame::OnSetTextMessage). Overwrite it right after with
	// the full descriptive title: the header needs the formula/site/table names to fill
	// its own "profile + table" display and its title bar fallback, regardless of what
	// the OS-level window text was just shortened to.
	if (p_flags_toolbar != NULL) {
		p_flags_toolbar->SetTitle(current_title);
	}
}
