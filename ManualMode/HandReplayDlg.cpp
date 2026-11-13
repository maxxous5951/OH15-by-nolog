//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Independent "Hand Replay" window of ManualMode
//
//******************************************************************************

#include "stdafx.h"
#include "HandReplayDlg.h"
#include "ManualModeDlg.h"

static const UINT_PTR kPlayTimer = 1;
static const UINT kDefaultDelayMs = 800;

BEGIN_MESSAGE_MAP(CHandReplayDlg, CDialog)
  ON_BN_CLICKED(IDC_HAND_LOAD, &CHandReplayDlg::OnBnClickedLoad)
  ON_BN_CLICKED(IDC_HAND_PREV, &CHandReplayDlg::OnBnClickedPrev)
  ON_BN_CLICKED(IDC_HAND_NEXT, &CHandReplayDlg::OnBnClickedNext)
  ON_BN_CLICKED(IDC_HAND_PREVHAND, &CHandReplayDlg::OnBnClickedPrevHand)
  ON_BN_CLICKED(IDC_HAND_NEXTHAND, &CHandReplayDlg::OnBnClickedNextHand)
  ON_BN_CLICKED(IDC_HAND_NEXTDEC, &CHandReplayDlg::OnBnClickedNextDecision)
  ON_BN_CLICKED(IDC_HAND_PLAY, &CHandReplayDlg::OnBnClickedPlay)
  ON_WM_TIMER()
  ON_WM_DESTROY()
END_MESSAGE_MAP()

CHandReplayDlg::CHandReplayDlg(CWnd* pParent /*=NULL*/) : CDialog(CHandReplayDlg::IDD, pParent) {
  m_main = NULL;
  m_playing = false;
}

BOOL CHandReplayDlg::OnInitDialog() {
  CDialog::OnInitDialog();
  // Default limit of edit-controls is only 32k
  CEdit* edit = (CEdit*)GetDlgItem(IDC_HH_TEXT);
  if (edit != NULL) {
    edit->SetLimitText(1000000);
  }
  SetDlgItemInt(IDC_HAND_DELAY, kDefaultDelayMs, FALSE);
  return TRUE;
}

bool CHandReplayDlg::Open(CManualModeDlg* main_window) {
  m_main = main_window;
  if (GetSafeHwnd() == NULL) {
    if (!Create(IDD, main_window)) return false;
    PlaceBeside(main_window);
    UpdateStatus();
    // Do not steal the focus from the ManualMode window
    ShowWindow(SW_SHOWNOACTIVATE);
    return true;
  }
  if (!IsWindowVisible()) {
    ShowWindow(SW_SHOWNOACTIVATE);
  }
  BringWindowToTop();
  return true;
}

