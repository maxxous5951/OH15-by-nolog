//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: The main view: draws the poker table, seats, cards, pot and the
//   indicators for the clickable casino buttons.
//   Since the "2026 UI" the drawing is done by modernui::DrawTable() (GDI+,
//   light/dark theme). This class only collects the data from the engine
//   (into a snapshot) and repaints when the snapshot changed.
//
//******************************************************************************

#ifndef INC_OPENHOLDEMVIEW_H
#define INC_OPENHOLDEMVIEW_H

#include "OpenHoldemDoc.h"
#include "Card.h"

#define DISPLAY_UPDATE_TIMER	111

namespace modernui { struct TableView; }

// OpenHoldemView.h : interface of the COpenHoldemView class

class COpenHoldemView : public CView {
 protected: // create from serialization only
	COpenHoldemView();
	DECLARE_DYNCREATE(COpenHoldemView)
	DECLARE_MESSAGE_MAP()
  afx_msg void OnTimer(UINT_PTR nIDEvent);
  afx_msg BOOL OnEraseBkgnd(CDC *pDC);
 public:
	COpenHoldemDoc* GetDocument() const;
	virtual void OnDraw(CDC* pDC);  // overridden to draw this view
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
	virtual ~COpenHoldemView();
	virtual void OnInitialUpdate();
 private:
  // Reads the engine state. Must only be called while the heartbeat
  // is not updating (cs_update_in_progress), see OnTimer().
  void BuildSnapshot(modernui::TableView *snapshot);
  void RefreshSnapshot();
 private:
  modernui::TableView *_snapshot;   // what is currently on screen
  bool _snapshot_valid;
};

inline COpenHoldemDoc* COpenHoldemView::GetDocument() const
   { return reinterpret_cast<COpenHoldemDoc*>(m_pDocument); }


#endif //INC_OPENHOLDEMVIEW_H
