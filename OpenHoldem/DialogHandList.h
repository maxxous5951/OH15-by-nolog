//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Hand list editor (modern UI: themed dialog, one hand matrix
//   instead of 169 check boxes, selection by dragging the mouse).
//
//******************************************************************************

#ifndef INC_DIALOGHANDLIST_H
#define INC_DIALOGHANDLIST_H

#include "resource.h"
#include "COHScriptList.h"
#include "ModernControls.h"
#include "ModernHandGrid.h"

// CDlgHandList dialog

class CDlgHandList : public CDialog {
  friend class DialogFormulaScintilla;
public:
	CDlgHandList(CWnd* pParent = NULL);   // standard constructor
	virtual ~CDlgHandList();
	virtual BOOL OnInitDialog();

	enum { IDD = IDD_HANDLIST };
  COHScriptList *p_list;

protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV support
	afx_msg void OnBnClickedOk();
	afx_msg void OnBnClickedPP();
	afx_msg void OnBnClickedBroadways();
	afx_msg void OnBnClickedSuited();
	afx_msg void OnBnClickedClear();
	afx_msg LRESULT OnGridChanged(WPARAM, LPARAM);
	afx_msg LRESULT OnThemeChanged(WPARAM, LPARAM);
	afx_msg BOOL OnEraseBkgnd(CDC* pDC);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnDestroy();

	CStatic m_HandList_Name, m_CommentST;
	modernglue::CHandGridWnd m_Grid;
	modernglue::CModernButton m_ButtonPP, m_ButtonBroadways, m_ButtonSuited,
		m_ButtonClear, m_ButtonOK, m_ButtonCancel;

private:
	void LayoutControls();
	void UpdateCount();
	void ApplyTheme();
	CBrush m_brush_window;
	CFont  m_font_title, m_font_count;
	DECLARE_MESSAGE_MAP()
};


#endif //INC_DIALOGHANDLIST_H
