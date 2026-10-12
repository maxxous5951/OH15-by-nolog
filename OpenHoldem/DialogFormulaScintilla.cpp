//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose:
//
//******************************************************************************

// DialogFormulaScintilla.cpp : implementation file
//

// menu options, menu edit commands

#include "stdafx.h"
#include "DialogFormulaScintilla.h"

#include <io.h>
#include "CAutoplayer.h"
#include "CAutoplayerTrace.h"
#include "CDebugTab.h"
#include "CEngineContainer.h"
#include "CFlagsToolbar.h"
#include "CFunction.h"
#include "CFunctionCollection.h"
#include "CHeartbeatThread.h"
#include "COHScriptList.h"

#include "CScraper.h"
#include "CSymbolEngineAutoplayer.h"
#include "CSymbolEngineIniFunctions.h"
#include "DialogHandList.h"
#include "DialogNew.h"
#include "DialogRename.h"
#include "MainFrm.h"
#include "..\DLLs\WindowFunctions_DLL\window_functions.h"
#include "OpenHoldem.h"
#include "OpenHoldemDoc.h"
#include "..\PokerTracker_Query_Definitions\pokertracker_query_definitions.h"
#include "../scintilla/include/SciLexer.h"
#include "../scintilla/include/Scintilla.h"
#include "WinMgr.h"
#include "ModernUI/ModernGfx.h"


// CDlgFormulaScintilla dialog
CDlgFormulaScintilla	*m_formulaScintillaDlg = NULL;

// Keywords got changed from "char* to "CString"
// as we want to create this list dynamically
CString keywords;
				  
#define ID_SCIN_SIZERBAR 5555

static UINT WM_FINDREPLACE = ::RegisterWindowMessage(FINDMSGSTRING);

// Window map tells CWinMgr how to position dialog controls
BEGIN_WINDOW_MAP(ScintillaFormulaMap)
BEGINCOLS(WRCT_REST,0,RCMARGINS(4,4))

BEGINROWS(WRCT_TOFIT,0,RCMARGINS(0,4))
RCTOFIT(IDC_SEARCH_ED)
RCREST(IDC_SCINTILLA_FORMULA_TREE)	// tree
RCTOFIT(IDC_FORMULA_TAB)
ENDGROUP()

RCFIXED(ID_SCIN_SIZERBAR,4)			// vertical sizer bar

BEGINROWS(WRCT_REST,0,0)
BEGINCOLS(WRCT_TOFIT,0,0)
RCREST(-1)
RCTOFIT(IDC_SCINTILLA_CALC_RESULT)	// calc result
RCSPACE(8)
RCTOFIT(IDC_SCINTILLA_CALC_LINE)	// calc result
RCSPACE(8)
RCTOFIT(IDC_SCINTILLA_CALC)			// calc
RCSPACE(8)
RCTOFIT(IDC_SCINTILLA_AUTO)			// auto
ENDGROUP()

RCSPACE(8)

RCREST(IDC_SCINCTRL)				// formula edit

RCSPACE(4)

RCTOFIT(IDC_FUNCTIONS_TAB)			// function tab

RCSPACE(4)

BEGINCOLS(WRCT_TOFIT,0,0)
RCREST(-1)
RCTOFIT(IDOK)							// ok
RCSPACE(8)
RCTOFIT(IDC_SCINTILLA_APPLY)			// apply
RCSPACE(8)
RCTOFIT(IDCANCEL)						// cancel
ENDGROUP()
ENDGROUP()
END_WINDOW_MAP()

CDlgFormulaScintilla::CDlgFormulaScintilla(CWnd* pParent /*=NULL*/) :
		CDialog(CDlgFormulaScintilla::IDD, pParent), m_winMgr(ScintillaFormulaMap) 
{
	in_startup = true;
	m_word_wrap = false;
  
  if (p_function_collection->IsOpenPPLProfile()) {
    // Either use autoplayer-functions (default) or OpenPPL
    // but not both, because they are incompatible
    m_standard_headings.Add("OpenPPL Functions");
  } else {
    m_standard_headings.Add("Autoplayer Functions");
  }
	m_standard_headings.Add("Secondary Functions");
  m_standard_headings.Add("Hopper Functions");
	m_standard_headings.Add("Ini Functions");
	m_standard_headings.Add("PrWin Functions");
  m_standard_headings.Add("ICM Functions");
  m_standard_headings.Add("Debug Functions");
  ASSERT(m_standard_headings.GetSize() == k_number_of_standard_headings);
  
	m_current_edit = "";
	m_dirty = false;
  ok_to_update_debug = false;
  m_header = NULL;
  m_status = NULL;
  m_in_layout = false;
  m_status_position = "Line: 1, Col: 1";

	m_pActiveScinCtrl = NULL;
	m_pFRDlg = NULL;
 	hUDFItem = NULL;
	_edit_font.CreatePointFont(100, "Consolas");
}

CDlgFormulaScintilla::~CDlgFormulaScintilla() 
{
	for (int i=0; i<m_ScinArray.GetSize(); i++)
	{
		if (&m_EmptyScinCtrl != m_ScinArray.GetAt(i)._pWnd)
			delete m_ScinArray.GetAt(i)._pWnd;
	}
	m_ScinArray. RemoveAll(); 
	// Their windows are already gone at this point (see PostNcDestroy)
	delete m_status;
	delete m_header;
}

void CDlgFormulaScintilla::DoDataExchange(CDataExchange* pDX) 
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_SCINCTRL, m_EmptyScinCtrl);
	DDX_Control(pDX, IDC_SEARCH_ED, m_SearchEdit);
	DDX_Control(pDX, IDC_SCINTILLA_FORMULA_TREE, m_FormulaTree);
	DDX_Control(pDX, IDC_SCINTILLA_CALC, m_ButtonCalc);
	DDX_Control(pDX, IDC_SCINTILLA_AUTO, m_ButtonAuto);
	DDX_Control(pDX, IDC_SCINTILLA_CALC_RESULT, m_CalcResult);
  DDX_Control(pDX, IDC_SCINTILLA_CALC_LINE, m_CalcRelativeLine);
	DDX_Control(pDX, IDC_FORMULA_TAB, m_TabControl);
	DDX_Control(pDX, IDC_FUNCTIONS_TAB, m_FunctionTab);
	DDX_Control(pDX, IDC_SYMBOL_TREE, m_SymbolTree);
	DDX_Control(pDX, IDCANCEL, m_FormulaCancel);
	DDX_Control(pDX, IDC_SCINTILLA_APPLY, m_FormulaApply);
}

BEGIN_MESSAGE_MAP(CDlgFormulaScintilla, CDialog)
	// Menu items
	ON_COMMAND(ID_FORMULA_FILE_APPLY, &CDlgFormulaScintilla::OnBnClickedApply)
	ON_COMMAND(ID_FORMULA_FILE_CANCEL, &CDlgFormulaScintilla::OnCancel)
	ON_COMMAND(ID_FORMULA_EDIT_NEW, &CDlgFormulaScintilla::OnNew)
	ON_COMMAND(ID_FORMULA_EDIT_RENAME, &CDlgFormulaScintilla::OnRename)
	ON_COMMAND(ID_FORMULA_EDIT_DELETE, &CDlgFormulaScintilla::OnDelete)
	ON_COMMAND(ID_FORMULA_EDIT_CUT, &CDlgFormulaScintilla::OnEditCut)
	ON_COMMAND(ID_FORMULA_EDIT_COPY, &CDlgFormulaScintilla::OnEditCopy)
	ON_COMMAND(ID_FORMULA_EDIT_PASTE, &CDlgFormulaScintilla::OnEditPaste)
	ON_COMMAND(ID_FORMULA_EDIT_DELETE_TEXT, &CDlgFormulaScintilla::OnEditDeleteText)
	ON_COMMAND(ID_FORMULA_EDIT_UNDO, &CDlgFormulaScintilla::OnEditUndo)
	ON_COMMAND(ID_FORMULA_EDIT_REDO, &CDlgFormulaScintilla::OnEditRedo)
	ON_COMMAND(ID_FORMULA_EDIT_SELECTALL, &CDlgFormulaScintilla::OnEditSelectAll)
	ON_COMMAND(ID_FORMULA_VIEW_WORDWRAP, &CDlgFormulaScintilla::OnViewWordWrap)
	ON_COMMAND(ID_FORMULA_EDIT_HANDLIST, &CDlgFormulaScintilla::OnHandList)
	ON_COMMAND(ID_FORMULA_EDIT_FIND, &CDlgFormulaScintilla::OnFindReplaceDlg)
	ON_COMMAND(ID_FORMULA_EDIT_FIND_NEXT, &CDlgFormulaScintilla::OnFindNext)
	ON_COMMAND(ID_FORMULA_EDIT_FIND_PREV, &CDlgFormulaScintilla::OnFindPrev)

	// Help 
	ON_COMMAND(ID_HELP_HELP, &CMainFrame::OnHelp)
	ON_COMMAND(ID_HELP_OPEN_PPL, &CMainFrame::OnHelpOpenPPL)
	ON_COMMAND(ID_HELP_FORUMS, &CMainFrame::OnHelpForums)

	// Keyboard Shortcuts
	ON_COMMAND(ID_TOGGLE_BOOKMARK, &CDlgFormulaScintilla::OnToggleBookmark)
	ON_COMMAND(ID_GOTO_NEXT_BOOKMARK, &CDlgFormulaScintilla::OnNextBookmark)
	ON_COMMAND(ID_GOTO_PREV_BOOKMARK, &CDlgFormulaScintilla::OnPrevBookmark)

	// Toolbar Buttons
	ON_COMMAND(ID_FORMULA_TOOLBAR_NEW, &CDlgFormulaScintilla::OnNew)
	ON_COMMAND(ID_FORMULA_TOOLBAR_RENAME, &CDlgFormulaScintilla::OnRename)
	ON_COMMAND(ID_FORMULA_TOOLBAR_DELETE, &CDlgFormulaScintilla::OnDelete)
	ON_COMMAND(ID_FORMULA_TOOLBAR_HANDLIST, &CDlgFormulaScintilla::OnHandList)
	
	// Buttons
	ON_BN_CLICKED(IDC_SCINTILLA_APPLY, &CDlgFormulaScintilla::OnBnClickedApply)
	ON_BN_CLICKED(IDC_SCINTILLA_CALC, &CDlgFormulaScintilla::OnBnClickedCalc)
	ON_BN_CLICKED(IDCANCEL, &CDlgFormulaScintilla::OnCancel)

	ON_REGISTERED_MESSAGE( WM_FINDREPLACE, OnFindReplace )

	ON_BN_CLICKED(IDC_SCINTILLA_AUTO, &CDlgFormulaScintilla::OnBnClickedAuto)

	// WinMgr
	ON_REGISTERED_MESSAGE(WM_WINMGR, OnWinMgr)
	ON_WM_SIZE()

	// Modern UI: frameless window, own title bar, light / dark theme
	ON_WM_NCCALCSIZE()
	ON_WM_NCHITTEST()
	ON_WM_SYSCOMMAND()
	ON_WM_GETMINMAXINFO()
	ON_WM_ERASEBKGND()
	ON_WM_CTLCOLOR()
	ON_MESSAGE(modernui::WMU_THEME_CHANGED, &CDlgFormulaScintilla::OnThemeChanged)
	ON_MESSAGE(WM_SETTEXT, &CDlgFormulaScintilla::OnSetTextMessage)

	// Scintilla
	ON_NOTIFY(TVN_SELCHANGING, IDC_SCINTILLA_FORMULA_TREE, &CDlgFormulaScintilla::OnTvnSelchangingFormulaTree)
	ON_NOTIFY(TVN_SELCHANGED, IDC_SCINTILLA_FORMULA_TREE, &CDlgFormulaScintilla::OnTvnSelchangedFormulaTree)
	ON_NOTIFY(TVN_ITEMEXPANDED, IDC_SCINTILLA_FORMULA_TREE, &CDlgFormulaScintilla::OnTvnExpandedFormulaTree)
	ON_NOTIFY(NM_RCLICK, IDC_SCINTILLA_FORMULA_TREE, OnTreeContextMenu)

	ON_NOTIFY(TCN_SELCHANGE, IDC_FORMULA_TAB, OnTabSelectionChange)
	ON_NOTIFY(TCN_SELCHANGE, IDC_FUNCTIONS_TAB, OnFunctionTabSelectionChange)

	ON_NOTIFY(TVN_GETINFOTIP, IDC_SYMBOL_TREE, OnSymbolTreeTipInfo)
	ON_NOTIFY(NM_RCLICK, IDC_SYMBOL_TREE, OnTreeContextMenu)

	ON_EN_UPDATE(IDC_SEARCH_ED, OnSearchUpdate)

	// Tooltips
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTW, 0, 0xFFFF, OnToolTipText)
	ON_NOTIFY_EX_RANGE(TTN_NEEDTEXTA, 0, 0xFFFF, OnToolTipText)

	// Timer
	ON_WM_TIMER()

	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

CScintillaWnd *CDlgFormulaScintilla::SetupScintilla(CScintillaWnd *pWnd, const char *title) 
{
	if (!pWnd) 
	{
		ASSERT(title);
		pWnd = new CScintillaWnd();
		m_ScinArray.Add(CScintillaInfo(pWnd, title));

		m_FunctionTab.InsertItem(m_FunctionTab.GetItemCount(), title);
		m_FunctionTab.SetCurSel(m_FunctionTab.GetItemCount());

		CRect rect;
		m_EmptyScinCtrl.GetWindowRect(rect);
		ScreenToClient(rect);

		pWnd->Create("", WS_CHILD|WS_VISIBLE|WS_TABSTOP, rect, this, IDC_SCINCTRL+m_ScinArray.GetSize());
	}
	pWnd->Init();
	pWnd->SendMessage(SCI_SETINDENTATIONGUIDES, false, 0);
	pWnd->SendMessage(SCI_SETMODEVENTMASK, (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT), 0);
	pWnd->SetLexer(SCLEX_CPP);
	UpdateScintillaKeywords(pWnd);
	pWnd->EnableWindow(false);
	SetStyleColors(pWnd);
  SetEquiDistantFont(pWnd);  
	modernglue::ApplyControlTheme(pWnd->GetSafeHwnd());
	// Apply the current word-wrap setting (also recalculates the horizontal scroll width)
	pWnd->SetWordWrap(m_word_wrap);

	return pWnd;
}

void CDlgFormulaScintilla::UpdateScintillaKeywords(CScintillaWnd *pWnd) {
  CString keywords;
  CString filter;
  m_SearchEdit.GetWindowText(filter);
  if (filter.IsEmpty()) {
    assert(p_engine_container != NULL);
    keywords = p_engine_container->SymbolsProvided();
	pWnd->SendMessage(SCI_SETKEYWORDS, 0, (LPARAM) keywords.GetString());
  } else {
	pWnd->SendMessage(SCI_SETKEYWORDS, 0, (LPARAM) "");
	pWnd->SendMessage(SCI_SETKEYWORDS, 3, (LPARAM) filter.GetString());
  }
  pWnd->Refresh();
}

void CDlgFormulaScintilla::UpdateAllScintillaKeywords() {
  CString keywords = p_engine_container->SymbolsProvided();
  for (int iScint=0; iScint<m_ScinArray.GetSize(); iScint++) {
    m_ScinArray[iScint]._pWnd->SendMessage(SCI_SETKEYWORDS, 0, (LPARAM) keywords.GetString());
    m_ScinArray[iScint]._pWnd->Refresh();
  }
}

void CDlgFormulaScintilla::DeleteScintilla(CScintillaWnd *pWnd) 
{
	for (int i=0; i<m_ScinArray.GetSize(); i++)
	{
		if (m_ScinArray[i]._pWnd == pWnd) 
		{
			m_ScinArray.RemoveAt(i);
		}
	}
	pWnd->DestroyWindow();
	delete pWnd;
}

