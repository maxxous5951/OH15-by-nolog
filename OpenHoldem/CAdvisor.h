//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Advisor mode.
//   Instead of playing automatically (autoplayer) OpenHoldem shows a small,
//   themed window on top of the poker table, which tells
//     * the BEST ACTION, i.e. what the profile would do right now
//     * the ACTION TAKEN, i.e. the action of the bot in the last turn.
//
//   The window
//     * is only visible if the advisor is enabled and we are connected
//     * follows the table window when the table gets moved or minimized
//     * can be dragged with the mouse and snaps to one of the 4 corners
//       of the table (top left / top right / bottom left / bottom right)
//
//   Threads: OnHeartbeat() is called by the heartbeat thread and only
//   calculates and stores data; all window handling happens in the GUI thread.
//
//******************************************************************************

#ifndef INC_CADVISOR_H
#define INC_CADVISOR_H

#include "..\Shared\CCritSec\CCritSec.h"

// The kinds of actions, used for texts, colours and for comparing
// the recommendation with the real action.
enum AdvisorActionKind {
  kAdvisorActionNone = 0,
  kAdvisorActionFold,
  kAdvisorActionCheck,
  kAdvisorActionCall,
  kAdvisorActionRaise,
  kAdvisorActionAllin
};

// The corners (stored in the preferences)
enum AdvisorCorner {
  kAdvisorTopLeft = 0,
  kAdvisorTopRight,
  kAdvisorBottomLeft,
  kAdvisorBottomRight
};

// Everything the window shows. Written by the heartbeat thread,
// read (copied) by the GUI thread.
struct AdvisorData {
  AdvisorActionKind best_kind;
  CString best_text;      // "RAISE to 12.50"
  CString detail_text;    // "Pot 6.00 | To call 2.00"
  AdvisorActionKind taken_kind;   // what the bot did in the last turn
  CString taken_text;
  bool    my_turn;
  bool    autoplayer_engaged;
  AdvisorData() : best_kind(kAdvisorActionNone), taken_kind(kAdvisorActionNone),
    my_turn(false), autoplayer_engaged(false) {}
};

class CAdvisor : public CWnd {
 public:
  CAdvisor();
  virtual ~CAdvisor();
 public:
  // GUI thread: creates the (initially hidden) window
  bool CreateAdvisorWindow(CWnd* main_window);
  // GUI thread: toggles the mode and saves it in the preferences
  void SetEnabled(bool enabled);
  bool IsEnabled();
  // Any thread: re-reads the preferences (enabled, corner)
  void RefreshFromPreferences();
 public:
  // Heartbeat thread: called by the autoplayer-trace when the decision got logged
  // ("Best action" / "Action taken" of the log). Nothing is evaluated here.
  void OnDecision(CString best_action, CString action_taken,
                  double pot, double to_call, double betsize);
  // Heartbeat thread: updates "your turn" / "waiting"
  void OnHeartbeat();
  // Forget everything (disabled, disconnected)
  void Reset();
 protected:
  afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
  afx_msg void OnTimer(UINT_PTR id);
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
  afx_msg void OnCaptureChanged(CWnd* pWnd);
  afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
  afx_msg void OnDestroy();
  afx_msg LRESULT OnAdvisorUpdate(WPARAM, LPARAM);
  afx_msg LRESULT OnThemeChanged(WPARAM, LPARAM);
  DECLARE_MESSAGE_MAP()
 private:
  // heartbeat thread
  CString MoneyText(double amount);
  void ClassifyAction(const CString& raw, double betsize, double to_call,
                      AdvisorActionKind* kind, CString* text);
 private:
  // GUI thread
  void UpdateVisibilityAndPosition();
  CSize WindowSize();
  CPoint CornerPosition(const CRect& table_client, int corner);
  int  NearestCorner(const CRect& table_client, const CRect& advisor);
  bool TableClientRectOnScreen(CRect* rect);
 private:
  // shared between the threads
  CCritSec    _data_critsec;
  AdvisorData _data;
 private:
  // GUI thread only
  bool   _dragging;
  CPoint _drag_offset;
  HWND   _table_window;            // the window we are owned by / follow
  CRect  _last_table_rect;
  bool   _visible_state;
  bool   _created;
};

extern CAdvisor *p_advisor;

#endif // INC_CADVISOR_H
