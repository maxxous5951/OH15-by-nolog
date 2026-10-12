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
//   Since the "2026 UI" this is a thin wrapper around modernui::ModernHeader
//   (custom title bar, themed menu bar, toolbar and flags). The public
//   interface is unchanged, so the rest of OpenHoldem does not notice.
//
//******************************************************************************

#ifndef INC_CFLAGSTOOLBAR_H
#define INC_CFLAGSTOOLBAR_H

#include "MyCtoolbar.h"

namespace modernui { class ModernHeader; }

class CFlagsToolbar: public CWnd {
 public:
	CFlagsToolbar(CFrameWnd *parent_window);
	~CFlagsToolbar();
 public:
	bool GetFlag(const int i); 
	int  GetFlagMax();
	long int  GetFlagBits();
  void SetFlag(int flag_number, bool new_value);
 public:
	void ResetButtonsOnConnect();
	void ResetButtonsOnDisconnect();
  void ResetButtonsOnAutoplayerOn();
  void ResetButtonsOnAutoplayerOff();
	void UnattachOHFromPokerWindow();
 public:
	void EnableButton(int button_ID, bool new_status);
	void CheckButton(int button_ID, bool new_status);
	bool IsButtonChecked(int button_ID);
	bool IsButtonEnabled(int button_ID);
 public:
  // Title bar, menu bar and layout
  void SetTitle(const char *title);
  void SetConnected(bool connected);
  bool OpenMenuByMnemonic(wchar_t character);
  int  HeaderHeight(float dpi_scale);
  int  TitleBarHeight();
  void MoveHeader(int x, int y, int width, int height);
 public:
	afx_msg void OnClickedFlags();
	DECLARE_MESSAGE_MAP()
 private:
	modernui::ModernHeader *_header;
	bool       _flags[kNumberOfFlags];
	CFrameWnd  *_parent_window;
	CCritSec   m_critsec;
};

extern CFlagsToolbar *p_flags_toolbar;

#endif // INC_CFLAGSTOOLBAR_H