// CDlgFormulaScintilla message handlers
BOOL CDlgFormulaScintilla::OnInitDialog() 
{
	int			i = 0, N = 0;
	int			max_x = 0, max_y = 0;

	in_startup = true;
	CDialog::OnInitDialog();

	// Save tofit windows as current size
	m_winMgr.InitToFitSizeFromCurrent(this);	// make tofit = current size
	m_winMgr.CalcLayout(this);						    // recalc
	m_winMgr.SetWindowPositions(this);				// set positions

	// Create sizer bar window
	m_winMgrSizerBar.Create(WS_VISIBLE|WS_CHILD, this, m_winMgr, ID_SCIN_SIZERBAR);

	// Modern UI: title bar, menu bar, toolbar and status bar are drawn by
	// ModernUI, the dialog never shows a native menu, toolbar or caption.
	ModifyStyle(0, WS_CLIPCHILDREN);
	CreateModernHeader();
	modernui::Theme::RegisterWindow(m_hWnd);

	// Accelerators
	HINSTANCE hInst = AfxFindResourceHandle(MAKEINTRESOURCE(IDR_FORMULA_EDIT_ACC), RT_ACCELERATOR);
	m_hEditAccelTable = ::LoadAccelerators(hInst, MAKEINTRESOURCE(IDR_FORMULA_EDIT_ACC));
	hInst = AfxFindResourceHandle(MAKEINTRESOURCE(IDR_FORMULA_DIALOG_ACC), RT_ACCELERATOR);
	m_hDialogAccelTable = ::LoadAccelerators(hInst, MAKEINTRESOURCE(IDR_FORMULA_DIALOG_ACC));

	m_TabControl.SetPadding(CSize(14, 4));
	m_FunctionTab.SetPadding(CSize(14, 4));
	m_TabControl.InsertItem(0, "Formulas");
	m_TabControl.InsertItem(1, "Symbols");

	m_FunctionTab.InsertItem(0, "Empty");
	PopulateFormulaTree();

	SetupScintilla(&m_EmptyScinCtrl, NULL);
	m_ScinArray.Add(CScintillaInfo(&m_EmptyScinCtrl, ""));
	m_pActiveScinCtrl = &m_EmptyScinCtrl;

	// Set dialog title
	::SetWindowText(m_hWnd, "Formula Editor");

	// Set dialog icon
	HICON hIcon = LoadIcon(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_ICON1));
	this->SetIcon(hIcon, FALSE);

	// Restore window location and size, precision preference
	max_x = GetSystemMetrics(SM_CXSCREEN) - GetSystemMetrics(SM_CXICON);
	max_y = GetSystemMetrics(SM_CYSCREEN) - GetSystemMetrics(SM_CYICON);
	::SetWindowPos(m_hWnd, HWND_TOP, min(Preferences()->formula_x(), max_x), min(Preferences()->formula_y(), max_y),
	Preferences()->formula_dx(), Preferences()->formula_dy(), SWP_NOCOPYBITS);
	// Always sort UDFs
	SortUdfTree();

	HandleEnables(true);

	//  Set auto button state
	m_ButtonAuto.SetWindowText("Auto");
	m_FormulaApply.SetPrimary(true);
	m_SearchEdit.SendMessage(EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter functions...");
	ApplyModernTheme();

	// Timer to keep menu state updated
	SetTimer(MENU_UPDATE_TIMER, 200, 0);

	// Timer to keep f$debug tab updated
	// Once per second is enough
	SetTimer(DEBUG_UPDATE_TIMER, 1000, 0);

	in_startup = false;
	{
		// Position header, status bar and controls for the restored size
		CRect client;
		GetClientRect(&client);
		LayoutModern(client.Width(), client.Height());
	}

	return TRUE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return false
}

void CDlgFormulaScintilla::OnCancel() {
  if (m_dirty) {
    if (!PromptToSave()) {
      return;
    }
  }
	DestroyWindow();
}

void CDlgFormulaScintilla::RemoveSingleItemGroups()
{
	HTREEITEM hUDFChildItem = m_FormulaTree.GetChildItem(hUDFItem);
	HTREEITEM hNextLevelItem = NULL, hNextItem = NULL;
	CString fnName = 0;

	while (hUDFChildItem != NULL)
	{
		if (!m_FormulaTree.ItemHasChildren(hUDFChildItem)) 
		{
			hUDFChildItem = m_FormulaTree.GetNextSiblingItem(hUDFChildItem);
			continue;
		} else {
			m_FormulaTree.SetItemState(hUDFChildItem, TVIS_EXPANDED, TVIS_EXPANDED);
		}

		hNextItem = m_FormulaTree.GetNextSiblingItem(hUDFChildItem);
		hNextLevelItem = m_FormulaTree.GetChildItem(hUDFChildItem);
		if (hNextLevelItem != NULL && m_FormulaTree.GetNextSiblingItem(hNextLevelItem) == NULL) 
		{
			MoveTreeItem(hNextLevelItem, hUDFItem, NULL, false);
			m_FormulaTree.DeleteItem(hUDFChildItem);
		}
		hUDFChildItem = hNextItem;
	}
}

void CDlgFormulaScintilla::GetGroupName(const char *functionName, CString &groupName)
{
	groupName = functionName;
	if (groupName.Find("f$") != 0)
		groupName.Empty();
	else 
	{
		int index = groupName.Find("_");
		if (index < 2)
			groupName.Empty();
		else
			groupName = groupName.Mid(2, index-2);
	}
}

void CDlgFormulaScintilla::GroupUDFs()
{
	HTREEITEM hUDFChildItem = m_FormulaTree.GetChildItem(hUDFItem);
	HTREEITEM hNextItem = NULL;
	CString groupName = "", itemText = "";

	while (hUDFChildItem != NULL)
	{
		hNextItem = m_FormulaTree.GetNextSiblingItem(hUDFChildItem);
		if (!m_FormulaTree.ItemHasChildren(hUDFChildItem)) {
			itemText = m_FormulaTree.GetItemText(hUDFChildItem);
			GetGroupName(itemText, groupName);
			if (!groupName.IsEmpty()) {
				HTREEITEM hExistingGroup = FindUDFGroupItem(groupName);
				if (!hExistingGroup)
					hExistingGroup = m_FormulaTree.InsertItem(groupName, hUDFItem);
				MoveTreeItem(hUDFChildItem, hExistingGroup, itemText, false);
			}
		}
		hUDFChildItem = hNextItem;
	}
	RemoveSingleItemGroups();
}

void CDlgFormulaScintilla::AddFunctionToTree(HTREEITEM hParent, CString name) {
  assert(name != "");
  HTREEITEM hItem = m_FormulaTree.InsertItem(name, hParent);
  m_FormulaTree.SetItemData(hItem, (DWORD_PTR)FindScintillaWindow(name));
}

void CDlgFormulaScintilla::AddStandardFunctionsToTree(
    HTREEITEM hParent, 
    int first, 
    int last) {
  assert(first >= 0);
  assert(last < k_number_of_standard_functions);
  for (int i=first; i<=last; ++i) {
    AddFunctionToTree(hParent, k_standard_function_names[i]);
  }
}
	
void CDlgFormulaScintilla::PopulateFormulaTree() {
  m_FormulaTree.DeleteAllItems();

  CString filter;
  m_SearchEdit.GetWindowText(filter);
  // Setup the tree
  HTREEITEM	parent = NULL, hItem;
  COHScriptObject *p_OH_script_object = NULL;

  assert(m_standard_headings.GetSize() > 0);
  if (p_function_collection->IsOpenPPLProfile()) {
    // Either use autoplayer-functions (default) or OpenPPL
    // but not both, because they are incompatible
    m_standard_headings.SetAt(0, "OpenPPL Functions");
  } else {
    m_standard_headings.SetAt(0, "Autoplayer Functions");
  }

  for (int j=0; j<m_standard_headings.GetSize(); j++) {
    if (m_standard_headings[j].IsEmpty()) {
	    parent = NULL;
    } else {
	    parent = m_FormulaTree.InsertItem(m_standard_headings[j]);
	    m_FormulaTree.SetItemState(parent, TVIS_BOLD, TVIS_BOLD);
    }
    switch(j) {
      case 0: 
        if (p_function_collection->IsOpenPPLProfile()) {
          AddFunctionToTree(parent, k_standard_function_names[k_autoplayer_function_beep]);
          // OpenPPL-functions
          for (int i=kBetroundPreflop; i<=kBetroundRiver; ++i) {
            AddFunctionToTree(parent, k_OpenPPL_function_names[i]);
          }
        } else {
          // Autoplayer functions
          AddStandardFunctionsToTree(parent, 
          k_autoplayer_function_beep, k_autoplayer_function_fold);
        }
        break;
      case 1:
        // Standard functions, including "notes"
        AddFunctionToTree(parent, "notes");
        AddStandardFunctionsToTree(parent, 
          k_standard_function_prefold, k_standard_function_shoot_replay_frame);
        break;
      case 2:
        // Hopper Functions
        AddStandardFunctionsToTree(parent,
          k_hopper_function_sitin, k_hopper_function_select_formula_file);
        break;
      case 3:
        // Ini functions
        AddStandardFunctionsToTree(parent,
          k_init_on_startup, k_init_on_heartbeat);
        break;
      case 4:
        // PrWin functions
        AddStandardFunctionsToTree(parent,
          k_prwin_number_of_opponents, k_prwin_wontplay);
        break;
      case 5:
        // ICM functions
        AddStandardFunctionsToTree(parent,
          k_icm_prize1, k_icm_prize9);
        break;
      case 6:
        // Debug functions
        AddFunctionToTree(parent, "f$debug");
        AddFunctionToTree(parent, "f$test");
        break;
    }
  }
  _subtree_handlists = m_FormulaTree.InsertItem("Hand Lists");
  parent = _subtree_handlists;
  m_FormulaTree.SetItemState(parent, TVIS_BOLD, TVIS_BOLD);

  p_OH_script_object = p_function_collection->GetFirst();
  while (p_OH_script_object != NULL) {
    if (p_OH_script_object->IsList() && !p_OH_script_object->IsReadOnlyLibrarySymbol()) {
      hItem = m_FormulaTree.InsertItem(p_OH_script_object->name(), parent);
      m_FormulaTree.SetItemData(hItem, (DWORD_PTR)FindScintillaWindow(
        p_OH_script_object->name()));
    }
    p_OH_script_object = p_function_collection->GetNext();
  }
 
  _subtree_user_defined_functions = m_FormulaTree.InsertItem("User Defined Functions");
  hUDFItem = parent = _subtree_user_defined_functions;
  p_OH_script_object = p_function_collection->GetFirst();
  while (p_OH_script_object != NULL) {
    m_FormulaTree.SetItemState(parent, TVIS_BOLD, TVIS_BOLD);
    if (p_OH_script_object->IsUserDefinedFunction() && !p_OH_script_object->IsReadOnlyLibrarySymbol()) {
      AddFunctionToTree(parent, p_OH_script_object->name());
    }
    p_OH_script_object = p_function_collection->GetNext();
  }
  GroupUDFs();
}

BOOL CDlgFormulaScintilla::PreTranslateMessage(MSG* pMsg)
{
	if (pMsg->message >= WM_KEYFIRST && pMsg->message <= WM_KEYLAST)
	{
		if (m_hDialogAccelTable != NULL &&  ::TranslateAccelerator(m_hWnd, m_hDialogAccelTable, pMsg))
			return TRUE;
		if (m_pActiveScinCtrl && GetFocus() == m_pActiveScinCtrl) 
		{
			if (m_hEditAccelTable != NULL &&  ::TranslateAccelerator(m_hWnd, m_hEditAccelTable, pMsg))
				return TRUE;
		}
	}
	return CDialog::PreTranslateMessage(pMsg);
}

void CDlgFormulaScintilla::OnTvnSelchangingFormulaTree(NMHDR *pNMHDR, LRESULT *pResult) 
{
	LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);
	*pResult = 0;
}

CScintillaWnd *CDlgFormulaScintilla::FindScintillaWindow(const char *name)
{
	for (int i=0; i<m_ScinArray.GetSize(); i++)
	{
		if (!m_ScinArray.GetAt(i)._name.Compare(name))
			return m_ScinArray.GetAt(i)._pWnd;
	}
	return NULL;
}

HTREEITEM CDlgFormulaScintilla::FindFormulaWithWindow(const CScintillaWnd *pWnd, HTREEITEM hFirstItem)
{
	HTREEITEM hItem = hFirstItem;
	while (hItem) {
		if (m_FormulaTree.GetChildItem(hItem)) {
			HTREEITEM hFound = FindFormulaWithWindow(pWnd, m_FormulaTree.GetChildItem(hItem));
			if (hFound)
				return hFound;
		} else if (pWnd == reinterpret_cast<CScintillaWnd *>(m_FormulaTree.GetItemData(hItem)))
			return hItem;
		hItem = m_FormulaTree.GetNextSiblingItem(hItem);
	}

	return NULL;
}

void CDlgFormulaScintilla::OnFunctionTabSelectionChange(NMHDR *pNMHDR, LRESULT *pResult)
{
	int iSel = m_FunctionTab.GetCurSel();
	HTREEITEM hItem = FindFormulaWithWindow(m_ScinArray[iSel]._pWnd, m_FormulaTree.GetRootItem());
	ASSERT(hItem != NULL);
	if (hItem)
		m_FormulaTree.SelectItem(hItem);
}

void CDlgFormulaScintilla::OnTabSelectionChange(NMHDR *pNMHDR, LRESULT *pResult)
{
	bool bFormulaShowing = (m_TabControl.GetCurSel() == 0);

	if (!bFormulaShowing)
		PopulateSymbols();

	m_FormulaTree.ShowWindow(bFormulaShowing ? SW_SHOW : SW_HIDE);
	m_SymbolTree.ShowWindow(!bFormulaShowing ? SW_SHOW : SW_HIDE);
	m_FormulaTree.EnableWindow(bFormulaShowing);
	m_SymbolTree.EnableWindow(!bFormulaShowing);

	HandleEnables(true);
}

void CDlgFormulaScintilla::OnSymbolTreeTipInfo(NMHDR *pNMHDR, LRESULT *pResult)
{
	LPNMTVGETINFOTIP lpGetInfoTip = (LPNMTVGETINFOTIP)pNMHDR;

	if (lpGetInfoTip && lpGetInfoTip->hItem)
	{
		toolTipData* toolTip = (toolTipData*)m_SymbolTree.GetItemData(lpGetInfoTip->hItem);

		if (toolTip && toolTip->description)
			strcpy_s(lpGetInfoTip->pszText, lpGetInfoTip->cchTextMax, *toolTip->description);
	}

	*pResult = 0;
}

void CDlgFormulaScintilla::OnTreeContextMenu(NMHDR *pNMHDR, LRESULT *pResult)
{
	CPoint cursorPos, clientPos;
	cursorPos.x= GetCurrentMessage()->pt.x;
	cursorPos.y= GetCurrentMessage()->pt.y;
	clientPos = cursorPos;
	CTreeCtrl *theTree = (CTreeCtrl*)theTree->FromHandle(pNMHDR->hwndFrom);
	theTree->ScreenToClient(&clientPos);
	HTREEITEM hItem = theTree->HitTest(clientPos);

	if (hItem != NULL && theTree->GetChildItem(hItem) == NULL) 
	{
		modernui::Menu contextMenu;
		int ret = 0;
		if (modernui::LoadPopupFromResource(AfxGetInstanceHandle(), IDR_FORMULA_SYMBOL_MENU, &contextMenu)) {
			for (size_t k = 0; k < contextMenu.items.size(); ++k) {
				if (!contextMenu.items[k].separator && contextMenu.items[k].id == ID_FORMULA_SYMBOL_COPY)
					contextMenu.items[k].icon = modernui::Icon::Copy;
			}
			POINT screen_point = { cursorPos.x, cursorPos.y };
			ret = modernui::TrackContextMenu(m_hWnd, contextMenu, screen_point);
		}
		if (ret == ID_FORMULA_SYMBOL_COPY)
		{
			if (OpenClipboard() && EmptyClipboard()) 
			{
				CString symbol;
				symbol = theTree->GetItemText(hItem);

				HANDLE hMem = ::GlobalAlloc(GMEM_MOVEABLE|GMEM_DDESHARE , symbol.GetLength()+1);
				if (hMem) 
				{
					LPSTR lpStr = (LPSTR)::GlobalLock(hMem);

					strcpy_s(lpStr, symbol.GetLength() + 1, symbol.GetString());
					::GlobalUnlock(hMem);

					if ( ::SetClipboardData( CF_TEXT, hMem ) != NULL )
						CloseClipboard();
				} 
				else
					CloseClipboard();
			}
		}
	}


	*pResult = 0;
}

