//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: HUD of the advisor mode.
//   A small label above the balance of every opponent shows statistics from the
//   OpenHoldem stats database. The statistics depend on the betround
//   (preferences: HUD page, one list per betround). The labels are
//   click-through windows owned by the table window, like the advisor window.
//
//   Threads: OnHeartbeat() (heartbeat thread) calculates the texts;
//   OnGuiTimer() (GUI thread, called by CAdvisor) positions / paints the labels.
//
//   The text logic itself is in HudFormat.h (no Windows, tested on its own).
//
//******************************************************************************

#ifndef INC_CADVISORHUD_H
#define INC_CADVISORHUD_H

#include "..\Shared\CCritSec\CCritSec.h"

const int kHudChairs = 10;

// Calculated by the heartbeat thread, copied by the GUI thread
struct HudChairData {
  bool    visible;
  CString text;       // "92/71/12 (119)"
  DWORD   colour;     // 0xAARRGGBB, by player type
  CRect   balance;    // balance (else name) region, relative to the table client area
  HudChairData() : visible(false), colour(0xFF8A8F98) { balance.SetRectEmpty(); }
};

// One label = one small layered window
class CHudLabel : public CWnd {
 public:
  CHudLabel();
  virtual ~CHudLabel();
  bool CreateLabel();
  void SetContent(const CString& text, DWORD colour);
  CString text() { return _text; }
  float MeasureWidth();       // device pixels
  int   Height();
 protected:
  afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
  afx_msg void OnDestroy();
  afx_msg LRESULT OnThemeChanged(WPARAM, LPARAM);
  DECLARE_MESSAGE_MAP()
 private:
  CString _text;
  DWORD   _colour;
};

class CAdvisorHud {
 public:
  CAdvisorHud();
  ~CAdvisorHud();
 public:
  // Heartbeat thread
  void OnHeartbeat();
  // GUI thread
  void OnGuiTimer();
  // Any thread: forget everything (labels get hidden by the next timer)
  void Reset();
 private:
  void HideAll();
 private:
  CCritSec     _critsec;
  HudChairData _data[kHudChairs];
 private:
  // GUI thread only
  CHudLabel    _labels[kHudChairs];
  HWND         _table_window;
  bool         _shown[kHudChairs];
  CRect        _last_rect[kHudChairs];
};

#endif // INC_CADVISORHUD_H
