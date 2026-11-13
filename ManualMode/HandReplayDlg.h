//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Independent (modeless) "Hand Replay" window of ManualMode.
//   Paste one or more PokerStars hand-histories, name the hero, step through
//   the hands (manually, to the next decision of the hero, or automatically).
//   It is a separate window, so the ManualMode table stays untouched
//   and OpenHoldem can scrape it undisturbed.
//
//   Keyboard (not while typing in an edit-control):
//     Left/Right = step back/forward     Up/Down = previous/next hand
//     PageDown   = next decision         P       = play/stop
//
//******************************************************************************

#pragma once
#include "afxwin.h"
#include "resource.h"

class CManualModeDlg;

class CHandReplayDlg : public CDialog {
 public:
  CHandReplayDlg(CWnd* pParent = NULL);
  enum { IDD = IDD_HANDREPLAY };
  // Creates the window (first call) and shows it beside the ManualMode window.
  bool Open(CManualModeDlg* main_window);
  void UpdateStatus();
  // Returns true if the key was a shortcut and has been handled
  bool HandleShortcut(UINT virtual_key);

 protected:
  virtual BOOL OnInitDialog();
  virtual BOOL PreTranslateMessage(MSG* pMsg);
  virtual void OnOK();      // Enter must not close a modeless window
  virtual void OnCancel();  // The close button only hides it
  afx_msg void OnBnClickedLoad();
  afx_msg void OnBnClickedPrev();
  afx_msg void OnBnClickedNext();
  afx_msg void OnBnClickedPrevHand();
  afx_msg void OnBnClickedNextHand();
  afx_msg void OnBnClickedNextDecision();
  afx_msg void OnBnClickedPlay();
  afx_msg void OnTimer(UINT_PTR event_id);
  afx_msg void OnDestroy();
  DECLARE_MESSAGE_MAP()

 private:
  void PlaceBeside(CWnd* main_window);
  void StartPlay();
  void StopPlay();
  UINT DelayMs();
  CManualModeDlg* m_main;
  bool m_playing;
};