void CDlgFormulaScintilla::ReEnableLineNumbersUglyWorkaroundForHugeShankyMonkeyStyleFormulas() {
  // The formula editor shows line-numbers per default
  // only up to 3 digits, but some people need to create single functions
  // with tens of thousands lines of code.
  // We didn~t manage to handle such large line/numbers correctly,
  // but disableing and then enabling line/numbers again
  // seems to do the job.
  // http://www.maxinmontreal.com/forums/viewtopic.php?f=111&t=18213
  m_pActiveScinCtrl->SetDisplayLinenumbers(false);
  m_pActiveScinCtrl->SetDisplayLinenumbers(true);
}

void CDlgFormulaScintilla::SelectFunctionTab(CScintillaWnd *pCurScin) {
	int iSel = kNotFound;
	for (int iWnd=0; iWnd<m_ScinArray.GetSize(); iWnd++) 	{
		if (m_ScinArray[iWnd]._pWnd == pCurScin) {
			iSel = iWnd;
			break;
		}
	}
	if (iSel != kNotFound) {
		m_FunctionTab.SetCurSel(iSel);
    ReEnableLineNumbersUglyWorkaroundForHugeShankyMonkeyStyleFormulas();    
	}
}

void CDlgFormulaScintilla::OnTvnExpandedFormulaTree(NMHDR *pNMHDR, LRESULT *pResult) 
{
	SaveSettingsToRegistry();
}

void CDlgFormulaScintilla::SetExtendedWindowTitle(CString additional_information)
{
	CString new_title = CString("Formula Editor [") + CString(VERSION_TEXT) + CString("]");
	if (additional_information != "")
	{
		new_title += " ";
		new_title += additional_information;
	}
	SetWindowText(new_title);
}

void CDlgFormulaScintilla::OnTvnSelchangedFormulaTree(NMHDR *pNMHDR, LRESULT *pResult) {
  LPNMTREEVIEW pNMTreeView = reinterpret_cast<LPNMTREEVIEW>(pNMHDR);


  ok_to_update_debug = false;

  CString s = m_FormulaTree.GetItemText(m_FormulaTree.GetSelectedItem());
  StopAutoButton();

  if ((m_FormulaTree.GetParentItem(m_FormulaTree.GetSelectedItem()) == NULL && s != "notes") 
      || m_FormulaTree.ItemHasChildren(m_FormulaTree.GetSelectedItem())) {
    // A root item was selected or a UDF group item
    FormerShowEnableHideCodeClone(&m_EmptyScinCtrl);
  } else {
    // A child item was selected
    // (OpenPPL-functions don't get shown in the editor)
	if (COHScriptObject::IsNotes(s)
        || COHScriptObject::IsFunction(s)
        || COHScriptObject::IsList(s)) {
      SetExtendedWindowTitle(s);
      COHScriptObject *p_function_or_list = p_function_collection->LookUp(s);
      if (p_function_or_list != NULL) {
        CScintillaWnd *pCurScin = reinterpret_cast<CScintillaWnd *>(m_FormulaTree.GetItemData(m_FormulaTree.GetSelectedItem()));
        if (!pCurScin) 
        {
          pCurScin = SetupScintilla(NULL, p_function_or_list->name()); 
          m_FormulaTree.SetItemData(m_FormulaTree.GetSelectedItem(), (DWORD_PTR)pCurScin);

          pCurScin->SendMessage(SCI_SETMODEVENTMASK, 0, 0);
          pCurScin->SetText(p_function_or_list->function_text());
          pCurScin->SendMessage(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT, 0);
          pCurScin->SendMessage(SCI_EMPTYUNDOBUFFER, 0, 0);
        }
        ASSERT(pCurScin != NULL);
        FormerShowEnableHideCodeClone(pCurScin);
        SelectFunctionTab(pCurScin);
        m_current_edit = p_function_or_list->name();
	  } 
    }
  }
  HandleEnables(true);
  *pResult = 0;
}

HTREEITEM CDlgFormulaScintilla::FindUDFGroupItem(const char *groupName)
{
	CString tempString = "";

	HTREEITEM searchItem = m_FormulaTree.GetChildItem(hUDFItem);
	while (searchItem) 
	{
		if (m_FormulaTree.ItemHasChildren(searchItem)) 
		{
			tempString = m_FormulaTree.GetItemText(searchItem);
			if (!tempString.Compare(groupName))
				return searchItem;
		}
		searchItem = m_FormulaTree.GetNextSiblingItem(searchItem);
	}

	return NULL;
}

HTREEITEM CDlgFormulaScintilla::FindUDFStartingItem(const char *groupName)
{
	CString searchString = "", tempString = "";

	searchString.Format("f$%s_", groupName);
	HTREEITEM searchItem = m_FormulaTree.GetChildItem(hUDFItem);
	
	while (searchItem) 
	{
		if (!m_FormulaTree.ItemHasChildren(searchItem)) 
		{
			tempString = m_FormulaTree.GetItemText(searchItem);

			if (tempString.Find(searchString) == 0)
				return searchItem; 
		}
		searchItem = m_FormulaTree.GetNextSiblingItem(searchItem);
	}

	return NULL;
}

HTREEITEM CDlgFormulaScintilla::MoveTreeItem(HTREEITEM hItem, HTREEITEM hNewParent, const char *name, bool bSelect)
{
	HTREEITEM hMovedItem = NULL;

	if (name)
		hMovedItem = m_FormulaTree.InsertItem(name, hNewParent);
	else 
	{
		CString itemName;
		itemName = m_FormulaTree.GetItemText(hItem);
		hMovedItem = m_FormulaTree.InsertItem(itemName, hNewParent);
	}

	m_FormulaTree.SetItemData(hMovedItem, m_FormulaTree.GetItemData(hItem));
	m_FormulaTree.DeleteItem(hItem);

	if (bSelect)
		m_FormulaTree.SelectItem(hMovedItem);

	return hMovedItem;
}

void CDlgFormulaScintilla::OnNew() {
  CDlgNew newdlg;
  HTREEITEM	p = NULL, newhtitem = NULL;

  StopAutoButton();
  CString s = m_FormulaTree.GetItemText(m_FormulaTree.GetSelectedItem());

  if (s == "Hand Lists" 
	  || (s.Find("list") != kNotFound && s.Left(2) != "f$")){
    newdlg.is_function = false;
  } else {  
    newdlg.is_function = true;
  }
  if (newdlg.DoModal() != IDOK) return;
  if (newdlg.CSnewname == "f$call") {
    // Set a non-empty-function -text
    // Must contain at least two spaces, as "empty" functions contain 1 space
    // as the editor does not work with completely function-texts.
    p_function_collection->LookUp("f$call")->SetText("  ");
    PopulateFormulaTree();
    return;
  }
  if (p_function_collection->Exists(newdlg.CSnewname)) {
    MessageBox_Interactive("Cannot proceed as function or list already exists", "Error", 0);
    return;
  }
  if (newdlg.is_function == false) {
    // Create new list
    // It will be released later by the function collection
    COHScriptList *p_new_list = new COHScriptList(newdlg.CSnewname, "");
    // Add it to working set CArray
    p_function_collection->Add((COHScriptObject*)p_new_list);
    // Add to tree
    p = m_FormulaTree.GetParentItem(m_FormulaTree.GetSelectedItem());
    if (p == NULL) { 
      // Selected item is a parent item  
      newhtitem = m_FormulaTree.InsertItem(newdlg.CSnewname, m_FormulaTree.GetSelectedItem());
    } else {
      newhtitem = m_FormulaTree.InsertItem(newdlg.CSnewname, p);
    }
  } else {
    // The added functions stays in the collection 
    // until a new profile gets loaded, until it gets overwritten]
    // or until the entire collection gets released
    CFunction *p_new_function = new CFunction(newdlg.CSnewname, "");
    // Add it to working set CArray
    p_function_collection->Add((COHScriptObject*)p_new_function);
    // Add to tree
    HTREEITEM hNewParent = hUDFItem;
    // !! Candidate for refactoring, probably duplicate functionality
    // !! Formula grouping
    CString tempString;
    CString groupName;
    GetGroupName(newdlg.CSnewname, groupName);
    if (!groupName.IsEmpty()) 
    {
	    // Does a group already exist?
	    HTREEITEM hExistingGroup = FindUDFGroupItem(groupName);
      if (hExistingGroup) {
        hNewParent = hExistingGroup;
      } else {
		    // If a group does not exist, is there another UDF to group together?
		    HTREEITEM matchingItem = FindUDFStartingItem(groupName);
		    if (matchingItem) 
		    {
			    hNewParent = m_FormulaTree.InsertItem(groupName, hUDFItem);
			    MoveTreeItem(matchingItem, hNewParent, NULL, false);
		    }
	    }
    }
    newhtitem = m_FormulaTree.InsertItem(newdlg.CSnewname, hNewParent);
    SortUdfTree();
  }

  UpdateAllScintillaKeywords();

  // Select newly inserted item
  m_FormulaTree.SelectItem(newhtitem);
  m_FormulaTree.SetFocus();
  m_dirty = true;
  HandleEnables(true);
}

void CDlgFormulaScintilla::OnRename() {
  CDlgRename rendlg;
  char str[MAX_WINDOW_TITLE] = {0};

  StopAutoButton();
  CString s = m_FormulaTree.GetItemText(m_FormulaTree.GetSelectedItem());
  strcpy_s(str, MAX_WINDOW_TITLE, s.GetString());
  rendlg.CSoldname = s;
  if (rendlg.DoModal() != IDOK) return;
  if (!p_function_collection->Rename(rendlg.CSoldname, rendlg.CSnewname)) return;

  // Rename the tab
  for (int i=0; i<m_ScinArray.GetSize(); ++i) {
    CString name = m_ScinArray.GetAt(i)._name;
    if (name == s) {
      m_ScinArray.GetAt(i)._name = s;
      break;
    }
  }
    
  UpdateAllScintillaKeywords();
  HTREEITEM hSelectedItem = m_FormulaTree.GetSelectedItem();
  m_FormulaTree.SetItemText(hSelectedItem, rendlg.CSnewname);
    
  PopulateFormulaTree();
  SortUdfTree();
  m_FormulaTree.Expand(_subtree_user_defined_functions, TVE_EXPAND); 
  SetWindowText("Formula - " + rendlg.CSnewname);

  //m_FormulaTree.UpdateDialogControls(); 
  m_FormulaTree.UpdateWindow();

  m_FormulaTree.SelectItem(hSelectedItem);
  m_FormulaTree.SetFocus();
  m_dirty = true;
  HandleEnables(true);
}


void CDlgFormulaScintilla::DeleteFormerParentItemIfEmpty(HTREEITEM sibbling) {
  HTREEITEM oldParentItem = m_FormulaTree.GetParentItem(sibbling);
  HTREEITEM hOldSiblingItem = m_FormulaTree.GetChildItem(oldParentItem);
  if (hOldSiblingItem && m_FormulaTree.GetNextSiblingItem(hOldSiblingItem) == NULL) {
    if ((oldParentItem != hUDFItem) && (oldParentItem != _subtree_handlists)) {
      // Delete empty sub-groups, but never delete the main groups 
      // for UDFs and hand lists
      m_FormulaTree.DeleteItem(oldParentItem); 
    }
  }
}

void CDlgFormulaScintilla::CloseTabOnDelete(CString name) {
  for (int i=0; i<m_ScinArray.GetSize(); ++i) {
    if (name == m_ScinArray.GetAt(i)._name) {
      m_ScinArray.GetAt(i)._pWnd->CloseWindow();
      delete m_ScinArray.GetAt(i)._pWnd;
      m_ScinArray.RemoveAt(i);
      break;
    }
  }
}

void CDlgFormulaScintilla::OnDelete() {
  if (IDYES != MessageBox_Interactive(
      "REALLY delete \"" 
      + m_FormulaTree.GetItemText(m_FormulaTree.GetSelectedItem()) 
      + "\" ?", 
      "Confirm Delete", MB_YESNO | MB_ICONWARNING)) {
    HandleEnables(true);
    return;
  }
  // Delete a UDF or list
  HTREEITEM h_item = m_FormulaTree.GetSelectedItem();
  CString name = m_FormulaTree.GetItemText(h_item);
  CloseTabOnDelete(name); 
  p_function_collection->Delete(name);
  m_dirty = true;
  // Clear the tab
  // The rest of the code is a partial clone 
  // to FormerShowEnableHideCodeClone().
  // However refactoring did not work and we got crashes.
  // So we are back to the working version (1.7.0).
  m_FormulaTree.SetFocus();
	SetWindowText("Formula - ");
	if (m_pActiveScinCtrl) {
		m_pActiveScinCtrl->ShowWindow(SW_HIDE);
		m_pActiveScinCtrl->EnableWindow(false);
	}
	m_pActiveScinCtrl = &m_EmptyScinCtrl;
	m_pActiveScinCtrl->ShowWindow(SW_SHOW);
	m_pActiveScinCtrl->EnableWindow(false);
	// Update the tree
	m_FormulaTree.DeleteItem(h_item );
	m_dirty = true;
  HandleEnables(true);
  UpdateAllScintillaKeywords();
}

void CDlgFormulaScintilla::OnToggleBookmark()
{
	if (!m_pActiveScinCtrl)
		return;

	int iLine = m_pActiveScinCtrl->GetCurrentLine();

	if (m_pActiveScinCtrl->HasBookmark(iLine))
		m_pActiveScinCtrl->DeleteBookmark(iLine);
	else 
	{
		m_pActiveScinCtrl->AddBookmark(iLine);
	}
}

void CDlgFormulaScintilla::OnNextBookmark()
{
	if (!m_pActiveScinCtrl)
		return;

	m_pActiveScinCtrl->FindNextBookmark();
}

void CDlgFormulaScintilla::OnPrevBookmark()
{
	if (!m_pActiveScinCtrl)
		return;

	m_pActiveScinCtrl->FindPreviousBookmark();
}

void CDlgFormulaScintilla::CloseFindReplaceDialog()
{
	if (m_pFRDlg && !m_pFRDlg->IsTerminating() && ::IsWindow(m_pFRDlg->GetSafeHwnd()))
		m_pFRDlg->DestroyWindow();

	m_pFRDlg = NULL;
}

void CDlgFormulaScintilla::DoFind(bool DirDown)
{
	m_pActiveScinCtrl->SetSearchflags((m_FindMatchCase ? SCFIND_MATCHCASE : 0)|
									  (m_FindWholeWord ? SCFIND_WORDSTART : 0));

	// Note: I have confirmed that the TextToFind struct for the search text is incorrectly
	// set to a non-const when in actuality the functions using that variable take a const pointer
	// I did not want to change a Scintilla header files (so I did a dirty dirty cast)
	if (DirDown)
		m_pActiveScinCtrl->SearchForward((char *)m_FindLastSearch.GetString());
	else
		m_pActiveScinCtrl->SearchBackward((char *)m_FindLastSearch.GetString());
}

LRESULT CDlgFormulaScintilla::OnFindReplace(WPARAM, LPARAM lParam)
{
	LPFINDREPLACE lpFindReplace = (LPFINDREPLACE) lParam;

	if (lpFindReplace && m_pActiveScinCtrl) 
	{
		if (lpFindReplace->Flags & FR_DIALOGTERM) 
		{
			CloseFindReplaceDialog();
		} 
		else if (lpFindReplace->Flags & FR_FINDNEXT) 
		{
			m_FindLastSearch = lpFindReplace->lpstrFindWhat;
			m_FindMatchCase = (lpFindReplace->Flags & FR_MATCHCASE ? true : false);
			m_FindWholeWord = (lpFindReplace->Flags & FR_WHOLEWORD ? true : false);
			DoFind((lpFindReplace->Flags & FR_DOWN ? true : false));
			HandleEnables(true);
		} 
		else if (lpFindReplace->Flags & FR_REPLACE) 
		{
		} 
		else if (lpFindReplace->Flags & FR_REPLACEALL) 
		{
		}
	}

	return 0;
}

void CDlgFormulaScintilla::OnFindNext()
{
	DoFind(true);
}

void CDlgFormulaScintilla::OnFindPrev()
{
	DoFind(false);
}

void CDlgFormulaScintilla::OnFindReplaceDlg()
{
	if ( NULL == m_pFRDlg )
	{
		m_pFRDlg = new CFindReplaceDialog();  // Must be created on the heap
		m_pFRDlg->Create(TRUE, "", "", FR_DOWN, this);
		m_pFRDlg->m_fr.lStructSize = sizeof(FINDREPLACE);
		m_pFRDlg->m_fr.hwndOwner = this->m_hWnd;
	}
}

