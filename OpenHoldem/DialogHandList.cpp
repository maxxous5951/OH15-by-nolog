//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Hand list editor (modern UI)
//
//******************************************************************************

// DialogHandList.cpp : implementation file
//

#include "stdafx.h"
#include "DialogHandList.h"

#include "ModernUI/ModernGfx.h"
#include "ModernUI/ModernTheme.h"


// CDlgHandList dialog
CDlgHandList::CDlgHandList(CWnd* pParent /*=NULL*/)
		: CDialog(CDlgHandList::IDD, pParent)
{
	p_list = NULL;
	// GDI+ is needed by the themed buttons and by the hand matrix
	modernui::GdiplusAcquire();
}

CDlgHandList::~CDlgHandList()
{
	modernui::GdiplusRelease();
}

void CDlgHandList::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_HANDLIST_NAME, m_HandList_Name);
	DDX_Control(pDX, IDC_COMMENT, m_CommentST);
	DDX_Control(pDX, IDANYPP, m_ButtonPP);
	DDX_Control(pDX, IDBROADWAYS, m_ButtonBroadways);
	DDX_Control(pDX, IDALLSUITED, m_ButtonSuited);
	DDX_Control(pDX, IDCLEAR, m_ButtonClear);
	DDX_Control(pDX, IDOK, m_ButtonOK);
	DDX_Control(pDX, IDCANCEL, m_ButtonCancel);
}

BEGIN_MESSAGE_MAP(CDlgHandList, CDialog)
	ON_BN_CLICKED(IDOK, &CDlgHandList::OnBnClickedOk)
	ON_BN_CLICKED(IDANYPP, &CDlgHandList::OnBnClickedPP)
	ON_BN_CLICKED(IDBROADWAYS, &CDlgHandList::OnBnClickedBroadways)
	ON_BN_CLICKED(IDALLSUITED, &CDlgHandList::OnBnClickedSuited)
	ON_BN_CLICKED(IDCLEAR, &CDlgHandList::OnBnClickedClear)
	ON_MESSAGE(modernglue::WMU_HANDGRID_CHANGED, &CDlgHandList::OnGridChanged)
	ON_MESSAGE(modernui::WMU_THEME_CHANGED, &CDlgHandList::OnThemeChanged)
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_WM_DESTROY()
END_MESSAGE_MAP()

// CDlgHandList message handlers
BOOL CDlgHandList::OnInitDialog()
{
	CDialog::OnInitDialog();
	ModifyStyle(0, WS_CLIPCHILDREN);

	assert(p_list != NULL);
	CString s = "";
	s.Format("Hand List Editor - %s", p_list->name());
	SetWindowText(s.GetString());
	s.Format("Hand list: %s", p_list->name());
	m_HandList_Name.SetWindowText(s.GetString());

	// Hand matrix
	m_Grid.CreateGrid(this, CRect(0, 0, 100, 100), modernglue::kHandGridControlId);
	for (int i = 2; i <= k_rank_ace; i++) {
		for (int j = 2; j <= k_rank_ace; j++) {
			m_Grid.SetSelected(i, j, p_list->IsOnList(i, j));
		}
	}
	m_ButtonOK.SetPrimary(true);

	// Light / dark like the rest of the application
	modernui::Theme::RegisterWindow(m_hWnd);
	ApplyTheme();
	LayoutControls();
	UpdateCount();
	CenterWindow();

	m_Grid.SetFocus();
	return FALSE;  // we have set the focus ourselves
}

void CDlgHandList::OnDestroy()
{
	modernui::Theme::UnregisterWindow(m_hWnd);
	CDialog::OnDestroy();
}

void CDlgHandList::ApplyTheme()
{
	const modernui::Palette& c = modernui::Theme::Colors();
	modernui::Theme::ApplyFrameAttributes(m_hWnd);
	m_brush_window.DeleteObject();
	m_brush_window.CreateSolidBrush(modernui::ToColorRef(c.window_bg));
}

LRESULT CDlgHandList::OnThemeChanged(WPARAM, LPARAM)
{
	ApplyTheme();
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
	return 0;
}

BOOL CDlgHandList::OnEraseBkgnd(CDC* pDC)
{
	CRect client;
	GetClientRect(&client);
	pDC->FillSolidRect(&client, modernui::ToColorRef(modernui::Theme::Colors().window_bg));
	return TRUE;
}

HBRUSH CDlgHandList::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	if (m_brush_window.GetSafeHandle() == NULL) {
		return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	}
	const modernui::Palette& c = modernui::Theme::Colors();
	const int id = (pWnd != NULL) ? pWnd->GetDlgCtrlID() : 0;
	pDC->SetTextColor(modernui::ToColorRef(id == IDC_COMMENT ? c.text_dim : c.text));
	pDC->SetBkColor(modernui::ToColorRef(c.window_bg));
	return (HBRUSH)m_brush_window.GetSafeHandle();
}

