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
  enum Kind { kHeading, kNote, kCheck, kNumber, kText, kLines, kRadio, kButton, kPreview };
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
  int     link_item;                  // preview: index of the radio group of the page that it shows
  std::function<void(CDlgPreferences*)> on_click;
  // called once after all values got saved
  std::function<void()>           after_apply;
  // value for "Reset to defaults" of a page (check box: 0/1, radio: index)
  bool    has_default;
  int     default_value;
  // controls (created by the dialog)
  int     first_id;
  CWnd*   label_ctrl;
  CWnd*   ctrl;
  CWnd*   spin;
  std::vector<CWnd*> radios;

  PrefsItem() : kind(kNote), min_value(0), max_value(0), step(1), disabled_options(0),
    disabled(false), same_row(false), link_item(-1), has_default(false), default_value(0), first_id(0), label_ctrl(NULL), ctrl(NULL), spin(NULL) {}
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
  CPrefsSidebar() : _selected(0), _hot(-1), _tracking(false), _scroll(0.0f) {}
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
  afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
  DECLARE_MESSAGE_MAP()
 private:
  float TopOffset(float scale) const;      // the list starts below the heading
  float RowHeight(float scale) const;      // shrinks to fit, min. 26 px
  float MaxScroll(float scale) const;
  void  ScrollTo(float pixels);
  void  EnsureSelectedVisible();
  int   HitTest(CPoint point) const;
  std::vector<CString> _titles;
  int  _selected, _hot;
  bool _tracking;
  float _scroll;                          // pixels
};

// The controls of one page. Hands all commands and notifications of its
// children to the dialog and gives them the colours of the theme.
class CPrefsPageWnd : public CWnd {
 public:
  CPrefsPageWnd() : _owner(NULL), _content_height(0), _scroll_pos(0) {}
  BOOL CreatePage(CWnd* parent, CWnd* owner, const CRect& rect);
  void ApplyTheme();
  // Height of all controls; shows a scrollbar if it is more than the window
  void SetContentHeight(int height);
  // Scrolls so that the control is visible (keyboard focus, validation)
  void EnsureVisible(CWnd* control);
 protected:
  afx_msg BOOL OnEraseBkgnd(CDC* pDC);
  afx_msg void OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar);
  afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
  afx_msg void OnSize(UINT nType, int cx, int cy);
  afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
  virtual BOOL OnCommand(WPARAM wParam, LPARAM lParam);
  virtual BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult);
  DECLARE_MESSAGE_MAP()
 private:
  void ScrollTo(int position);
  void UpdateScrollbar();
  CWnd*  _owner;
  CBrush _brush_window, _brush_field;
  int    _content_height, _scroll_pos;
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
  // Puts the default values into all controls of a page (not yet saved)
  void ResetPageToDefaults(const char* title);

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
  afx_msg BOOL OnMouseWheel(UINT nFlags, short zDelta, CPoint pt);
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