CString CDlgFormulaScintilla::ExtractCommentFromHandList(CString HandListAsString)
{
	int		length = HandListAsString.GetLength();
	bool	inside_comment = false;
	CString	comment = "";

	for (int i=0; i<length; i++)
	{
		char current_char = HandListAsString.GetAt(i);
		// We check only for a single slash as beginning of a comment,
		// but that should be ok for handlists (not for formulas).
		if (current_char == '/')
		{
			inside_comment = true;
		}
		if (inside_comment)
		{
			comment += current_char;
		}
		if (current_char == '\n')
		{
			// Comments end automatically at the end of the line
			inside_comment = false;
		}
	}
	return comment;
}

void CDlgFormulaScintilla::OnHandList()  {
  CDlgHandList		myDialog;
  int				list_index = 0, i = 0, j = 0;
  CString			token = "", hand = "", newstring = "";

  CString s = m_FormulaTree.GetItemText(m_FormulaTree.GetSelectedItem());
  COHScriptList *p_handlist = (COHScriptList*)p_function_collection->LookUp(s);
  if (p_handlist == NULL)  return;
  // Window title and static text content
  myDialog.p_list = p_handlist;

  // Start dialog
  if (myDialog.DoModal() == IDOK) {
    CString old_comment = ExtractCommentFromHandList(p_handlist->function_text());
    CString new_handlist_without_comment = p_handlist->function_text();
    CString new_handlist_with_comment = old_comment + new_handlist_without_comment;
    p_handlist->SetText(new_handlist_with_comment, false); 
    p_handlist->Parse();

    // update scintilla window
    CScintillaWnd *pCurScin = reinterpret_cast<CScintillaWnd *>(m_FormulaTree.GetItemData(m_FormulaTree.GetSelectedItem()));
    ASSERT(pCurScin != NULL);
    if (pCurScin) {
	  pCurScin->SetText(new_handlist_with_comment);
    }

    m_dirty = true;
  }
  HandleEnables(true);
}

BOOL CDlgFormulaScintilla::DestroyWindow()
{
	COpenHoldemDoc		*pDoc = COpenHoldemDoc::GetDocument();

	StopAutoButton();
	SaveSettingsToRegistry();
	CloseFindReplaceDialog();
	
	// Uncheck formula button on main toolbar
	p_flags_toolbar->CheckButton(ID_MAIN_TOOLBAR_FORMULA, false);
	modernui::Theme::UnregisterWindow(m_hWnd);

	return CDialog::DestroyWindow();
}

void CDlgFormulaScintilla::PostNcDestroy()
{
	CDialog::PostNcDestroy();
	delete this;
	m_formulaScintillaDlg	=	NULL;
}

void CDlgFormulaScintilla::ClearCalcResult() {
  m_CalcResult.SetWindowText("");
  m_CalcRelativeLine.SetWindowText("");
}

BOOL CDlgFormulaScintilla::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) 
{
	NMHDR *phDR;
	phDR = (NMHDR*)lParam;
	CString s = "";

	if (in_startup)  
	{
		return true;
	}

	// does notification come from my scintilla control?
	if (phDR != NULL && phDR->hwndFrom == m_pActiveScinCtrl->m_hWnd) 
	{
		SCNotification *pMsg = (SCNotification*)lParam;
		switch (phDR->code) 
		{
		case SCN_STYLENEEDED:
			break;
		case SCN_CHARADDED:
			break;
		case SCN_SAVEPOINTREACHED:
			break;
		case SCN_SAVEPOINTLEFT:
			// called when the document is changed - mark document modified
			break;
		case SCN_MODIFYATTEMPTRO:
			break;
		case SCN_KEY:
			break;
		case SCN_DOUBLECLICK:
			break;
		case SCN_UPDATEUI:
			// called when something changes and we want to show new indicator state or brace matching

			// update status bar
			s.Format("Line: %d, Col: %d", m_pActiveScinCtrl->GetCurrentLine(), m_pActiveScinCtrl->GetCurrentColumn());
			m_status_position = s;
			UpdateStatusBar();

			m_pActiveScinCtrl->UpdateUI();
			break;
		case SCN_MODIFIED:
			// Keep the horizontal scrollbar in sync with the longest line
			if (pMsg->modificationType & SC_MOD_INSERTTEXT) {
				m_pActiveScinCtrl->UpdateScrollWidthForRange(pMsg->position, pMsg->length);
			}
			StopAutoButton();
      ClearCalcResult();
			m_dirty = true;
			HandleEnables(true);

			break;
		case SCN_MACRORECORD:
			break;
		case SCN_MARGINCLICK:
			// user clicked margin - try folding action
			m_pActiveScinCtrl->DoDefaultFolding(pMsg->margin, pMsg->position);
			break;
		case SCN_NEEDSHOWN:
			break;
		case SCN_PAINTED:
			break;
		case SCN_USERLISTSELECTION:
			break;
		case SCN_URIDROPPED:
			break;
		case SCN_DWELLSTART:
			break;
		case SCN_DWELLEND:
			break;
		}
		return TRUE; // we processed the message
	}

	return CDialog::OnNotify(wParam, lParam, pResult);
}

BOOL CDlgFormulaScintilla::OnToolTipText(UINT, NMHDR* pNMHDR, LRESULT* pResult) 
{
	// allow top level routing frame to handle the message
	if (GetRoutingFrame() != NULL)
		return false;

	// need to handle both ANSI and UNICODE versions of the message
	TOOLTIPTEXTA* pTTTA = (TOOLTIPTEXTA*)pNMHDR;
	TOOLTIPTEXTW* pTTTW = (TOOLTIPTEXTW*)pNMHDR;
	TCHAR szFullText[256];
	CString strTipText;
	UINT_PTR nID = (UINT) pNMHDR->idFrom;
	if (pNMHDR->code == TTN_NEEDTEXTA && (pTTTA->uFlags & TTF_IDISHWND) ||
			pNMHDR->code == TTN_NEEDTEXTW && (pTTTW->uFlags & TTF_IDISHWND)) 
	{
		// idFrom is actually the HWND of the tool
		nID = ::GetDlgCtrlID((HWND)nID);
	}

	if (nID != 0) 
	{ // will be zero on a separator
		AfxLoadString(nID, szFullText);
		strTipText=szFullText;
	}

	if (pNMHDR->code == TTN_NEEDTEXTA)
		lstrcpyn(pTTTA->szText, strTipText, sizeof(pTTTA->szText));
	else
		_mbstowcsz(pTTTW->szText, strTipText, sizeof(pTTTW->szText));

	*pResult = 0;

	// bring the tooltip window above other popup windows
	::SetWindowPos(pNMHDR->hwndFrom, HWND_TOP, 0, 0, 0, 0,SWP_NOACTIVATE|
				   SWP_NOSIZE|SWP_NOMOVE|SWP_NOOWNERZORDER);

	return true;
}

LRESULT CDlgFormulaScintilla::OnWinMgr(WPARAM wp, LPARAM lp) 
{
	ASSERT(lp);
	NMWINMGR& nmw = *(NMWINMGR*)lp;

	if (nmw.code==NMWINMGR::GET_SIZEINFO) 
	{
		if (wp==(WORD)GetDlgCtrlID()) 
		{
			// Parent frame is requesting my size info. Report min size.
			m_winMgr.GetMinMaxInfo(this, nmw.sizeinfo);
			return true; // handled--important!
		}
	}

	else if (nmw.code==NMWINMGR::SIZEBAR_MOVED) 
	{
		// User moved a sizer bar: call WinMgr to do it!
		m_winMgr.MoveRect(wp, nmw.sizebar.ptMoved, this);
		m_winMgr.SetWindowPositions(this);

		ResizeScintillaWindows();

		if (::IsWindow(m_FormulaTree.GetSafeHwnd()))
		{
			CRect rc;
			m_FormulaTree.GetWindowRect(rc);
			ScreenToClient(rc);
			m_SymbolTree.SetWindowPos(NULL, rc.left,rc.top,rc.Width(),rc.Height(), SWP_NOZORDER);
		}

		return true;
	}

	return false; // not handled
}




void CDlgFormulaScintilla::ResizeScintillaWindows()
{
	if (::IsWindow(m_EmptyScinCtrl.GetSafeHwnd())) 
	{
		CRect rc;
		m_EmptyScinCtrl.GetWindowRect(rc);
		ScreenToClient(rc);

		HDWP hdwp = ::BeginDeferWindowPos(m_ScinArray.GetSize()-1);

		for (int i=1; i<m_ScinArray.GetSize(); i++)
		{
			::DeferWindowPos(hdwp, m_ScinArray[i]._pWnd->GetSafeHwnd(), NULL, rc.left,rc.top,rc.Width(),rc.Height(), SWP_NOZORDER);
		}
		::EndDeferWindowPos(hdwp);
	}
}

void CDlgFormulaScintilla::OnSize(UINT nType, int cx, int cy) 
{
	CDialog::OnSize(nType, cx, cy);
	if (nType == SIZE_MINIMIZED) {
		return;
	}
	LayoutModern(cx, cy);
}

void CDlgFormulaScintilla::LayoutModern(int cx, int cy)
{
	if (m_in_layout) {
		return;   // moving the child windows must never re-enter the layout
	}
	m_in_layout = true;
	// Space for our own header (title bar, menu bar, toolbar) on top
	// and for the status bar at the bottom.
	const float scale = modernui::DpiScale(m_hWnd);
	const int header_height = (m_header != NULL) ? m_header->Height(scale) : 0;
	const int status_height = (m_status != NULL) ? modernui::StatusbarHeight(scale) : 0;
	if (m_header != NULL) {
		::MoveWindow(m_header->hwnd(), 0, 0, cx, header_height, TRUE);
	}
	if (m_status != NULL) {
		::MoveWindow(m_status->hwnd(), 0, cy - status_height, cx, status_height, TRUE);
	}

	m_winMgr.CalcLayout(0, header_height, cx, cy - status_height, this);
	m_winMgr.SetWindowPositions(this);

	ResizeScintillaWindows();

	if (::IsWindow(m_FormulaTree.GetSafeHwnd()))
	{
		CRect rc;
		m_FormulaTree.GetWindowRect(rc);
		ScreenToClient(rc);
		m_SymbolTree.SetWindowPos(NULL, rc.left,rc.top,rc.Width(),rc.Height(), SWP_NOZORDER);
	}
	m_in_layout = false;
}

void CDlgFormulaScintilla::OnBnClickedCalc() {
  CString					Cstr = "", s = "";
  double					ret = 0.;
  std::string			str = "";
  char					  format[50] = {0};

  ClearCalcResult();
  OnBnClickedApply();
  p_function_collection->ClearCache();
  
  if (!p_function_collection->BotLogicCorrectlyParsed()) {
    s.Format("There are syntax errors in one or more formulas that are\n");
    s.Append("preventing calculation.\n");
    s.Append("These errors need to be corrected before the 'Calc'\n");
    s.Append("button can be used.");
    MessageBox_Error_Warning(s, "PARSE ERROR(s)");
    return;
  }

  // f$debug is a special case, handle it first
  if (m_current_edit == "f$debug") {
    UpdateDebugAuto();
  } 
  // Processing for any other formula
  else {
    // Execute the currently selected formula
    p_function_collection->Dump();
    ret = p_function_collection->Evaluate(m_current_edit);
    int line = p_autoplayer_trace->GetLastEvaluatedRelativeLineNumber();
    sprintf_s(format, 50, "%%.%df", k_precision_for_debug_tab);
    Cstr.Format(format, ret);
    m_CalcResult.SetWindowText(Cstr);
    Cstr.Format("Line: %d", line);
    m_CalcRelativeLine.SetWindowText(Cstr);
    SetExtendedWindowTitle(m_current_edit.GetString());	
  }
}

void CDlgFormulaScintilla::OnBnClickedAuto() {
  if (m_ButtonAuto.GetCheck() == 1) {
    ok_to_update_debug = false;
    ClearCalcResult();
    OnBnClickedApply();
	  // Validate parse trees
	  if (!p_function_collection->BotLogicCorrectlyParsed()) {
        CString s;
	    s.Format("There are syntax errors in one or more formulas that are\n");
	    s.Append("preventing calculation of this formula.\n");
	    s.Append("These errors need to be corrected before the 'Auto'\n");
	    s.Append("button can be used.");
	    MessageBox_Error_Warning(s, "PARSE ERROR(s)");
	    // All we need to do is remove the Auto Check since the button text hasn't been
      // updated yet and ok_to_update_debug has already been set to false
	    m_ButtonAuto.SetCheck(0);
	    return;
	  }
    m_ButtonAuto.SetWindowText("* Auto *");
	  ok_to_update_debug = true;
  } else {
	  ok_to_update_debug = false;
	  m_ButtonAuto.SetWindowText("Auto");
  }
}

void CDlgFormulaScintilla::StopAutoButton()
{
	// If auto button is on, turn it off
	if (m_ButtonAuto.GetCheck() == 1) {
		m_ButtonAuto.SetCheck(0);
		OnBnClickedAuto();
	}
}

void CDlgFormulaScintilla::UpdateDebugAuto(void) {
  p_function_collection->ClearCache();
  assert(p_debug_tab != NULL);
  CString result = p_debug_tab->EvaluateAll();
  m_pActiveScinCtrl->SendMessage(SCI_SETMODEVENTMASK, 0, 0);
  m_pActiveScinCtrl->SendMessage(SCI_SETTEXT,0,(LPARAM)result.GetString());
  m_pActiveScinCtrl->SendMessage(SCI_EMPTYUNDOBUFFER);
  m_pActiveScinCtrl->UpdateScrollWidth();
  m_pActiveScinCtrl->GotoPosition(0);
  m_pActiveScinCtrl->SendMessage(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT, 0);
}

void CDlgFormulaScintilla::WarnAboutAutoplayerWhenApplyingFormula()
{
	MessageBox_Interactive("Editing the formula while the autoplayer is enabled\n"
		"is an extremely insane idea\n"
		"(like changing wheels while driving on the highway).\n\n"
		"We will have to turn the autoplayer off,\n"
		"but to avoid any problems we skip saving.",
		"Warning", MB_OK | MB_TOPMOST);
	p_autoplayer->EngageAutoplayer(false);	
	return;
}

void CDlgFormulaScintilla::CopyTabContentsToFormulaSet() {
  for (int i=0; i<m_ScinArray.GetSize(); ++i) {
    CString name = m_ScinArray.GetAt(i)._name;
    if (name == "") {
      // This should only happen for the default "Empty" tab
      continue;
    }
    p_function_collection->SetFunctionText(name, 
      m_ScinArray.GetAt(i)._pWnd->GetText(),
      false);  
  }
}

void CDlgFormulaScintilla::OnBnClickedApply() {
  ClearCalcResult();
  COpenHoldemDoc		*pDoc = COpenHoldemDoc::GetDocument();
  // If autoplayer is engaged, dis-engage it
  if (p_autoplayer->autoplayer_engaged()) {
	  WarnAboutAutoplayerWhenApplyingFormula();
  }
  CopyTabContentsToFormulaSet();
  p_function_collection->ParseAll();
  if (!p_function_collection->BotLogicCorrectlyParsed()) {
    if (MessageBox_Interactive("There are errors in the working formula set.\n\n"
        "Would you still like to apply changes in the working set to the main set?\n\n"
        "Note that if you choose yes here, then the main formula set will \n"
        "contain errors, will likely not act as you expect, and may cause you\n"
        "to lose money at the tables.\n\n"
        "Please only click 'Yes' if you really know what you are doing.",
        "PARSE ERROR(s)", 
        MB_YESNO) != IDYES) {
      return;
    }
  }
  pDoc->SetModifiedFlag(true);
  // Re-calc symbols
  p_engine_container->EvaluateAll(); // ??? disabled while parsing
  m_dirty = false;
  HandleEnables(true);
}

bool CDlgFormulaScintilla::PromptToSave()
{
	COpenHoldemDoc		*pDoc = COpenHoldemDoc::GetDocument();

	int response = MessageBox_Interactive("You have made changes to this formula.\n\nDo you want to apply changes?", 
				   "Apply changes?", 
				   MB_YESNOCANCEL);
	if (response == IDYES)
	{
		OnBnClickedApply();
		return true;
	}
	
	if (response == IDNO)
	{
		m_dirty = false;
		return true;
	}
	return false;
}

