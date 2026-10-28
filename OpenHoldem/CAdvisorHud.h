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
//   Labels with statistics of the OpenHoldem stats database, next to every
//   opponent. The statistics depend on the betround (preferences: page HUD).
//   Four styles (preferences), see HudFormat.h / the preview on the HUD page:
//     0 pill    one line, above the seat
//     1 tab     two lines, directly under the seat
//     2 column  labelled column next to the seat, outside of the table
//     3 badge   coloured badge, all stats of the round on mouse-over
//   The labels never overlap each other, the seats, the cards, the bets, the
//   dealer button or the board (HudFormat.h: PlaceHudLabels).
//   The labels are small windows owned by the table window.
//
//   Threads: OnHeartbeat() (heartbeat thread) calculates texts and the places to
//   avoid; OnGuiTimer() (GUI thread, called by CAdvisor) places and paints.
//
//   The text and placement logic is in HudFormat.h (no Windows, tested on its own).
//
//******************************************************************************

#ifndef INC_CADVISORHUD_H
#define INC_CADVISORHUD_H

#include "..\Shared\CCritSec\CCritSec.h"
#include <functional>
#include "HudFormat.h"

namespace Gdiplus { class Graphics; }

const int kHudChairs = 10;

// Size (device pixels) of a label of the style; the mouse-over card of the badge style
// is included if "expanded"
void HudLabelSize(Gdiplus::Graphics& g, int style, const ohstats::HudText& text, float scale,
                  bool expanded, float* width, float* height);
// Paints a label into (0, 0, width, height). Also used by the preview of the preferences.
void DrawHudLabel(Gdiplus::Graphics& g, int style, const ohstats::HudText& text, float scale,
                  bool expanded, float width, float height);

// Calculated by the heartbeat thread, copied by the GUI thread
struct HudChairData {
  bool             visible;
  ohstats::HudText text;
  CRect            seat;      // name + balance of the player, relative to the table client area
  HudChairData() : visible(false) { seat.SetRectEmpty(); }
};

// One label = one small layered window
class CHudLabel : public CWnd {
 public:
  CHudLabel();
  virtual ~CHudLabel();
  bool CreateLabel();
  // Returns true if the size changed
  void SetContent(int style, const ohstats::HudText& text);
  // Size in device pixels without the mouse-over card
  CSize LabelSize();
  bool  IsExpanded() { return _expanded; }
  void  SetClickThrough(bool click_through);
 protected:
  afx_msg int  OnCreate(LPCREATESTRUCT lpCreateStruct);
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
  afx_msg void OnDestroy();
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
  afx_msg LRESULT OnThemeChanged(WPARAM, LPARAM);
  DECLARE_MESSAGE_MAP()
 private:
  void Resize(bool expanded);
  void ApplyRoundedRegion(int width, int height);
 private:
  int              _style;
  ohstats::HudText _text;
  CSize            _size;
  bool             _size_valid;
  bool             _expanded;
  bool             _tracking;
  bool             _click_through;
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
  std::vector<ohstats::HudRect> _obstacles;    // cards, bets, dealer button, board, other seats
 private:
  // GUI thread only
  CHudLabel    _labels[kHudChairs];
  HWND         _table_window;
  bool         _shown[kHudChairs];
  CRect        _last_rect[kHudChairs];
};

// Example table for the preferences page: shows the selected style with sample
// players (English), placed by the same code as the real HUD.
class CHudPreviewWnd : public CWnd {
 public:
  CHudPreviewWnd();
  virtual ~CHudPreviewWnd();
  // style_getter: returns the style currently selected in the dialog (not yet saved)
  bool CreatePreview(CWnd* parent, const CRect& rect, std::function<int()> style_getter);
  int  PreferredHeight(int width);
 protected:
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
  afx_msg void OnTimer(UINT_PTR id);
  afx_msg void OnDestroy();
  DECLARE_MESSAGE_MAP()
 private:
  std::function<int()> _style_getter;
  int    _last_style;
};

#endif // INC_CADVISORHUD_H