void CDlgHandList::LayoutControls()
{
	const float s = modernui::DpiScale(m_hWnd);
	const int margin = (int)(24 * s + 0.5f);
	const int gap = (int)(4 * s + 0.5f);
	const int header_height = (int)(24 * s + 0.5f);
	const int header_space = (int)(12 * s + 0.5f);
	const int button_height = (int)(34 * s + 0.5f);
	const int button_gap = (int)(8 * s + 0.5f);
	const int button_space = (int)(16 * s + 0.5f);

	// Cell size: 40 px, smaller on small screens (the dialog must fit)
	CRect work;
	::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
	const int fixed_height = 2 * margin + header_height + header_space + button_space
		+ button_height + GetSystemMetrics(SM_CYCAPTION) + 2 * GetSystemMetrics(SM_CYFIXEDFRAME);
	int cell = (int)(40 * s + 0.5f);
	const int max_cell = (work.Height() - fixed_height - 12 * gap) / 13;
	if (cell > max_cell) cell = max_cell;
	if (cell < (int)(26 * s)) cell = (int)(26 * s);
	const int grid_side = 13 * cell + 12 * gap;

	const int client_width = grid_side + 2 * margin;
	const int client_height = margin + header_height + header_space + grid_side
		+ button_space + button_height + margin;

	// Window size from the wanted client size
	CRect frame(0, 0, client_width, client_height);
	::AdjustWindowRectEx(&frame, GetStyle(), FALSE, GetExStyle());
	SetWindowPos(NULL, 0, 0, frame.Width(), frame.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

	// Fonts of the header
	m_font_title.DeleteObject();
	m_font_count.DeleteObject();
	m_font_title.CreateFont(-(int)(15 * s + 0.5f), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
	m_font_count.CreateFont(-(int)(14 * s + 0.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
	m_HandList_Name.SetFont(&m_font_title);
	m_CommentST.SetFont(&m_font_count);
	m_CommentST.ModifyStyle(SS_LEFT | SS_CENTER, SS_RIGHT);

	const int half = client_width / 2;
	m_HandList_Name.MoveWindow(margin, margin, half - margin, header_height);
	m_CommentST.MoveWindow(half, margin, client_width - margin - half, header_height);

	const int grid_top = margin + header_height + header_space;
	m_Grid.MoveWindow(margin, grid_top, grid_side, grid_side);

	// Button row: presets on the left, OK / Cancel on the right
	const int y = grid_top + grid_side + button_space;
	const int w_pairs = (int)(88 * s), w_broadways = (int)(100 * s), w_suited = (int)(92 * s);
	const int w_clear = (int)(68 * s), w_ok = (int)(80 * s), w_cancel = (int)(80 * s);
	int x = margin;
	m_ButtonPP.MoveWindow(x, y, w_pairs, button_height);               x += w_pairs + button_gap;
	m_ButtonBroadways.MoveWindow(x, y, w_broadways, button_height);    x += w_broadways + button_gap;
	m_ButtonSuited.MoveWindow(x, y, w_suited, button_height);          x += w_suited + button_gap;
	m_ButtonClear.MoveWindow(x, y, w_clear, button_height);
	x = client_width - margin - w_cancel;
	m_ButtonCancel.MoveWindow(x, y, w_cancel, button_height);
	x -= button_gap + w_ok;
	m_ButtonOK.MoveWindow(x, y, w_ok, button_height);
}

void CDlgHandList::UpdateCount()
{
	CString comment = "";
	const int nhands = m_Grid.HandCount();
	comment.Format("%d/1326=%.2f%%", nhands, ((double)nhands / 1326.0) * 100.0);
	m_CommentST.SetWindowText(comment);
}

LRESULT CDlgHandList::OnGridChanged(WPARAM, LPARAM)
{
	UpdateCount();
	return 0;
}

void CDlgHandList::OnBnClickedOk()
{
	// The hand list only gets modified when the user confirms
	for (int i = 2; i <= k_rank_ace; i++) {
		for (int j = 2; j <= k_rank_ace; j++) {
			if (m_Grid.IsSelected(i, j)) {
				p_list->Set(i, j);
			} else {
				p_list->Remove(i, j);
			}
		}
	}
	p_list->GenerateFunctionTextFromHandlistMatrix();
	OnOK();
}

void CDlgHandList::OnBnClickedPP()
{
	m_Grid.SelectPairs();
	UpdateCount();
	m_Grid.SetFocus();
}

void CDlgHandList::OnBnClickedBroadways()
{
	m_Grid.SelectBroadways();
	UpdateCount();
	m_Grid.SetFocus();
}

void CDlgHandList::OnBnClickedSuited()
{
	m_Grid.SelectSuited();
	UpdateCount();
	m_Grid.SetFocus();
}

void CDlgHandList::OnBnClickedClear()
{
	m_Grid.ClearAll();
	UpdateCount();
	m_Grid.SetFocus();
}