void CDlgFormulaScintilla::OnSearchUpdate()
{
	CString filter;
	m_SearchEdit.GetWindowText(filter);
	if (filter.IsEmpty()) {
		UpdateAllScintillaKeywords();
	} else {
		for (int iScint=0; iScint<m_ScinArray.GetSize(); iScint++)
		{
			UpdateScintillaKeywords(m_ScinArray[iScint]._pWnd);
			m_ScinArray[iScint]._pWnd->Refresh();
		}
	}
	PopulateFormulaTree();
}

void CDlgFormulaScintilla::OnEditCut() 
{
	m_pActiveScinCtrl->Cut();
}

void CDlgFormulaScintilla::OnEditCopy() 
{
	m_pActiveScinCtrl->Copy();
}

void CDlgFormulaScintilla::OnEditPaste() 
{
	m_pActiveScinCtrl->Paste();
}

void CDlgFormulaScintilla::OnEditDeleteText() 
{
	m_pActiveScinCtrl->SendMessage(SCI_DELETEBACK, 0, 0);
}

void CDlgFormulaScintilla::OnEditUndo() 
{
	m_pActiveScinCtrl->Undo();
}

void CDlgFormulaScintilla::OnEditRedo() 
{
	m_pActiveScinCtrl->Redo();
}
void CDlgFormulaScintilla::OnEditSelectAll() 
{
	m_pActiveScinCtrl->SelectAll();
}

// Toggle word wrap (line wrapping) for all formula editors
void CDlgFormulaScintilla::OnViewWordWrap()
{
	m_word_wrap = !m_word_wrap;
	for (int i=0; i<m_ScinArray.GetSize(); i++)
	{
		CScintillaWnd *p_scintilla = m_ScinArray[i]._pWnd;
		if (p_scintilla != NULL && ::IsWindow(p_scintilla->GetSafeHwnd()))
		{
			p_scintilla->SetWordWrap(m_word_wrap);
		}
	}
	if (::IsWindow(m_EmptyScinCtrl.GetSafeHwnd()))
	{
		m_EmptyScinCtrl.SetWordWrap(m_word_wrap);
	}
	// Check mark in the View menu and on the toolbar button
	if (m_header != NULL)
	{
		m_header->SetCommandChecked(ID_FORMULA_VIEW_WORDWRAP, m_word_wrap);
	}
	if (m_pActiveScinCtrl != NULL)
	{
		m_pActiveScinCtrl->SetFocus();
	}
}

void CDlgFormulaScintilla::OnTimer(UINT_PTR nIDEvent) {
	if (nIDEvent == MENU_UPDATE_TIMER) 
	{
		HandleEnables(false);
	}
	// Update debug tab (if auto button is pressed)
	else if (nIDEvent == DEBUG_UPDATE_TIMER)
	{
		if (m_ButtonAuto.GetCheck() == 1 && m_current_edit == "f$debug" && ok_to_update_debug)
			UpdateDebugAuto();
	}
}

void CDlgFormulaScintilla::SetEquiDistantFont(CScintillaWnd *pWnd) {
  // http://support.microsoft.com/kb/85518/en-us
  // http://msdn.microsoft.com/de-de/library/chxkwcsd.aspx
  // http://en.wikipedia.org/wiki/Monospaced_font
  // http://en.wikipedia.org/wiki/Courier_%28typeface%29
  LOGFONT logfont; 
  //_edit_font.DeleteObject();
  _edit_font.GetLogFont(&logfont);
	for (int i=0; i<=MAX_STYLE_NUM; i++) {
		pWnd->SetFontname(i, logfont.lfFaceName);
		pWnd->SetFontheight(i, -logfont.lfHeight);
		pWnd->SetBold(i, (logfont.lfWeight==FW_BOLD ? true : false));
		pWnd->SetItalic(i, logfont.lfItalic);
	}
  
  _edit_font.DeleteObject();
  bool success = _edit_font.CreatePointFont(100, "Consolas");
  assert(success);
}

void CDlgFormulaScintilla::SetStyleColors(CScintillaWnd *pWnd) {
	// Colours follow the light / dark theme of the modern UI.
	// Light mode keeps the classic colours of the formula editor.
	using modernui::ToColorRef;
	const modernui::Palette& c = modernui::Theme::Colors();
	const bool dark = c.dark;
	const COLORREF back          = ToColorRef(dark ? c.window_bg : c.surface);
	const COLORREF text          = ToColorRef(c.text);
	const COLORREF margin_back   = ToColorRef(dark ? c.surface : c.window_bg);
	const COLORREF comment_color = dark ? ToColorRef(c.success) : RGB(0x00, 0x99, 0x00);
	const COLORREF keyword_color = dark ? ToColorRef(c.accent)  : RGB(0x00, 0x00, 0xFF);
	const COLORREF operator_color= dark ? ToColorRef(c.danger)  : RGB(0xFF, 0x00, 0x00);
	const COLORREF number_color  = dark ? RGB(0xE8, 0xB8, 0x6A) : RGB(0x00, 0x00, 0x00);
	const COLORREF string_color  = dark ? RGB(0xE5, 0x9A, 0x6B) : RGB(200, 20, 0);

	// Default style first, then propagate it to all other styles
	pWnd->SetBackground(STYLE_DEFAULT, back);
	pWnd->SetForeground(STYLE_DEFAULT, text);
	pWnd->SendMessage(SCI_STYLECLEARALL, 0, 0);

	pWnd->SetForeground(SCE_C_DEFAULT, text);
	// Comments: green
	pWnd->SetForeground(SCE_C_COMMENT, comment_color);			// (/* */)
	pWnd->SetForeground(SCE_C_COMMENTLINE, comment_color);		// (//)
	pWnd->SetForeground(SCE_C_COMMENTDOC, comment_color);
	pWnd->SetForeground(SCE_C_COMMENTLINEDOC, comment_color);
	// Numbers
	pWnd->SetForeground(SCE_C_NUMBER, number_color);
	// Keywords: blue
	pWnd->SetForeground(SCE_C_WORD, keyword_color);
	// Strings
	pWnd->SetForeground(SCE_C_STRING, string_color);
	pWnd->SetForeground(SCE_C_CHARACTER, string_color);
	pWnd->SetForeground(SCE_C_PREPROCESSOR, keyword_color);
	// Operators: red
	pWnd->SetForeground(SCE_C_OPERATOR, operator_color);
	pWnd->SetForeground(SCE_C_IDENTIFIER, text);
	// Background of the "global class" style
	pWnd->SetBackground(SCE_C_GLOBALCLASS, dark ? RGB(0x5C, 0x4A, 0x00) : RGB(250, 240, 0));

	// Margins, brace matching, caret, selection
	pWnd->SetBackground(STYLE_LINENUMBER, margin_back);
	pWnd->SetForeground(STYLE_LINENUMBER, ToColorRef(c.text_dim));
	pWnd->SetBackground(STYLE_BRACELIGHT, dark ? ToColorRef(c.success_soft) : RGB(0, 255, 0));
	pWnd->SetForeground(STYLE_BRACELIGHT, dark ? ToColorRef(c.success) : text);
	pWnd->SetForeground(STYLE_BRACEBAD, ToColorRef(c.danger));
	pWnd->SendMessage(SCI_SETFOLDMARGINCOLOUR, TRUE, (LPARAM)margin_back);
	pWnd->SendMessage(SCI_SETFOLDMARGINHICOLOUR, TRUE, (LPARAM)margin_back);
	pWnd->SendMessage(SCI_MARKERSETFORE, SC_MARKNUM_FOLDEROPEN, (LPARAM)margin_back);
	pWnd->SendMessage(SCI_MARKERSETBACK, SC_MARKNUM_FOLDEROPEN, (LPARAM)ToColorRef(c.text_dim));
	pWnd->SendMessage(SCI_MARKERSETFORE, SC_MARKNUM_FOLDER, (LPARAM)margin_back);
	pWnd->SendMessage(SCI_MARKERSETBACK, SC_MARKNUM_FOLDER, (LPARAM)ToColorRef(c.text_dim));
	pWnd->SendMessage(SCI_MARKERSETFORE, 0, (LPARAM)ToColorRef(c.accent));		// bookmark
	pWnd->SendMessage(SCI_MARKERSETBACK, 0, (LPARAM)ToColorRef(c.accent));
	pWnd->SendMessage(SCI_SETCARETFORE, (WPARAM)text, 0);
	pWnd->SendMessage(SCI_SETCARETLINEVISIBLE, TRUE, 0);
	pWnd->SendMessage(SCI_SETCARETLINEBACK, dark ? ToColorRef(c.surface) : RGB(0xF4, 0xF6, 0xFA), 0);
	pWnd->SendMessage(SCI_SETSELBACK, TRUE, (LPARAM)ToColorRef(c.accent_soft));
	pWnd->SendMessage(SCI_SETSELFORE, FALSE, 0);
}

// ---------------------------------------------------------------------------
// Modern UI
// ---------------------------------------------------------------------------

void CDlgFormulaScintilla::CreateModernHeader() {
	m_header = new modernui::ModernHeader();
	m_header->Create(m_hWnd, modernglue::kHeaderControlId);
	modernglue::ConfigureFormulaHeader(m_header);
	m_status = new modernui::ModernStatusbar();
	m_status->Create(m_hWnd, modernglue::kStatusbarControlId);
	CString title;
	GetWindowText(title);
	m_header->SetTitle(modernui::ToWide((LPCTSTR)title));
	UpdateStatusBar();
}

void CDlgFormulaScintilla::UpdateStatusBar() {
	if (m_status == NULL) return;
	m_status->SetTexts(modernui::ToWide((LPCTSTR)m_status_position),
		m_dirty ? L"Unsaved changes" : L"",
		m_dirty ? L"" : L"Applied");
}

// Gives all controls the colours of the current theme (also called when the
// user switches between light, dark and "follow Windows").
void CDlgFormulaScintilla::ApplyModernTheme() {
	const modernui::Palette& c = modernui::Theme::Colors();
	m_brush_window.DeleteObject();
	m_brush_window.CreateSolidBrush(modernui::ToColorRef(c.window_bg));
	m_brush_surface.DeleteObject();
	m_brush_surface.CreateSolidBrush(modernui::ToColorRef(c.surface));

	modernglue::ApplyTreeColors(&m_FormulaTree);
	modernglue::ApplyTreeColors(&m_SymbolTree);
	modernglue::ApplyEditColors(&m_SearchEdit);
	modernglue::ApplyEditColors(&m_CalcResult);
	modernglue::ApplyEditColors(&m_CalcRelativeLine);

	for (int i=0; i<m_ScinArray.GetSize(); i++) {
		CScintillaWnd *p_scintilla = m_ScinArray[i]._pWnd;
		if (p_scintilla != NULL && ::IsWindow(p_scintilla->GetSafeHwnd())) {
			SetStyleColors(p_scintilla);
			SetEquiDistantFont(p_scintilla);
			modernglue::ApplyControlTheme(p_scintilla->GetSafeHwnd());
			p_scintilla->Refresh();
		}
	}
	RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

LRESULT CDlgFormulaScintilla::OnThemeChanged(WPARAM, LPARAM) {
	ApplyModernTheme();
	return 0;
}

LRESULT CDlgFormulaScintilla::OnSetTextMessage(WPARAM wparam, LPARAM lparam) {
	LRESULT result = Default();
	if (m_header != NULL && lparam != 0) {
		m_header->SetTitle(modernui::ToWide((const char*)lparam));
	}
	return result;
}

void CDlgFormulaScintilla::OnNcCalcSize(BOOL bCalcValidRects, NCCALCSIZE_PARAMS* lpncsp) {
	// The client area is the whole window: no native caption, no native
	// border. (WS_CAPTION / WS_THICKFRAME stay, so that Windows still provides
	// shadow, snapping and the minimize / maximize animations.)
	// A maximized window extends over the sizing border on all sides: take it
	// away, so that nothing is cut off at the screen edges.
	if (bCalcValidRects && IsZoomed()) {
		const int padding = GetSystemMetrics(SM_CXPADDEDBORDER);
		::InflateRect(&lpncsp->rgrc[0],
			-(GetSystemMetrics(SM_CXSIZEFRAME) + padding),
			-(GetSystemMetrics(SM_CYSIZEFRAME) + padding));
	}
}

LRESULT CDlgFormulaScintilla::OnNcHitTest(CPoint point) {
	const float scale = modernui::DpiScale(m_hWnd);
	CRect window;
	GetWindowRect(&window);
	if (!IsZoomed()) {
		// Resizing at the edges (there is no native frame any more)
		const int border = (int)(6 * scale + 0.5f);
		const bool left   = point.x <  window.left   + border;
		const bool right  = point.x >= window.right  - border;
		const bool top    = point.y <  window.top    + border;
		const bool bottom = point.y >= window.bottom - border;
		if (top && left)     return HTTOPLEFT;
		if (top && right)    return HTTOPRIGHT;
		if (bottom && left)  return HTBOTTOMLEFT;
		if (bottom && right) return HTBOTTOMRIGHT;
		if (left)   return HTLEFT;
		if (right)  return HTRIGHT;
		if (top)    return HTTOP;
		if (bottom) return HTBOTTOM;
	}
	// The empty part of the title bar moves the window
	// (the buttons of the title bar are handled by the header itself).
	if (m_header != NULL) {
		CPoint client = point;
		ScreenToClient(&client);
		if (client.y >= 0 && client.y < m_header->TitleBarHeight(scale)) {
			return HTCAPTION;
		}
	}
	return HTCLIENT;
}

void CDlgFormulaScintilla::OnSysCommand(UINT nID, LPARAM lParam) {
	if ((nID & 0xFFF0) == SC_KEYMENU) {
		// Alt+<letter> opens the matching menu of our own menu bar.
		// Alt alone or Alt+Space: nothing, there is no classic system menu.
		if (lParam != 0 && lParam != ' ' && m_header != NULL) {
			m_header->OpenMenuByMnemonic((wchar_t)lParam);
		}
		return;
	}
	CDialog::OnSysCommand(nID, lParam);
}

void CDlgFormulaScintilla::OnGetMinMaxInfo(MINMAXINFO* lpMMI) {
	CDialog::OnGetMinMaxInfo(lpMMI);
	// Below this size the layout (header, trees, editor) makes no sense
	const float scale = modernui::DpiScale(m_hWnd);
	lpMMI->ptMinTrackSize.x = (LONG)(720 * scale);
	lpMMI->ptMinTrackSize.y = (LONG)(480 * scale);
}

BOOL CDlgFormulaScintilla::OnEraseBkgnd(CDC* pDC) {
	CRect client;
	GetClientRect(&client);
	pDC->FillSolidRect(&client, modernui::ToColorRef(modernui::Theme::Colors().window_bg));
	return TRUE;
}

HBRUSH CDlgFormulaScintilla::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
	if (m_brush_window.GetSafeHandle() == NULL) {
		// Very early phase of the creation: theme not applied yet
		return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
	}
	const modernui::Palette& c = modernui::Theme::Colors();
	const int id = (pWnd != NULL) ? pWnd->GetDlgCtrlID() : 0;
	if (nCtlColor == CTLCOLOR_EDIT || id == IDC_SEARCH_ED
			|| id == IDC_SCINTILLA_CALC_RESULT || id == IDC_SCINTILLA_CALC_LINE) {
		// Input fields and the (read-only) calc results
		pDC->SetTextColor(modernui::ToColorRef(c.text));
		pDC->SetBkColor(modernui::ToColorRef(c.surface));
		return (HBRUSH)m_brush_surface.GetSafeHandle();
	}
	pDC->SetTextColor(modernui::ToColorRef(c.text));
	pDC->SetBkColor(modernui::ToColorRef(c.window_bg));
	return (HBRUSH)m_brush_window.GetSafeHandle();
}

BOOL CDlgFormulaScintilla::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	if (PMainframe()->wait_cursor())
	{
		RestoreWaitCursor();
		return TRUE;
	}

	return CDialog::OnSetCursor(pWnd, nHitTest, message);
}


