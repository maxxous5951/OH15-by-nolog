//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: "Import hand" dialog of ManualMode: paste a hand-history, name the hero.
//
//******************************************************************************

#pragma once
#include "afxwin.h"
#include "resource.h"

class CImportHandDlg : public CDialog {
 public:
  CImportHandDlg(CWnd* pParent = NULL);
  enum { IDD = IDD_IMPORTHAND };
  CString m_text;   // the pasted hand-history
  CString m_hero;   // player name; empty = the one in the "Dealt to" line

 protected:
  virtual void DoDataExchange(CDataExchange* pDX);
  virtual BOOL OnInitDialog();
};
