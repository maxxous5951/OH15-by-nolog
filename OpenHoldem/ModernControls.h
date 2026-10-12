//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: MFC glue: themed (light / dark) versions of the standard controls
//   that are used inside the dialogs converted to the modern UI.
//
//   * CModernButton   - push button / push-like check box, drawn with GDI+
//   * CModernTabCtrl  - tab control drawn as a row of pills
//   * helpers to give tree views, edits and scrollbars the dark / light look
//
//   All classes are drop-in replacements: they subclass the existing control
//   (DDX_Control), so the dialog code keeps using GetCheck(), InsertItem(), ...
//
//******************************************************************************

#ifndef INC_MODERNCONTROLS_H
#define INC_MODERNCONTROLS_H

#include "ModernUI/ModernTheme.h"

namespace modernglue {

class CModernButton : public CButton {
 public:
  CModernButton() : _primary(false), _hot(false), _tracking(false) {}
  // Primary buttons (Apply) are filled with the accent colour.
  void SetPrimary(bool primary) { _primary = primary; if (m_hWnd) Invalidate(); }
 protected:
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
  afx_msg void OnSetFocus(CWnd* pOld);
  afx_msg void OnKillFocus(CWnd* pNew);
  afx_msg void OnEnable(BOOL bEnable);
  DECLARE_MESSAGE_MAP()
 private:
  bool _primary, _hot, _tracking;
};

class CModernTabCtrl : public CTabCtrl {
 public:
  CModernTabCtrl() : _hot(-1), _tracking(false) {}
 protected:
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
  DECLARE_MESSAGE_MAP()
 private:
  int  _hot;
  bool _tracking;
};

// Dark / light "Explorer" look for trees, edits, list boxes and scrollbars.
// Safe to call again after the theme changed.
void ApplyControlTheme(HWND hwnd);
// Colours and row height of a tree view (call after ApplyControlTheme).
void ApplyTreeColors(CTreeCtrl* tree);
// Re-applies everything for the current palette.
void ApplyEditColors(CEdit* edit);

}  // namespace modernglue

#endif  // INC_MODERNCONTROLS_H