void CDlgFormulaScintilla::SortUdfTree()
{
	if (hUDFItem != NULL) 
	{
		m_FormulaTree.SortChildren(hUDFItem);
		HTREEITEM hSearchItem = m_FormulaTree.GetChildItem(hUDFItem);
		while (hSearchItem) 
		{
			if (m_FormulaTree.ItemHasChildren(hSearchItem))
				m_FormulaTree.SortChildren(hSearchItem);
			hSearchItem = m_FormulaTree.GetNextSiblingItem(hSearchItem);
		}
	}
}

void CDlgFormulaScintilla::SaveSettingsToRegistry()
{
	WINDOWPLACEMENT wp;
	HTREEITEM		hItem = NULL;
	CString			text = "";
	UINT			state = 0;

	GetWindowPlacement(&wp);
	Preferences()->SetValue(k_prefs_formula_x, wp.rcNormalPosition.left);
	Preferences()->SetValue(k_prefs_formula_y, wp.rcNormalPosition.top);
	Preferences()->SetValue(k_prefs_formula_dx, wp.rcNormalPosition.right - wp.rcNormalPosition.left);
	Preferences()->SetValue(k_prefs_formula_dy, wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
}

void CDlgFormulaScintilla::HandleEnables(bool AllItems)
{
	// These are what drive the enable/disable logic
	bool bFormulaVisible = true;
	bool bTreeHeadingSelected = false;
	bool bTreeValidLeafSelected = false;
	int  iWhichTypeSelected = kUndefined; // 0=Standard, 1=Hand List, 2=User Defined Function, -1=In_valid
	bool bFindInfo = false;
	bool bDebugActive = false;
	bool bNotesActive = false;
	bool bEditorWindowActive = false;
	// m_dirty

	// Figure out the state of the dialog
	HTREEITEM curSelected = m_FormulaTree.GetSelectedItem();
	HTREEITEM parentItem = (curSelected == NULL ? NULL : m_FormulaTree.GetParentItem(curSelected));

	bFindInfo = !m_FindLastSearch.IsEmpty();
	bTreeHeadingSelected = (parentItem == NULL);
	bTreeValidLeafSelected = (!bTreeHeadingSelected && curSelected && m_FormulaTree.GetChildItem(curSelected) == NULL);
	bEditorWindowActive = (m_pActiveScinCtrl && GetFocus() == m_pActiveScinCtrl);
	bFormulaVisible = (m_TabControl.GetCurSel() == 0);

	CString headingText;
	if (bTreeHeadingSelected && curSelected)
		headingText = m_FormulaTree.GetItemText(curSelected);
	else if (parentItem)
	{
		headingText = m_FormulaTree.GetItemText(curSelected);
		bDebugActive = (headingText == "f$debug");
		bNotesActive = (headingText == "notes");
		headingText = m_FormulaTree.GetItemText(parentItem);
	}
	if (headingText == "Autoplayer Functions")			iWhichTypeSelected = 0;
	else if (headingText == "Standard Functions")		iWhichTypeSelected = 0;
	else if (headingText == "Ini Functions")			  iWhichTypeSelected = 0;
	else if (headingText == "Debug Functions")			iWhichTypeSelected = 0;
	else if (headingText == "Hand Lists")				    iWhichTypeSelected = 1;
	else if (headingText == "User Defined Functions")	iWhichTypeSelected = 2;
	else {
		HTREEITEM hNextLevelUp = m_FormulaTree.GetParentItem(parentItem);
		if (hNextLevelUp != NULL) {
			headingText = m_FormulaTree.GetItemText(hNextLevelUp);
			if (headingText == "User Defined Functions")
				iWhichTypeSelected = 2;
		}
	}

	if (m_header == NULL) {
		return;
	}
	// Enable / disable by command-ID: applies to the toolbar buttons and to
	// the items of the menus at the same time.
	const bool has_text = bTreeValidLeafSelected && m_current_edit.GetLength()!=0 && m_pActiveScinCtrl;
	const bool has_selection = has_text && m_pActiveScinCtrl->GetSelectionStart()-m_pActiveScinCtrl->GetSelectionEnd()!=0;
	// Edit commands that relate to the state of the Scintilla window
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_UNDO,        has_text && m_pActiveScinCtrl->CanUndo());
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_REDO,        has_text && m_pActiveScinCtrl->CanRedo());
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_CUT,         has_selection);
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_COPY,        has_selection);
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_PASTE,       has_text && m_pActiveScinCtrl->CanPaste());
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_DELETE_TEXT, has_selection);
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_SELECTALL,   has_text);
	if (!AllItems)
		return;

	m_FormulaTree.EnableWindow(true); // Spew: Just in Case?

	// Enable the Buttons
	m_ButtonCalc.EnableWindow(bTreeValidLeafSelected && !bNotesActive);
	m_ButtonAuto.EnableWindow(bDebugActive);

	// File menu and buttons
	m_header->SetCommandEnabled(ID_FORMULA_FILE_APPLY,  m_dirty);
	m_header->SetCommandEnabled(ID_FILE_SAVE,           m_dirty);
	m_header->SetCommandEnabled(ID_FORMULA_FILE_CANCEL, true);
	m_FormulaApply.EnableWindow(m_dirty);
	m_FormulaCancel.EnableWindow(true);

	bool ModifiableSelected = (bTreeValidLeafSelected && iWhichTypeSelected>0);

	// Edit menu and toolbar: remaining items
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_NEW,       bFormulaVisible&&iWhichTypeSelected>0);
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_RENAME,    bFormulaVisible&&ModifiableSelected);
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_DELETE,    bFormulaVisible&&ModifiableSelected);
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_FIND,      bTreeValidLeafSelected);
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_FIND_NEXT, bTreeValidLeafSelected && bFindInfo);
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_FIND_PREV, bTreeValidLeafSelected && bFindInfo);
	m_header->SetCommandEnabled(ID_FORMULA_EDIT_HANDLIST,  bTreeValidLeafSelected && iWhichTypeSelected==1);

	// View menu
	m_header->SetCommandEnabled(ID_FORMULA_VIEW_WORDWRAP, true);
	m_header->SetCommandChecked(ID_FORMULA_VIEW_WORDWRAP, m_word_wrap);

	UpdateStatusBar();
}

HTREEITEM CDlgFormulaScintilla::AddSymbolSubTitle(HTREEITEM parentItem, const char *title, const char *description)
{
	toolTipData* toolTip = new toolTipData();
	toolTip->description = new CString(description);
	HTREEITEM ret = m_SymbolTree.InsertItem(title, parentItem);

	m_SymbolTree.SetItemData(ret, (DWORD_PTR)toolTip);

	return ret;
}

HTREEITEM CDlgFormulaScintilla::AddSymbolTitle(const char *title, const char *description, HTREEITEM parentItem)
{
	toolTipData* toolTip = new toolTipData();
	toolTip->description = new CString(description);

	HTREEITEM ret = m_SymbolTree.InsertItem(title, parentItem);

	m_SymbolTree.SetItemData(ret, (DWORD_PTR)toolTip);
	m_SymbolTree.SetItemState(ret, TVIS_BOLD, TVIS_BOLD);

	return ret;
}

HTREEITEM CDlgFormulaScintilla::AddSymbol(HTREEITEM parentItem, const char *symbol, const char *description)
{
	toolTipData* toolTip = new toolTipData();
	toolTip->description = new CString(description);

	HTREEITEM ret  = m_SymbolTree.InsertItem(symbol, hRawItem);
	m_SymbolTree.SetItemData(ret, (DWORD_PTR)toolTip);

	ret = m_SymbolTree.InsertItem(symbol, parentItem);
	m_SymbolTree.SetItemData(ret, (DWORD_PTR)toolTip);

	return ret;
}

void CDlgFormulaScintilla::PopulatePokerTrackerSymbols()
{
	HTREEITEM mainParent, parent;
	mainParent = parent = AddSymbolTitle("Poker Tracker symbols", NULL, hCatItem);
	parent = AddSymbolSubTitle(mainParent, "Symbols for chair X");
	for (int i=0; i<PT_DLL_GetNumberOfStats(); ++i)
	{
		CString description, symbol;
		symbol.Format("pt_%sX (X=0..9)", PT_DLL_GetBasicSymbolNameWithoutPTPrefix(i));
		description.Format("%s for chair X", PT_DLL_GetDescription(i));
		AddSymbol(parent, symbol, description);
	}

	parent = AddSymbolSubTitle(mainParent, "Symbols for the last raiser");
	for (int i=0; i<PT_DLL_GetNumberOfStats(); ++i)
	{
		CString description, symbol;
		symbol.Format("pt_%s_raischair", PT_DLL_GetBasicSymbolNameWithoutPTPrefix(i));
		description.Format("%s for the last raiser", PT_DLL_GetDescription(i));
		AddSymbol(parent, symbol, description);
	}

	parent = AddSymbolSubTitle(mainParent, "Symbols for headsup opponent chair");
	for (int i=0; i<PT_DLL_GetNumberOfStats(); ++i)
	{
		CString description, symbol;
		symbol.Format("pt_%s_headsup", PT_DLL_GetBasicSymbolNameWithoutPTPrefix(i));
		description.Format("%s for headsup opponent chair", PT_DLL_GetDescription(i));
		AddSymbol(parent, symbol, description);
	}
}

