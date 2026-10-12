//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Main toolbar and flags of the main window.
//
//******************************************************************************

#include "stdafx.h"
#include "CFlagsToolbar.h"

#include <afxwin.h>
#include "CAutoplayer.h"
#include "CEngineContainer.h"
#include "ModernFrameSupport.h"
#include "ModernUI/ModernHeader.h"
#include "MyCtoolbar.h"


CFlagsToolbar *p_flags_toolbar = NULL;

#define ENT CSLock lock(m_critsec);

BEGIN_MESSAGE_MAP(CFlagsToolbar, CWnd)
END_MESSAGE_MAP()

CFlagsToolbar::CFlagsToolbar(CFrameWnd *parent_window) {
	_parent_window = parent_window;
  for (int i = 0; i < kNumberOfFlags; i++) {
    _flags[i] = false;
  }
  _header = new modernui::ModernHeader();
  modernglue::ConfigureHeader(_header, _parent_window);
  _header->Create(_parent_window->m_hWnd, modernglue::kHeaderControlId);

	// Autoplayer and shoot-frame buttons start out disabled
	_header->EnableButton(ID_MAIN_TOOLBAR_AUTOPLAYER, false);
	_header->EnableButton(ID_MAIN_TOOLBAR_SHOOTFRAME, false);

	bool to_be_enabled_or_not = p_autoplayer->autoplayer_engaged();
	_header->CheckButton(ID_MAIN_TOOLBAR_AUTOPLAYER, to_be_enabled_or_not);
	for (int i=0; i<kNumberOfFlags; i++) {
		SetFlag(i, false);
	}
  ResetButtonsOnDisconnect();
  ResetButtonsOnAutoplayerOff();
}

CFlagsToolbar::~CFlagsToolbar() {
  delete _header;
  _header = NULL;
}

void CFlagsToolbar::ResetButtonsOnConnect() {
	ResetButtonsOnAutoplayerOff();
	_header->EnableButton(ID_MAIN_TOOLBAR_SHOOTFRAME, true);
  _header->EnableButton(ID_MAIN_TOOLBAR_SCRAPER_OUTPUT, true);
}

void CFlagsToolbar::ResetButtonsOnDisconnect() {
  ResetButtonsOnAutoplayerOff();
	_header->CheckButton(ID_MAIN_TOOLBAR_AUTOPLAYER, false);
  _header->EnableButton(ID_MAIN_TOOLBAR_SCRAPER_OUTPUT, false);
}

void CFlagsToolbar::ResetButtonsOnAutoplayerOn() {
  _header->EnableButton(ID_FILE_NEW, false);
	_header->EnableButton(ID_FILE_OPEN, false);
  _header->EnableButton(ID_EDIT_FORMULA, false);
  CheckButton(ID_MAIN_TOOLBAR_AUTOPLAYER, true);
}

void CFlagsToolbar::ResetButtonsOnAutoplayerOff() {
  _header->EnableButton(ID_FILE_NEW, true);
	_header->EnableButton(ID_FILE_OPEN, true);
  _header->EnableButton(ID_EDIT_FORMULA, true);
  CheckButton(ID_MAIN_TOOLBAR_AUTOPLAYER, false);
}

bool CFlagsToolbar::GetFlag(int flag_number) {
	ENT
	if (flag_number>=0 && flag_number<kNumberOfFlags) {
		return _flags[flag_number]; 
	}	else {
		return false; 
	}
}

int CFlagsToolbar::GetFlagMax() {
	int flag_max = -1;
	for (int i=0; i<kNumberOfFlags; i++) {
		if (_flags[i]) {
			flag_max = i;
		}
	}
	return flag_max;
}

long int CFlagsToolbar::GetFlagBits() {
	long int flag_bits = 0;
	for (int i=0; i<kNumberOfFlags; i++) {
		if (_flags[i]) {
			flag_bits |= (1 << i);
		}
	}
	return flag_bits;
}

void CFlagsToolbar::SetFlag(int flag_number, bool new_value) {
	ENT
	// Only a range-check, no error-message here,
	// as this function might get called by external programs
	// via window-messages.
	if (flag_number>=0 && flag_number<kNumberOfFlags) {
		_flags[flag_number] = new_value; 
    _header->CheckButton(modernglue::FlagCommandIds()[flag_number], new_value);
	}
}

void CFlagsToolbar::OnClickedFlags() {
  for (int i = 0; i < kNumberOfFlags; ++i) {
    SetFlag(i, _header->IsButtonChecked(modernglue::FlagCommandIds()[i]));
  }
	// No longer calling p_engine_container->EvaluateAll();
  // It might cause unexpected side-effects,
  // if we evaluate multiple times during one heartbeat.
  // The effect of this button-click will have to wait
  // till the next heartbeat starts.
}

void CFlagsToolbar::UnattachOHFromPokerWindow() {
}

void CFlagsToolbar::EnableButton(int button_ID, bool new_status) {
	_header->EnableButton(button_ID, new_status);
}

void CFlagsToolbar::CheckButton(int button_ID, bool new_status) {
	_header->CheckButton(button_ID, new_status);
}

bool CFlagsToolbar::IsButtonChecked(int button_ID) {
	return _header->IsButtonChecked(button_ID);
}

bool CFlagsToolbar::IsButtonEnabled(int button_ID) {
	return _header->IsButtonEnabled(button_ID);
}

void CFlagsToolbar::SetTitle(const char *title) {
  _header->SetTitle(modernui::ToWide(title));
}

void CFlagsToolbar::SetConnected(bool connected) {
  _header->SetConnected(connected);
}

bool CFlagsToolbar::OpenMenuByMnemonic(wchar_t character) {
  return _header->OpenMenuByMnemonic(character);
}

int CFlagsToolbar::HeaderHeight(float dpi_scale) {
  return _header->Height(dpi_scale);
}

int CFlagsToolbar::TitleBarHeight() {
  return _header->TitleBarHeight(modernui::DpiScale(_header->hwnd()));
}

void CFlagsToolbar::MoveHeader(int x, int y, int width, int height) {
  ::MoveWindow(_header->hwnd(), x, y, width, height, TRUE);
}
