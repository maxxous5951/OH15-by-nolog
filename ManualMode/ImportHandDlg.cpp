//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: "Import hand" dialog of ManualMode
//
//******************************************************************************

#include "stdafx.h"
#include "ImportHandDlg.h"

CImportHandDlg::CImportHandDlg(CWnd* pParent /*=NULL*/) : CDialog(CImportHandDlg::IDD, pParent) {
}

void CImportHandDlg::DoDataExchange(CDataExchange* pDX) {
  CDialog::DoDataExchange(pDX);
  DDX_Text(pDX, IDC_HH_TEXT, m_text);
  DDX_Text(pDX, IDC_HH_HERO, m_hero);
}

BOOL CImportHandDlg::OnInitDialog() {
  CDialog::OnInitDialog();
  // Default limit of edit-controls is only 32k
  CEdit* edit = (CEdit*)GetDlgItem(IDC_HH_TEXT);
  if (edit != NULL) {
    edit->SetLimitText(1000000);
    edit->SetFocus();
    return FALSE;  // we set the focus
  }
  return TRUE;
}
