//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: The preferences of OpenHoldem, in one single themed (light / dark)
//   dialog: list of categories on the left, the settings of the selected
//   category on the right.
//
//   This replaces the 20 classic property pages (DialogSAPrefs2..22 with
//   their dialog templates, the page tree and the Smaller Animals shell).
//   The pages are described as data in DialogPreferences.cpp: every setting
//   knows its label and how to read / write the preference, the dialog
//   creates, lays out, validates and saves the controls itself.
//
//******************************************************************************

#ifndef INC_DIALOGPREFERENCES_H
#define INC_DIALOGPREFERENCES_H

#include "resource.h"
#include "ModernControls.h"
#include "ModernUI/ModernTheme.h"
#include <functional>
#include <memory>
#include <vector>

class CDlgPreferences;

// Sent by the category list to the dialog: wParam = index of the category
const UINT WMU_PREFS_SELECT = WM_APP + 0x0450;

// One setting (or a heading / a note) of a page
struct PrefsItem {
  enum Kind { kHeading, kNote, kCheck, kNumber, kText, kLines, kRadio, kButton };
  Kind    kind;
  CString label;
  CString key;                        // name to find the item (see CDlgPreferences::TextOf)
  // check box
  std::function<bool()>           get_bool;
  std::function<void(bool)>       set_bool;
  // number (min..max, step of the spin button) and radio group (index of the option)
  std::function<int()>            get_int;
  std::function<void(int)>        set_int;
  int     min_value, max_value, step;
  CString error_text;                 // message if the number is out of range
  // text field, list of lines
  std::function<CString()>        get_text;
  std::function<void(const CString&)> set_text;
  // returns an error message, or "" if the text is fine
  std::function<CString(const CString&)> validate_text;
  // radio group
  std::vector<CString> options;
  unsigned disabled_options;          // bit mask
  // check box / button
  bool    disabled;
  bool    same_row;                   // button: in the row of the previous heading, on the right
  std::function<void(CDlgPreferences*)> on_click;
  // called once after all values got saved
  std::function<void()>           after_apply;
  // controls (created by the dialog)
  int     first_id;
  CWnd*   label_ctrl;
  CWnd*   ctrl;
  CWnd*   spin;
  std::vector<CWnd*> radios;

  PrefsItem() : kind(kNote), min_value(0), max_value(0), step(1), disabled_options(0),
    disabled(false), same_row(false), first_id(0), label_ctrl(NULL), ctrl(NULL), spin(NULL) {}
};

struct PrefsPage {
  CString title;
  int     columns;                    // check boxes are put into this number of columns
  int     row_height;                 // of the check boxes
  std::vector<PrefsItem> items;
  CWnd*   window;                     // the controls of the page
  PrefsPage() : columns(1), row_height(28), window(NULL) {}
};

namespace modernglue {

// The list of categories on the left
class CPrefsSidebar : public CWnd {
 public:
  CPrefsSidebar() : _selected(0), _hot(-1), _tracking(false) {}
  BOOL CreateSidebar(CWnd* parent, const CRect& rect, UINT id);
  void SetTitles(const std::vector<CString>& titles) { _titles = titles; if (m_hWnd) Invalidate(FALSE); }
  void Select(int index, bool notify);
  int  Selected() const { return _selected; }
 protected:
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg LRESULT OnMouseLeave(WPARAM, LPARAM);
  afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
  afx_msg UINT OnGetDlgCode();
  afx_msg void OnSetFocus(CWnd* pOld);
  afx_msg void OnKillFocus(CWnd* pNew);
  DECLARE_MESSAGE_MAP()
 private:
  float TopOffset(float scale) const { return 12.0f * scale; }
  float RowHeight(float scale) const { return 36.0f * scale; }
  int   HitTest(CPoint point) const;
  std::vector<CString> _titles;
  int  _selected, _hot;
  bool _tracking;
};

// The controls of one page. Hands all commands and notifications of its
// children to the dialog and gives them the colours of the theme.
class CPrefsPageWnd : public CWnd {
 public:
  CPrefsPageWnd() : _owner(NULL) {}
  BOOL CreatePage(CWnd* parent, CWnd* owner, const CRect& rect);
  void ApplyTheme();
 protected:
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
  virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  DECLARE_MESSAGE_MAP()
 private:
  CWnd*  _owner;
  CBrush _brush_window, _brush_field;
};

}  // namespace modernglue

class CDlgPreferences : public CDialog {
 public:
  CDlgPreferences(CWnd* pParent = NULL);
  virtual ~CDlgPreferences();
  enum { IDD = IDD_PREFERENCES };

  // Current text of a text field of the dialog (not yet saved)
  CString TextOf(const char* key);
  // Current state of a check box of the dialog (not yet saved)
  bool CheckedOf(const char* key);
  // Flips all (enabled) check boxes of a page
  void ToggleChecksOfPage(const char* title);

 protected:
  virtual BOOL OnInitDialog();
  virtual void OnOK();
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
  virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  afx_msg LRESULT OnSelectPage(WPARAM wParam, LPARAM lParam);
  afx_msg LRESULT OnThemeChanged(WPARAM, LPARAM);
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
  afx_msg void OnDestroy();
  DECLARE_MESSAGE_MAP()

 private:
  struct CtrlRef { int page; int item; int sub; };

  void BuildPages();
  void CreateControls(int page_index);
  void LayoutPage(int page_index);
  void ShowPage(int page_index);
  void LayoutDialog();
  void LoadValues();
  bool Validate();
  void ApplyValues();
  void ApplyTheme();
  void OnItemClicked(UINT id);
  void SetRadio(PrefsItem& item, int index);
  int  GetRadio(const PrefsItem& item) const;
  bool GetCheck(const PrefsItem& item) const;
  CString GetText(const PrefsItem& item) const;
  PrefsItem* FindItem(const char* key);
  CWnd* Own(CWnd* window);

  std::vector<PrefsPage>  _pages;
  std::vector<CtrlRef>    _refs;
  std::vector<std::unique_ptr<CWnd> > _owned;
  modernglue::CPrefsSidebar _sidebar;
  CStatic   _title;
  modernglue::CModernButton _button_ok, _button_cancel;
  CFont     _font, _font_bold, _font_title;
  CBrush    _brush_window;
  CRect     _content_rect;          // where the pages live
  int       _current_page;
};

#endif  // INC_DIALOGPREFERENCES_H