void CDlgFormulaScintilla::PopulateSymbols()
{
	if (m_SymbolTree.GetRootItem() != NULL)
		return;

	hRawItem = AddSymbolTitle("All Symbols", NULL, NULL);
	hCatItem = AddSymbolTitle("Categories", NULL, NULL);

	HTREEITEM mainParent, parent;
	mainParent = parent = AddSymbolTitle("General symbols", NULL, hCatItem);
	AddSymbol(parent, "nchairs", "the integer value for the Table Map symbol _s$nchairs");
	AddSymbol(parent, "session", "the current logging instance (0-24)");
	AddSymbol(parent, "version", "returns the version number of OpenHoldem that is currently running");

	mainParent = parent = AddSymbolTitle("Table Map symbols", NULL, hCatItem);
	AddSymbol(parent, "sitename$abc", "true if user defined string abc appears within the Table Map symbol _s$_sitename");
	AddSymbol(parent, "network$def", "true if user defined string def appears within the Table Map symbol _s$_network");
  AddSymbol(parent, "title$xyz", "true if user defined string xyz appears within the table title");
  AddSymbol(parent, "islobby", "true if the tablemap is designed to connect to the lobby");
  AddSymbol(parent, "ispopup", "true if the tablemap is designed to connect to known popup windows");

	mainParent = parent = AddSymbolTitle("Formula file", NULL, hCatItem);
	AddSymbol(parent, "f$prwin_number_of_iterations", "number of iterations tested by the analyzer(s)");

	mainParent = parent = AddSymbolTitle("Limits", NULL, hCatItem);
	AddSymbol(parent, "bblind", "the big blind amount");
	AddSymbol(parent, "sblind", "the small blind amount");
  AddSymbol(parent, "bbet", "the big bet amount (fixed limit)");
	AddSymbol(parent, "ante", "the current pre-deal ante requirement");
	AddSymbol(parent, "buyin", "the tournament buyin in dollars");
	AddSymbol(parent, "prizepool", "the tournament prize-pool in dollars");
	AddSymbol(parent, "prizepoolmultiplier", "the tournament prize-pool multiplier");
	AddSymbol(parent, "lim", "the current table limit 0=NL 1=PL 2=FL");
	AddSymbol(parent, "isnl", "(lim==0)");
	AddSymbol(parent, "ispl", "(lim==1)");
	AddSymbol(parent, "isfl", "(lim==2)");
	AddSymbol(parent, "istournament", "true if a tournament table is detected");
  AddSymbol(parent, "ismtt", "true if a multi-table tournament is detected");
  AddSymbol(parent, "issng", "true if a single-table tournament is detected");
  AddSymbol(parent, "isdon", "true if a double-or-nothing tournament is detected");
  AddSymbol(parent, "istripleup", "true if a triple-up tournament is detected");
  AddSymbol(parent, "isshootout", "true if a shoot-out tournament is detected");
  AddSymbol(parent, "isfreeroll", "true if a freeroll tournament is detected");
  AddSymbol(parent, "isknockout", "true if a knock-out tournament is detected");
  AddSymbol(parent, "isrebuy", "true if a rebuy tournament is detected");
  AddSymbol(parent, "issatelitte", "true if a issatelitte tournament is detected");
  AddSymbol(parent, "isspin", "true if a spin tournament is detected");
  AddSymbol(parent, "isturbo", "true if a turbo tournament is detected");
  AddSymbol(parent, "issemiturbo", "true if a semi-turbo tournament is detected");
  AddSymbol(parent, "issuperturbo", "true if a super-turbo tournament is detected");
  AddSymbol(parent, "ishyperturbo", "true if a hyper-turbo tournament is detected");
  AddSymbol(parent, "isultraturbo", "true if an ultra-turbo tournament is detected");
  AddSymbol(parent, "isfinaltable", "true if you are playing the finaltable of an MTT and the tables can be visually distinguished.");
  AddSymbol(parent, "isomaha", "true if the game is Omaha.");
  AddSymbol(parent, "isrush", "true if the game is rush / zoom.");
	
  mainParent = parent = AddSymbolTitle("MTT Info", NULL, hCatItem);
	AddSymbol(parent, "mtt_number_entrants", "number of entrants in the tournament");
	AddSymbol(parent, "mtt_players_remaining", "number of players remaining in the tournament");
	AddSymbol(parent, "mtt_paid_places", "number of places paid in the tournament");
	AddSymbol(parent, "mtt_my_rank", "my ranking in the tournament");
	AddSymbol(parent, "mtt_largest_stack", "the largest stack size in the tournament");
	AddSymbol(parent, "mtt_average_stack", "the average stack size in the tournament");
	AddSymbol(parent, "mtt_smallest_stack", "the smallest stack size in the tournament");
	
	mainParent = parent = AddSymbolTitle("Hand Rank", NULL, hCatItem);
	AddSymbol(parent, "handrank169", "your pocket holdem hand rank 1-169 (see table)");
	AddSymbol(parent, "handrank2652", "your pocket holdem hand rank 12-2652 (see table)");
	AddSymbol(parent, "handrank1326", "your pocket holdem hand rank 6-1326 (handrank2652/2)");
	AddSymbol(parent, "handrank1000", "your pocket holdem hand rank 4-1000 (1000*handrank2652/2652)");
	AddSymbol(parent, "handrankp", "2652 / (1+f$prwin_number_of_opponents)");

	mainParent = parent = AddSymbolTitle("Chairs", NULL, hCatItem);
	AddSymbol(parent, "userchair", "user chair number (0-9)");
  AddSymbol(parent, "headsupchair", "chair number of the one and only opponent headsup (0-9)");
  AddSymbol(parent, "bigblindchair", "big blind chair number(0 - 9)");
  AddSymbol(parent, "smallblindchair", "small blind chair number (0-9)");
  AddSymbol(parent, "dealerchair", "dealer chair number (0-9)");
  AddSymbol(parent, "buttonchair", "synonym for dealerchair (0-9)");
  AddSymbol(parent, "cutoffchair", "cut off chair number (0-9)");
  AddSymbol(parent, "mp3chair", "middle position 3 chair number (0-9)");
  AddSymbol(parent, "mp2chair", "middle position  2 chair number (0-9)");
  AddSymbol(parent, "mp1chair", "middle position  1 chair number (0-9)");
  AddSymbol(parent, "ep3chair", "early  position 3 chair number (0-9)");
  AddSymbol(parent, "ep2chair", "early  position 2 chair number (0-9)");
  AddSymbol(parent, "ep1chair", "early  position 1 chair number (0-9)");
  AddSymbol(parent, "utgchair", "under the gun chair number (0-9)");
  // Chairs, raisers and callers
  AddSymbol(parent, "firstcallerchair", "first caller chair number (0-9)");
  AddSymbol(parent, "lastcallerchair", "last caller chair number (0-9)");
  AddSymbol(parent, "firstraiserchair", "first raiser chair number (0-9)");
  AddSymbol(parent, "lastraiserchair", "last raiser chair number (0-9)");
  // lastraiserchair and raischair are synonyms
	AddSymbol(parent, "raischair", "synonym for lastraiserchair (0-9)");
	AddSymbol(parent, "chair$abc", "player abc chair number (0-9); -1 if not found");
	AddSymbol(parent, "chairbit$abc", "player abc chairbit (1 << chai_r$abc); 0 if not found");

	mainParent = parent = AddSymbolTitle("Betting Rounds / Positions", NULL, hCatItem);
	AddSymbol(parent, "betround", "betting round (1-4) 1=preflop, 2=flop, 3=turn, 4=river");
  AddSymbol(parent, "previousround", "previous betting round (1-3) 1=preflop, 2=flop, 3=turn");
  AddSymbol(parent, "currentround", "synonym for betround (1-4) 1=preflop, 2=flop, 3=turn, 4=river");
  AddSymbol(parent, "preflop", "verbose constant for the 1st betting round");
  AddSymbol(parent, "flop", "verbose constant for the 2nd betting round");
  AddSymbol(parent, "turn", "verbose constant for the 3rd betting round");
  AddSymbol(parent, "river", "verbose constant for the 4th betting round");
  
  mainParent = parent = AddSymbolTitle("Positions", NULL, hCatItem);
	AddSymbol(parent, "betposition", "your bet position (1=sblind,2=bblind,...,nplayersdealt=dealer).  Betposition will change as players fold in front of you.");
	AddSymbol(parent, "dealposition", "your deal position (1=sblind,2=bblind ... nplayersdealt=dealer).  Dealposition will not change as players fold.");
	AddSymbol(parent, "callposition", "your numbered offset from the raising player (who is 0)");
	AddSymbol(parent, "dealpositionrais", "the deal position of the raising player (1-10)");
	AddSymbol(parent, "betpositionrais", "the bet position of the raising player (1-10)");

	mainParent = parent = AddSymbolTitle("Probabilities", NULL, hCatItem);
	AddSymbol(parent, "prwin", "the probability of winning this hand (0.000 - 1.000)");
	AddSymbol(parent, "prlos", "the probability of losing this hand (0.000 - 1.000)");
	AddSymbol(parent, "prtie", "the probability of pushing this hand (0.000 - 1.000)");
	AddSymbol(parent, "prwinnow", "probability that all opponents have a lower hand right now");
	AddSymbol(parent, "prlosnow", "probability that any opponents have a higher hand right now");
	AddSymbol(parent, "prw1326_useme", "check value of prw1326->useme (if it is 1326 then prw1326 is enabled)");
	AddSymbol(parent, "prw1326_usepreflop", "check value of prw1326->preflop (if it is 1326 then prw1326 is enabled for preflop)");
	AddSymbol(parent, "prw1326_usecallback", "check value of prw1326->usecallback (if it is 1326 then prw1326 callback is invoked)");
	AddSymbol(parent, "random", "random number between (0.000-1.000) - gets evaluated once per heartbeat.");
	AddSymbol(parent, "randomhand", "random number between (0.000-1.000) for the hand");
	AddSymbol(parent, "randomround", "random number between (0.000-1.000) for the current round");

	mainParent = parent = AddSymbolTitle("Formulas", NULL, hCatItem);
	AddSymbol(parent, "f$name", "reference the specified formula");

	mainParent = parent = AddSymbolTitle("Chip Amounts", NULL, hCatItem);
	AddSymbol(parent, "balance", "your balance");
	AddSymbol(parent, "balance0 - balance9", "specific player/chair balance");
	AddSymbol(parent, "stack0 - stack9", "sorted playersplaying balances from 0=biggest to 9=smallest");
	AddSymbol(parent, "currentbet", "your current amount of chips in play");
	AddSymbol(parent, "currentbet0 - currentbet9", "specific player/chair currentbet");
	AddSymbol(parent, "call", "the amount you need to call");
	AddSymbol(parent, "bet", "the amount of a single initial bet or raise for current round");
	AddSymbol(parent, "bet1 - bet 4", "the amount of a single initial bet or raise for round 1 - round 4");
	AddSymbol(parent, "pot", "the total amount of chips in play including player bets");
	AddSymbol(parent, "potcommon", "the total amount of chips in the middle");
	AddSymbol(parent, "potplayer", "the total amount of chips in front of all players");

	mainParent = parent = AddSymbolTitle("Number of Bets", NULL, hCatItem);
	AddSymbol(parent, "nbetstocall", "total number of additional bets required to call.");
	AddSymbol(parent, "nbetstorais", "total number of additional bets required to raise.");
	AddSymbol(parent, "ncurrentbets", "total number of bets currently in front of you.");
	AddSymbol(parent, "ncallbets", "total number of bets you would have on the table if you call");
	AddSymbol(parent, "nraisbets", "total number of bets you would have on the table if you raise");

	mainParent = parent = AddSymbolTitle("List Tests", NULL, hCatItem);
	AddSymbol(parent, "listXYZ", "true if your hand is in list XYZ");

	mainParent = parent = AddSymbolTitle("Poker Values", NULL, hCatItem);
	AddSymbol(parent, "pokerval", "absolute poker value for your 5 card hand");
	AddSymbol(parent, "pokervalplayer", "absolute poker value for your 2 card pocket hand only.");
	AddSymbol(parent, "pokervalcommon", "absolute poker value for the common cards");
	AddSymbol(parent, "pcbits", "bit list of where your pocket cards are used in your 5 card hand");
	AddSymbol(parent, "npcbits", "number (0-2) of your pocket cards used in your 5 card hand");

	mainParent = parent = AddSymbolTitle("Poker Value Constants", NULL, hCatItem);
	AddSymbol(parent, "hicard", "1<< 0 (2 ** 0)");
	AddSymbol(parent, "onepair", "1<<24 (2 ** 24)");
	AddSymbol(parent, "twopair", "1<<25 (2 ** 25)");
	AddSymbol(parent, "threeofakind", "1<<26 (2 ** 26)");
	AddSymbol(parent, "straight", "1<<27 (2 ** 27)");
	AddSymbol(parent, "flush", "1<<28 (2 ** 28)");
	AddSymbol(parent, "fullhouse", "1<<29 (2 ** 29)");
	AddSymbol(parent, "fourofakind", "1<<30 (2 ** 30)");
	AddSymbol(parent, "straightflush", "1<<31 (2 ** 31)");
	AddSymbol(parent, "royalflush", "0x800edcba");

	mainParent = parent = AddSymbolTitle("Hand Tests", NULL, hCatItem);
	AddSymbol(parent, "$CCc", "see the hand symbol reference for details");
	AddSymbol(parent, "ishandup", "true if your hand has gone up a level (i.e. from 1 pair to 2 pair)");
	AddSymbol(parent, "ishandupcommon", "true if common hand has gone up a level (i.e. from 1 pair to 2 pair)");
	AddSymbol(parent, "ishicard", "true when you have hicard hand");
	AddSymbol(parent, "isonepair", "true when you have one pair");
	AddSymbol(parent, "istwopair", "true when you have two pair");
	AddSymbol(parent, "isthreeofakind", "true when you have three of a kind");
	AddSymbol(parent, "isstraight", "true when you have a straight");
	AddSymbol(parent, "isflush", "true when you have a flush");
	AddSymbol(parent, "isfullhouse", "true when you have a full house");
	AddSymbol(parent, "isfourofakind", "true when you have four of a kind");
	AddSymbol(parent, "isstraightflush", "true when you have a straight flush");
	AddSymbol(parent, "isroyalflush", "true when you have a royal flush");
  AddSymbol(parent, "nutfullhouseorfourofakind", "OpenPPL symbol that ranks the strength of quads and full houses, 1 = best, 999 = not at all");
  AddSymbol(parent, "nutfullhouseorfourofakind_ntotal", "total number of currently possible quads and full houses, for comparison");

	mainParent = parent = AddSymbolTitle("Pocket Tests", NULL, hCatItem);
	AddSymbol(parent, "ispair", "true when your two dealt pocket cards are rank equal (0-1)");
	AddSymbol(parent, "issuited", "true when your two dealt pocket cards are suit equal (0-1)");
	AddSymbol(parent, "isconnector", "true when your two dealt pocket cards are rank adjacent (0-1)");

	mainParent = parent = AddSymbolTitle("Pocket/Common Tests", NULL, hCatItem);
	AddSymbol(parent, "ishipair", "true when you have hi pair (0-1)");
	AddSymbol(parent, "islopair", "true when you have lo pair (0-1)");
	AddSymbol(parent, "ismidpair", "true when you have mid pair (0-1)");
	AddSymbol(parent, "ishistraight", "true when you have the highest straight possible");
	AddSymbol(parent, "ishiflush", "true when you have the highest flush possible");

	mainParent = parent = AddSymbolTitle("Players, Opponents", NULL, hCatItem);
  AddSymbol(parent, "maxnplayersdealt", "maximum number of dealt players in this session");
	AddSymbol(parent, "nplayersseated", "number of players seated (including you) (0-10)");
	AddSymbol(parent, "nplayersactive", "number of players active (including you) (0-10)");
	AddSymbol(parent, "nplayersdealt", "number of players dealt (including you) (0-10)");
	AddSymbol(parent, "nplayersplaying", "number of players playing (including you) (0-10)");
	AddSymbol(parent, "nplayersblind", "number of players blind (including you) (0-10)");
	AddSymbol(parent, "nopponentsseated", "number of opponents seated (not including you) (0-9)");
	AddSymbol(parent, "nopponentsactive", "number of opponents active (not including you) (0-9)");
	AddSymbol(parent, "nopponentsdealt", "number of opponents dealt (not including you) (0-9)");
	AddSymbol(parent, "nopponentsplaying", "number of opponents playing (not including you) (0-9)");
	AddSymbol(parent, "nopponentsblind", "number of opponents blind (not including you) (0-9)");
	AddSymbol(parent, "nopponentschecking", "number of opponents playing with a zero current bet equal to the previous bettor (0-9)");
	AddSymbol(parent, "nopponentscalling", "number of opponents playing with a non-zero current bet equal to the previous bettor (0-9)");
	AddSymbol(parent, "nopponentstruelyraising", "number of opponents playing with a current bet greater than the previous bettor (0-9). This does NOT include so-called \"blind-raisers\" and people who post antes, but it includes the first voluntary bettor postflop.");
	AddSymbol(parent, "nopponentsbetting", "number of opponents playing with a non zero current bet (0-9)");
	AddSymbol(parent, "nopponentsfolded", "number of opponents that have folded this hand (0-9)");
	AddSymbol(parent, "nplayerscallshort", "number of players that must call to stay in the hand");
	AddSymbol(parent, "nchairsdealtright", "number of chairs dealt before your chair");
	AddSymbol(parent, "nchairsdealtleft", "number of chairs dealt after your chair");
	AddSymbol(parent, "playersseatedbits", "bits 9-0: 1=seated 0=unseated");
	AddSymbol(parent, "playersactivebits", "bits 9-0: 1=active 0=inactive");
	AddSymbol(parent, "playersdealtbits", "bits 9-0: 1=dealt 0=notdealt");
	AddSymbol(parent, "playersplayingbits", "bits 9-0: 1=playing 0=notplaying");
	AddSymbol(parent, "playersblindbits", "bits 9-0: 1=blind 0=notblind");
	AddSymbol(parent, "opponentsseatedbits", "bits 9-0: 1=seated 0=unseated");
	AddSymbol(parent, "opponentsactivebits", "bits 9-0: 1=active 0=inactive");
	AddSymbol(parent, "opponentsdealtbits", "bits 9-0: 1=dealt 0=notdealt");
	AddSymbol(parent, "opponentsplayingbits", "bits 9-0: 1=playing 0=notplaying");
	AddSymbol(parent, "opponentsblindbits", "bits 9-0: 1=blind 0=notblind");
	AddSymbol(parent, "opponent_chair_headsup",  "headsup opponent chair number (0-9)");

  mainParent = parent = AddSymbolTitle("Flags", NULL, hCatItem);
	AddSymbol(parent, "fmax", "highest numbered flag button pressed");
	AddSymbol(parent, "f0 - f19", "true if flag 0 - flag 19 button is pressed false otherwise");
	AddSymbol(parent, "flagbits", "flag button bits 19-0 - 1=pressed 0=notpressed");

	mainParent = parent = AddSymbolTitle("Common Cards", NULL, hCatItem);
	AddSymbol(parent, "ncommoncardsknown", "number of common cards known (normal not highlighted)");

	mainParent = parent = AddSymbolTitle("(Un)known Cards", NULL, hCatItem);
	AddSymbol(parent, "ncardsknown", "total number of cards you can see (yours and commons)");
	AddSymbol(parent, "ncardsunknown", "total number of cards you cannot see (deck and opponents)");
	AddSymbol(parent, "ncardsbetter", "total number of single unknown cards that can beat you, e.g. if the board is four suited in hearts, and you have two spades, then ncardsbetter will be at least 9, because of the possible flush.");

	mainParent = parent = AddSymbolTitle("nhands", NULL, hCatItem);
	AddSymbol(parent, "nhands", "total possible number of two-card hands using the unseen cards (nhandshi+nhandslo+nhandsti)");
	AddSymbol(parent, "nhandshi", "number of hands that can beat you in a showdown right now");
	AddSymbol(parent, "nhandslo", "number of hands that you can beat in a showdown right now");
	AddSymbol(parent, "nhandsti", "number of hands that can tie you in a showdown right now");

	mainParent = parent = AddSymbolTitle("Flushes / Straights / Sets", NULL, hCatItem);
	AddSymbol(parent, "nsuited", "total number of same suited cards you have (1-7)");
	AddSymbol(parent, "nsuitedcommon", "total number of same suited cards in the middle (1-5)");
	AddSymbol(parent, "tsuit", "specific card suit for nsuited (1-4)");
	AddSymbol(parent, "tsuitcommon", "specific card suit for nsuitedcommon (1-4)");
	AddSymbol(parent, "nranked", "total number of same ranked cards you have (1-4)");
	AddSymbol(parent, "nrankedcommon", "total number of same ranked cards in the middle (1-4)");
	AddSymbol(parent, "trank", "specific card rank for nranked (2-14)");
	AddSymbol(parent, "trankcommon", "specific card rank for nrankedcommon (2-14)");
	AddSymbol(parent, "nstraight", "total number of connected cards you have (1-7)");
	AddSymbol(parent, "nstraightcommon", "total number of connected common cards (1-5)");
	AddSymbol(parent, "nstraightfill", "total number of cards needed to fill a straight (0-5)");
	AddSymbol(parent, "nstraightfillcommon", "total number of cards needed to fill a common straight (0-5)");
	AddSymbol(parent, "nstraightflush", "total number of suited connected cards you have (1-7)");
	AddSymbol(parent, "nstraightflushcommon", "total number of suited connected common cards (1-5)");
	AddSymbol(parent, "nstraightflushfill", "total number of cards needed to fill a straightflush (0-5)");
	AddSymbol(parent, "nstraightflushfillcommon", "total number of cards needed to fill a common straightflush (0-5)");

	mainParent = parent = AddSymbolTitle("Rank Bits (aces are hi and lo)", NULL, hCatItem);
	AddSymbol(parent, "rankbits", "bit list of card ranks (yours and commons)");
	AddSymbol(parent, "rankbitscommon", "bit list of card ranks (commons)");
	AddSymbol(parent, "rankbitsplayer", "bit list of card ranks (yours)");
	AddSymbol(parent, "rankbitspoker", "bit list of card ranks (pokerval)");
	AddSymbol(parent, "srankbits", "bit list of suited card ranks (yours and commons tsuit)");
	AddSymbol(parent, "srankbitscommon", "bit list of suited card ranks (commons tsuitcommon)");
	AddSymbol(parent, "srankbitsplayer", "bit list of suited card ranks (yours tsuit)");
	AddSymbol(parent, "srankbitspoker", "bit list of suited card ranks (pokerval tsuit)");
  AddSymbol(parent, "suitbitsplayerN", "bit list of card ranks(yours) for suit N (N = 0..3)");
  AddSymbol(parent, "suitbitscommonN", "bit list of card ranks(common) for suit N (N = 0..3)");

  mainParent = parent = AddSymbolTitle("Suit Constants", NULL, hCatItem);
  AddSymbol(parent, "clubs", "verbose constant to access suit symbols (value = 2)");
  AddSymbol(parent, "diamonds", "verbose constant to access suit symbols (value = 1)");
  AddSymbol(parent, "hearts", "verbose constant to access suit symbols (value = 0)");
  AddSymbol(parent, "spades", "verbose constant to access suit symbols (value = 3)");

	mainParent = parent = AddSymbolTitle("Rank Hi (aces are hi)", NULL, hCatItem);
	AddSymbol(parent, "rankhi", "highest card rank (14-2) (yours and commons)");
	AddSymbol(parent, "rankhicommon", "highest card rank (14-2) (commons)");
	AddSymbol(parent, "rankhiplayer", "highest card rank (14-2) (yours)");
	AddSymbol(parent, "rankhipoker", "highest card rank (14-2) (pokerval)");
	AddSymbol(parent, "srankhi", "highest suited card rank (14-2) (yours and commons tsuit)");
	AddSymbol(parent, "srankhicommon", "highest suited card rank (14-2) (commons tsuitcommon)");
	AddSymbol(parent, "srankhiplayer", "highest suited card rank (14-2) (yours tsuit)");
	AddSymbol(parent, "srankhipoker", "highest suited card rank (14-2) (pokerval tsuit)");

	mainParent = parent = AddSymbolTitle("Rank Lo (aces are hi)", NULL, hCatItem);
	AddSymbol(parent, "ranklo", "lowest card rank (14-2) (yours and commons)");
	AddSymbol(parent, "ranklocommon", "lowest card rank (14-2) (commons)");
	AddSymbol(parent, "rankloplayer", "lowest card rank (14-2) (yours)");
	AddSymbol(parent, "ranklopoker", "lowest card rank (14-2) (pokerval)");
	AddSymbol(parent, "sranklo", "lowest suited card rank (14-2) (yours and commons tsuit)");
	AddSymbol(parent, "sranklocommon", "lowest suited card rank (14-2) (commons tsuitcommon)");
	AddSymbol(parent, "srankloplayer", "lowest suited card rank (14-2) (yours tsuit)");
	AddSymbol(parent, "sranklopoker", "lowest suited card rank (14-2) (pokerval tsuit)");

	mainParent = parent = AddSymbolTitle("Time", NULL, hCatItem);
	AddSymbol(parent, "elapsed", "time in seconds since sitting down");
	AddSymbol(parent, "elapsedhand", "time in seconds since end of previous hand");
	AddSymbol(parent, "elapsedauto", "time in seconds since autoplayer took action");
	AddSymbol(parent, "elapsedtoday", "time in seconds since midnight GMT");
	AddSymbol(parent, "elapsedstart", "time in seconds since this OpenHoldem instance started");

	mainParent = parent = AddSymbolTitle("Autoplayer", NULL, hCatItem);
	AddSymbol(parent, "myturnbits", "bits 01234 correspond to buttons FCKRA (check allin raise call fold)");
	AddSymbol(parent, "ismyturn", "true if myturnbits in non/zero");
	AddSymbol(parent, "issittingin", "true when you are not being dealt out");
	AddSymbol(parent, "issittingout", "true when you are being dealt out");
	AddSymbol(parent, "isautopost", "true when you are autoposting");
	AddSymbol(parent, "isfinalanswer", "true when autoplayer preparing to act; false any other time.");

	mainParent = parent = AddSymbolTitle("History", NULL, hCatItem);
	AddSymbol(parent, "nplayersround1 - nplayersround4", "number of players that began betting round 1 - round 4");
	AddSymbol(parent, "nplayersround", "number of players that began the current betting round");
	AddSymbol(parent, "prevaction", "record of previously attempted autoplayer action. (-1=fold 0=chec 1=call 2=rais 3=betsize 4=alli)");
  AddSymbol(parent, "didfold", "true if the autoplayer has folded during current round");
	AddSymbol(parent, "didchec", "true if the autoplayer has checked during current round");
	AddSymbol(parent, "didcall", "true if the autoplayer has called during current round");
	AddSymbol(parent, "didrais", "true if the autoplayer has raised during current round");
	AddSymbol(parent, "didbetsize", "true if the autoplayer has betsized during current round");
  AddSymbol(parent, "didalli", "true if the autoplayer went allin during current round");
	AddSymbol(parent, "nbetsround1 - nbetsround4", "the largest number of bets in front of any player during round 1- round 4");
	AddSymbol(parent, "nbetsround", "the largest number of bets in front of any player right now");
  AddSymbol(parent, "didfoldround1 - didfoldround4", "true if userchair folded during round 1 - round 4");
	AddSymbol(parent, "didchecround1 - didchecround4", "true if userchair checked during round 1 - round 4");
	AddSymbol(parent, "didcallround1 - didcallround4", "true if userchair called during round 1 - round 4");
	AddSymbol(parent, "didraisround1 - didraisround4", "true if userchair raised during round 1 - round 4");
	AddSymbol(parent, "didbetsizeround1 - didbetsizeround4", "true if userchair betsized during round 1 - round 4");
	AddSymbol(parent, "didalliround1 - didalliround4", "true if userchair went allin during round 1 - round 4");

	mainParent = parent = AddSymbolTitle("Versus symbols", NULL, hCatItem);
	AddSymbol(parent, "vs$nhands", "Total possible number of opponent hands");
	AddSymbol(parent, "vs$nhandshi", "Number of opponent hands that have higher river chances ");
	AddSymbol(parent, "vs$nhandsti", "Number of opponent hands that have equal river chances");
	AddSymbol(parent, "vs$nhandslo", "Number of opponent hands that have lower river chances");
	AddSymbol(parent, "vs$prwin", "Probability (0.000 - 1.000) of winning versus all possible opponent hands ");
	AddSymbol(parent, "vs$prtie", "Probability (0.000 - 1.000) of chopping versus all possible opponent hands ");
	AddSymbol(parent, "vs$prlos", "Probability (0.000 - 1.000) of losing versus all possible opponent hands ");
	AddSymbol(parent, "vs$prwinhi", "Probability (0.000 - 1.000) of winning versus higher opponent hands ");
	AddSymbol(parent, "vs$prtiehi", "Probability (0.000 - 1.000) of chopping versus higher opponent hands ");
	AddSymbol(parent, "vs$prloshi", "Probability (0.000 - 1.000) of losing versus higher opponent hands ");
	AddSymbol(parent, "vs$prwinti", "Probability (0.000 - 1.000) of winning versus equal opponent hands ");
	AddSymbol(parent, "vs$prtieti", "Probability (0.000 - 1.000) of chopping versus equal opponent hands ");
	AddSymbol(parent, "vs$prlosti", "Probability (0.000 - 1.000) of losing versus equal opponent hands ");
	AddSymbol(parent, "vs$prwinlo", "Probability (0.000 - 1.000) of winning versus lower opponent hands ");
	AddSymbol(parent, "vs$prtielo", "Probability (0.000 - 1.000) of chopping versus lower opponent hands ");
	AddSymbol(parent, "vs$prloslo", "Probability (0.000 - 1.000) of losing versus lower opponent hands ");
	AddSymbol(parent, "vs$x$prwin", "Probability (0.000 - 1.000) of winnning versus hand list x");
	AddSymbol(parent, "vs$x$prlos", "Probability (0.000 - 1.000) of losing versus hand list x");
	AddSymbol(parent, "vs$x$prtie", "Probability (0.000 - 1.000) of a tie versus hand list x");
  AddSymbol(parent, "vs$multiplex$FUNC$prwin", "Probability (0.000 - 1.000) of a win versus hand list specified by FUNC");
  AddSymbol(parent, "vs$multiplex$FUNC$prtie", "Probability (0.000 - 1.000) of a tie versus hand list specified by FUNC");
  AddSymbol(parent, "vs$multiplex$FUNC$prlos", "Probability (0.000 - 1.000) of a los versus hand list specified by FUNC");

	mainParent = parent = AddSymbolTitle("History symbols", NULL, hCatItem);
	AddSymbol(parent, "hi_<sym>x (x=1-4)", "the value of the symbol <sym> as of your last turn in betting round x.  Example: hi_prwin1 would return prwin as of your last turn in br1.");
	/*
	Valid values for <sym> are:
	*PROBABILITIES: prwin, prlos, prtie
	*CHIP AMOUNTS: balance, balance0, balance1, balance2, balance3, balance4, balance5, balance6, balance7, balance8, balance9, stack0, stack1, stack2, stack3, stack4, stack5, stack6, stack7, stack8, stack9
	*POKER VALUES: pokerval, pokervalplayer, pokervalcommon, pcbits, npcbits
	*HAND TESTS: ishandup, ishandupcommon, ishicard, isonepair, istwopair, isthreeofakind, isstraight, isflush, isfullhouse, isfourofakind, isstraightflush, isroyalflush, 
	*POCKET/COMMON TESTS: ishipair, islopair, ismidpair, ishistraight, ishiflush
	*(UN)KNOWN CARDS: nouts, ncardsbetter
	*NHANDS: nhands, nhandshi, nhandslo, nhandsti, prwinnow, prlosnow
	*FLUSHES SETS STRAIGHTS: nsuited, nsuitedcommon, tsuit, tsuitcommon, nranked, nrankedcommon, trank, trankcommon, nstraight, nstraightcommon, nstraightfill, nstraightfillcommon, nstraightflush, nstraightflushcommon, nstraightflushfill, nstraightflushfillcommon
	*RANK BITS: rankbits, rankbitscommon, rankbitsplayer, rankbitspoker, srankbits, 	srankbitscommon, srankbitsplayer, srankbitspoker
	*RANK HI: rankhi, rankhicommon, rankhiplayer, rankhipoker, srankhi, srankhicommon, srankhiplayer, srankhipoker
	*RANK LO: ranklo, ranklocommon, rankloplayer, ranklopoker, sranklo, sranklocommon, srankloplayer, sranklopoker
	*run$ ron$: ron$royfl, ron$strfl, ron$4kind, ron$fullh, ron$strai, ron$3kind, ron$2pair, ron$1pair, ron$hcard, ron$total, ron$pokervalmax, ron$prnuts, ron$prbest, run$royfl, run$strfl, run$4kind, run$fullh, run$strai, run$3kind, run$2pair, run$1pair, run$hcard, run$total, run$pokervalmax, run$prnuts, run$prbest
	*/

	mainParent = parent = AddSymbolTitle("Action symbols", NULL, hCatItem);
	AddSymbol(parent, "lastraisedx (x=1-4)", "which chair was the last to raise in round x");
	AddSymbol(parent, "raisbitsx (x=1-4)", "which chairs raised in round x ");
	AddSymbol(parent, "callbitsx (x=1-4)", "which chairs called in round x");
	AddSymbol(parent, "foldbitsx (x=1-4)", "which chairs folded in round x");
	AddSymbol(parent, "ac_agchair_after", "does the aggressor chair act after me?");
	AddSymbol(parent, "ac_preflop_pos", "preflop position of the userchair (SB=1 BB=2 Early=3 Middle=4 Late=5 Dealer=6)");
	AddSymbol(parent, "ac_prefloprais_pos", "preflop position of the raiser (SB=1 BB=2 Early=3 Middle=4 Late=5 Dealer=6)");
	AddSymbol(parent, "ac_postflop_pos", "postflop position of the userchair (first=1 early=2 middle=3 late=4 last=5)");
	AddSymbol(parent, "ac_first_into_pot", "returns true if you are first into the pot (first to act or checked to you)");
	AddSymbol(parent, "ac_betposx (x=0-9)", "returns bet position of specified chair");
	AddSymbol(parent, "ac_dealposx (x=0-9)", "returns deal position of specified chair");

	mainParent = parent = AddSymbolTitle("Table stats symbols", "Note: the setting for [y minutes] can be found in Edit/Preferences, and defaults to 15 minutes.", hCatItem);
	AddSymbol(parent, "floppct", "percentage of players seeing the flop for the last 15 hands");
	AddSymbol(parent, "turnpct", "percentage of players seeing the turn for the last 15 hands");
	AddSymbol(parent, "riverpct", "percentage of players seeing the river for the last 15 hands");
	AddSymbol(parent, "avgbetspf", "average number of bets preflop for the last 15 hands");
	AddSymbol(parent, "tablepfr", "pfr percentage preflop for the last 15 hands");
	AddSymbol(parent, "maxbalance", "my highest balance during the session");
	AddSymbol(parent, "handsplayed", "number of hands played this session by this OpenHoldem instance");
    AddSymbol(parent, "handsplayed_headsup", "number of consecutive hands played headsup");
	AddSymbol(parent, "gamesplayed", "number of games played this session by this OpenHoldem instance");
	AddSymbol(parent, "balance_rankx (x=0-9)", "ranked list of player balances (includes players not currently in hand, and includes currentbet for each player as well).  rank0 has highest balance.");

	PopulatePokerTrackerSymbols();	
	
	mainParent = parent = AddSymbolTitle("ICM calculator symbols", NULL, hCatItem);
	AddSymbol(parent, "icm", "my tournament equity before any action is considered (just balances)");
	AddSymbol(parent, "icm_fold", "my tournament equity if I fold");
	AddSymbol(parent, "icm_callwin", "my tournament equity if I call and win");
	AddSymbol(parent, "icm_calllose", "my tournament equity if I call and lose");
	AddSymbol(parent, "icm_alliwin0 - icm_alliwin9", "my tournament equity if I push all-in and win against 0 - 9 callers");
	AddSymbol(parent, "icm_allilose1 - icm_allilose9", "my tournament equity if I push all-in and lose against 0 - 9 callers ");

	mainParent = parent = AddSymbolTitle("Hand multiplexor symbols", NULL, hCatItem);
	AddSymbol(parent, "f$$X", "Evaluate the UDF that corresponds to my first card");
	AddSymbol(parent, "f$$XX", "Evaluate the UDF that corresponds to my first card and second card");
	AddSymbol(parent, "f$$XXx", "Evaluate the UDF that corresponds to my first card, second card and suited/unsuited state");

	mainParent = parent = AddSymbolTitle("Memory symbols", NULL, hCatItem);
	AddSymbol(parent, "me_st_", "Stores a value. Example: me_st_abc_123_45 - stores the value '123.45' in variable 'abc'.");
	AddSymbol(parent, "me_re_", "Retrieves a previously stored value. Example: me_re_abc - retrieves the value from variable 'abc'.");
  AddSymbol(parent, "me_inc_", "Increments a previously stored value. Example: me_inc_abc - increments the value in variable 'abc'.");
  AddSymbol(parent, "me_add_", "Adds a value to a memory symbol. Example: me_add_outs_4'.");
  AddSymbol(parent, "me_sub_", "Subtracts a value from a memory symbol. Example: me_sub_outs_1_5");

	mainParent = parent = AddSymbolTitle("Hand Symbols", NULL, hCatItem);
	AddSymbol(parent, "$RR", "You can reference your dealt hand directly by using the $ symbols. The general form of the $ hand symbols are: $RRs. The $ character is required followed by 1 or 2 standard card rank characters. The hand symbols are not case sensitive.");

	mainParent = parent = AddSymbolTitle("Card Symbols", NULL, hCatItem);
	AddSymbol(parent, "$$ccX", "the card value for common card X (X=0-4)");
	AddSymbol(parent, "$$crX", "the rank value for common card X (X=0-4)");
	AddSymbol(parent, "$$csX", "the suit value for common card X (X=0-4)");
	AddSymbol(parent, "$$pcX", "the card value for player card X (X=0-3)");
	AddSymbol(parent, "$$prX", "the rank value for player card X (X=0-3)");
	AddSymbol(parent, "$$psX", "the suit value for player card X (X=0-3)");

	mainParent = parent = AddSymbolTitle("Hand and Board Expressions", NULL, hCatItem);
	AddSymbol(parent, "hand$XYZ", "True, if you have hand XYZ, e.g. hand$ATSuited");
	AddSymbol(parent, "board$XYZ", "True, if the board contains XYZ, e.g. board$AcTT");

	mainParent = parent = AddSymbolTitle("Debug messages", NULL, hCatItem);
	AddSymbol(parent, "msgbox$TEXT", "Displays a message-box and evaluates to 0");

	m_SymbolTree.SortChildren(hRawItem);
}

void CDlgFormulaScintilla::FormerShowEnableHideCodeClone(CScintillaWnd *new_pActiveScinCtrl) {
  if (m_pActiveScinCtrl) {
	  m_pActiveScinCtrl->ShowWindow(SW_HIDE); 
	  m_pActiveScinCtrl->EnableWindow(false);
  }
  m_pActiveScinCtrl = new_pActiveScinCtrl;
  m_pActiveScinCtrl->ShowWindow(SW_SHOW);
  if (new_pActiveScinCtrl == &m_EmptyScinCtrl) {
    m_pActiveScinCtrl->EnableWindow(false);
  } else {
    m_pActiveScinCtrl->EnableWindow(true);
  }
}