// Right of the ManualMode window, otherwise left, otherwise below.
// Never on top of it: OpenHoldem must see the whole table.
void CHandReplayDlg::PlaceBeside(CWnd* main_window) {
  CRect mw, rw;
  main_window->GetWindowRect(&mw);
  GetWindowRect(&rw);
  MONITORINFO mi;
  memset(&mi, 0, sizeof(mi));
  mi.cbSize = sizeof(mi);
  HMONITOR monitor = MonitorFromWindow(main_window->GetSafeHwnd(), MONITOR_DEFAULTTONEAREST);
  GetMonitorInfo(monitor, &mi);
  int x = mw.right + 8;
  int y = mw.top;
  if (x + rw.Width() > mi.rcWork.right) {
    x = mw.left - rw.Width() - 8;
  }
  if (x < mi.rcWork.left) {
    x = mi.rcWork.left;
    y = mw.bottom + 8;
    if (y + rw.Height() > mi.rcWork.bottom) {
      y = mi.rcWork.bottom - rw.Height();
    }
  }
  SetWindowPos(NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void CHandReplayDlg::UpdateStatus() {
  if (m_main == NULL) return;
  const bool loaded = m_main->HandLoaded();
  SetDlgItemText(IDC_HAND_STATUS, m_main->HandStatus());
  GetDlgItem(IDC_HAND_PREV)->EnableWindow(loaded && m_main->HandStep() > 0);
  GetDlgItem(IDC_HAND_NEXT)->EnableWindow(loaded && m_main->HandStep() < m_main->HandStepCount());
  GetDlgItem(IDC_HAND_PREVHAND)->EnableWindow(loaded && m_main->HandIndex() > 0);
  GetDlgItem(IDC_HAND_NEXTHAND)->EnableWindow(loaded && m_main->HandIndex() + 1 < m_main->HandCount());
  GetDlgItem(IDC_HAND_NEXTDEC)->EnableWindow(loaded);
  GetDlgItem(IDC_HAND_PLAY)->EnableWindow(loaded);
}

void CHandReplayDlg::OnBnClickedLoad() {
  if (m_main == NULL) return;
  StopPlay();
  CString text, hero, error;
  GetDlgItemText(IDC_HH_TEXT, text);
  GetDlgItemText(IDC_HH_HERO, hero);
  if (!m_main->ImportHand(text, hero, &error)) {
    MessageBox(error, "Hand Replay", MB_ICONWARNING);
  }
  else {
    // Show the hero that is actually used (maybe taken from "Dealt to")
    SetDlgItemText(IDC_HH_HERO, m_main->HandHero());
  }
  UpdateStatus();
}

// Every manual navigation stops the automatic playback

void CHandReplayDlg::OnBnClickedPrev() {
  StopPlay();
  if (m_main != NULL) m_main->HandStepPrev();
  UpdateStatus();
}

void CHandReplayDlg::OnBnClickedNext() {
  StopPlay();
  if (m_main != NULL) m_main->HandStepNext();
  UpdateStatus();
}

void CHandReplayDlg::OnBnClickedPrevHand() {
  StopPlay();
  if (m_main != NULL) m_main->PrevHand();
  UpdateStatus();
}

void CHandReplayDlg::OnBnClickedNextHand() {
  StopPlay();
  if (m_main != NULL) m_main->NextHand();
  UpdateStatus();
}

void CHandReplayDlg::OnBnClickedNextDecision() {
  StopPlay();
  if (m_main != NULL && !m_main->HandStepToDecision()) {
    MessageBox("No more decisions of the hero.", "Hand Replay", MB_ICONINFORMATION);
  }
  UpdateStatus();
}

/*****************************************************
*
* Automatic playback: one step per tick, over all hands.
* It stops as soon as it is the hero's turn (time for OpenHoldem to
* answer). "Play" again continues with the action of the hero.
*
*****************************************************/

UINT CHandReplayDlg::DelayMs() {
  UINT delay = GetDlgItemInt(IDC_HAND_DELAY, NULL, FALSE);
  if (delay == 0) delay = kDefaultDelayMs;
  if (delay < 100) delay = 100;
  if (delay > 10000) delay = 10000;
  return delay;
}

void CHandReplayDlg::StartPlay() {
  if (m_main == NULL || !m_main->HandLoaded()) return;
  m_playing = true;
  SetTimer(kPlayTimer, DelayMs(), NULL);
  SetDlgItemText(IDC_HAND_PLAY, "&Stop");
}

void CHandReplayDlg::StopPlay() {
  if (!m_playing) return;
  m_playing = false;
  KillTimer(kPlayTimer);
  SetDlgItemText(IDC_HAND_PLAY, "&Play");
}

void CHandReplayDlg::OnBnClickedPlay() {
  if (m_playing) StopPlay();
  else StartPlay();
}

void CHandReplayDlg::OnTimer(UINT_PTR event_id) {
  if (event_id != kPlayTimer) {
    CDialog::OnTimer(event_id);
    return;
  }
  if (m_main == NULL || !m_main->HandLoaded()) {
    StopPlay();
    return;
  }
  if (m_main->HandStep() >= m_main->HandStepCount()) {
    // End of hand: go on with the next hand, or stop after the last one
    if (!m_main->NextHand()) StopPlay();
  }
  else {
    m_main->HandStepNext();
    if (m_main->HeroToAct()) StopPlay();
  }
  UpdateStatus();
  if (m_playing) {
    // Re-arm with the current delay, so changes of the delay apply immediately
    SetTimer(kPlayTimer, DelayMs(), NULL);
  }
}

void CHandReplayDlg::OnDestroy() {
  StopPlay();
  CDialog::OnDestroy();
}

/*****************************************************
*
* Keyboard shortcuts
*
*****************************************************/

bool CHandReplayDlg::HandleShortcut(UINT virtual_key) {
  if (m_main == NULL || !m_main->HandLoaded()) return false;
  // Ctrl/Alt combinations belong to Windows and the mnemonics
  if ((GetKeyState(VK_CONTROL) & 0x8000) || (GetKeyState(VK_MENU) & 0x8000)) return false;
  // Never steal keys while the user is typing
  CWnd* focus = GetFocus();
  if (focus != NULL) {
    int id = focus->GetDlgCtrlID();
    if (id == IDC_HH_TEXT || id == IDC_HH_HERO || id == IDC_HAND_DELAY || id == IDC_SWAG) {
      return false;
    }
  }
  switch (virtual_key) {
    case VK_RIGHT: OnBnClickedNext(); return true;
    case VK_LEFT:  OnBnClickedPrev(); return true;
    case VK_DOWN:  OnBnClickedNextHand(); return true;
    case VK_UP:    OnBnClickedPrevHand(); return true;
    case VK_NEXT:  OnBnClickedNextDecision(); return true;  // PageDown
    case 'P':      OnBnClickedPlay(); return true;
  }
  return false;
}

// Must run before the dialog manager, which would use the arrow keys
// to move the focus between the buttons.
BOOL CHandReplayDlg::PreTranslateMessage(MSG* pMsg) {
  if (pMsg->message == WM_KEYDOWN) {
    if (HandleShortcut((UINT)pMsg->wParam)) return TRUE;
  }
  return CDialog::PreTranslateMessage(pMsg);
}

void CHandReplayDlg::OnOK() {
  // Intentionally empty
}

void CHandReplayDlg::OnCancel() {
  StopPlay();
  ShowWindow(SW_HIDE);
}
