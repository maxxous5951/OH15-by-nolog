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

#ifndef INC_OPENHOLDEM_STATUSBAR_H
#define INC_OPENHOLDEM_STATUSBAR_H

namespace modernui { class ModernStatusbar; }

class COpenHoldemStatusbar {
 public:
	COpenHoldemStatusbar(CWnd *main_window);
	~COpenHoldemStatusbar();
 public:
	void OnUpdateStatusbar();
	void GetWindowRect(RECT *statusbar_position);
	int  Height(float dpi_scale);
	void Move(int x, int y, int width, int height);
 public:
   void SetLastAction(CString action) { _last_action = action; }
   // What the autoplayer (or the advisor) really did / would do
   void SetActionTaken(CString action) { _action_taken = action; }
   void SetHandrank(int handrank)     { _handrank = handrank; }
   void SetPrWin(double prwin, double prtie, double prlos);
 private:
	void InitStatusbar();
  CString LastAction();
  CString ActionTaken() { return _action_taken; }
 private:
	modernui::ModernStatusbar *_status_bar;
	CWnd    *_main_window;
 private:
  int _handrank;
  double _prwin;
  double _prtie;
  double _prlos;
 private:
  // Info to be displayed
  CString _last_action;
  CString _action_taken;
  // to do!!! last line
  CString	_status_handrank;
  CString	_status_prwin;
};

extern COpenHoldemStatusbar *p_openholdem_statusbar;

#endif // INC_STATUSBAR_H