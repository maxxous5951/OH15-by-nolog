//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: see ModernDialogs.h
//
//******************************************************************************

#include "stdafx.h"
#include "ModernDialogs.h"

#include <memory>
#include <string>
#include <vector>
#include "ModernControls.h"
#include "ModernUI/ModernGfx.h"
#include "ModernUI/ModernTheme.h"
#include "resource.h"

using namespace modernui;
using namespace Gdiplus;

namespace modernglue {
namespace {

// Dialog template without any control: everything is created and painted in code.
// 9 WORDs header (style, ex-style, #controls, x, y, cx, cy) + menu + class + title.
const DWORD kDialogTemplateStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
__declspec(align(4)) const WORD kEmptyDialogTemplate[] = {
  (WORD)(kDialogTemplateStyle & 0xFFFF),
  (WORD)(kDialogTemplateStyle >> 16),
  0, 0,
  0,
  0, 0, 100, 60,
  0, 0, 0
};

std::wstring ToWide(const CString& text) {
  return std::wstring(CT2W((LPCTSTR)text));
}

// Multi-line, wrapping text (DrawLabel is single line only)
void DrawParagraph(Graphics& g, const std::wstring& text, const Font* font,
                   const RectF& rect, const Color& color) {
  StringFormat format;
  format.SetTrimming(StringTrimmingNone);
  SolidBrush brush(color);
  g.DrawString(text.c_str(), -1, font, rect, &format, &brush);
}

float MeasureParagraphHeight(Graphics& g, const std::wstring& text, const Font* font, float width) {
  StringFormat format;
  format.SetTrimming(StringTrimmingNone);
  RectF bounds;
  g.MeasureString(text.c_str(), -1, font, RectF(0, 0, width, 0), &format, &bounds);
  return bounds.Height;
}

// ---------------------------------------------------------------------------
// Common base: themed frame, buttons, double-buffered painting
// ---------------------------------------------------------------------------

class CModernDialogBase : public CDialog {
 public:
  CModernDialogBase() : _default_id(IDOK), _topmost(false), _has_ok(false), _has_cancel(false), _only_ok(true) {}
 protected:
  virtual CString DialogTitle() = 0;
  // Creates the controls, returns the size of the client area
  virtual CSize LayoutContent(Graphics& g, float s) = 0;
  virtual void PaintContent(Graphics& g, float s, float width, float height) = 0;
  virtual void OnButtonPressed(int id) { EndDialog(id); }
 protected:
  void AddButton(int id, const char* label, const CRect& rect, bool primary) {
    std::unique_ptr<CModernButton> button(new CModernButton());
    button->Create(label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | (primary ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
      rect, this, id);
    button->SetPrimary(primary);
    if (id == IDOK) _has_ok = true;
    if (id == IDCANCEL) _has_cancel = true;
    if (id != IDOK) _only_ok = false;
    _buttons.push_back(std::move(button));
  }
  virtual BOOL OnInitDialog();
  virtual void OnOK() { EndDialog(_default_id); }
  virtual void OnCancel();
  afx_msg void OnPaint();
  afx_msg BOOL OnEraseBkgnd(CDC*) { return TRUE; }
  afx_msg void OnDestroy();
  afx_msg void OnCommandButton(UINT id) {
    // Esc / close box / Enter must behave like in ::MessageBox
    if (id == IDCANCEL && !_has_cancel) { OnCancel(); return; }
    if (id == IDOK && !_has_ok) { OnOK(); return; }
    OnButtonPressed((int)id);
  }
  afx_msg LRESULT OnThemeChanged(WPARAM, LPARAM);
  DECLARE_MESSAGE_MAP()
 protected:
  int  _default_id;
  bool _topmost;
 private:
  bool _has_ok, _has_cancel, _only_ok;
  std::vector<std::unique_ptr<CModernButton>> _buttons;
};

BEGIN_MESSAGE_MAP(CModernDialogBase, CDialog)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_DESTROY()
  ON_COMMAND_RANGE(IDOK, IDNO, &CModernDialogBase::OnCommandButton)
  ON_MESSAGE(modernui::WMU_THEME_CHANGED, &CModernDialogBase::OnThemeChanged)
END_MESSAGE_MAP()

BOOL CModernDialogBase::OnInitDialog() {
  CDialog::OnInitDialog();
  ModifyStyle(0, WS_CLIPCHILDREN);
  SetWindowText(DialogTitle());
  const float s = DpiScale(m_hWnd);
  CSize client;
  {
    CClientDC dc(this);
    Graphics g(dc.GetSafeHdc());
    SetupQuality(g);
    client = LayoutContent(g, s);
  }
  CRect frame(0, 0, client.cx, client.cy);
  ::AdjustWindowRectEx(&frame, GetStyle(), FALSE, GetExStyle());
  SetWindowPos(NULL, 0, 0, frame.Width(), frame.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  Theme::RegisterWindow(m_hWnd);
  Theme::ApplyFrameAttributes(m_hWnd);
  CenterWindow();
  if (_topmost) {
    SetWindowPos(&CWnd::wndTopMost, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  }
  SetDefID(_default_id);
  CWnd* focus = GetDlgItem(_default_id);
  if (focus != NULL) focus->SetFocus();
  return FALSE;   // focus set by us
}

void CModernDialogBase::OnCancel() {
  // Same behaviour as MessageBox: Esc closes dialogs with OK or Cancel,
  // but is ignored for Yes/No.
  if (_has_cancel) EndDialog(IDCANCEL);
  else if (_only_ok) EndDialog(IDOK);
}

void CModernDialogBase::OnDestroy() {
  Theme::UnregisterWindow(m_hWnd);
  CDialog::OnDestroy();
}

LRESULT CModernDialogBase::OnThemeChanged(WPARAM, LPARAM) {
  Theme::ApplyFrameAttributes(m_hWnd);
  RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
  return 0;
}

void CModernDialogBase::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  SetupQuality(g);
  const Palette& c = Theme::Colors();
  SolidBrush background(Rgba(c.window_bg));
  g.FillRectangle(&background, RectF(0, 0, (float)paint.Width(), (float)paint.Height()));
  PaintContent(g, DpiScale(m_hWnd), (float)paint.Width(), (float)paint.Height());
}

// ---------------------------------------------------------------------------
// Message box
// ---------------------------------------------------------------------------

class CDlgModernMessage : public CModernDialogBase {
 public:
  CDlgModernMessage(const char* message, const char* title, UINT flags)
    : _message(message), _title(title), _flags(flags), _footer_top(0) {
    _topmost = ((flags & MB_TOPMOST) != 0);
  }
 protected:
  virtual CString DialogTitle() { return _title; }
  virtual CSize LayoutContent(Graphics& g, float s);
  virtual void PaintContent(Graphics& g, float s, float width, float height);
 private:
  CString _message, _title;
  UINT    _flags;
  RectF   _icon_rect, _text_rect;
  int     _footer_top;
};

CSize CDlgModernMessage::LayoutContent(Graphics& g, float s) {
  auto px = [s](float v) { return (int)(v * s + 0.5f); };
  const int pad = px(28), icon = px(44), gap = px(16), width = px(470);
  const int button_width = px(96), button_height = px(34), button_gap = px(8);
  const float text_width = (float)(width - 2 * pad - icon - gap);
  CString text = _message;
  text.Replace("\r\n", "\n");
  const float text_height = MeasureParagraphHeight(g, ToWide(text), GetFont(14.5f * s), text_width);
  const int content_height = max((int)(text_height + 0.5f), icon);
  _icon_rect = RectF((float)pad, (float)pad, (float)icon, (float)icon);
  _text_rect = RectF((float)(pad + icon + gap), (float)pad + (content_height - text_height) / 2.0f,
                     text_width, text_height + 2.0f);
  _footer_top = pad + content_height + px(26);
  const int button_top = _footer_top + px(16);
  const int client_height = button_top + button_height + px(16);

  // Buttons, right aligned, in the order of ::MessageBox
  struct B { int id; const char* label; };
  std::vector<B> buttons;
  switch (_flags & 0x0F) {   // MB_TYPEMASK
    case MB_OKCANCEL:         buttons = { {IDOK, "OK"}, {IDCANCEL, "Cancel"} }; break;
    case MB_ABORTRETRYIGNORE: buttons = { {IDABORT, "Abort"}, {IDRETRY, "Retry"}, {IDIGNORE, "Ignore"} }; break;
    case MB_YESNOCANCEL:      buttons = { {IDYES, "Yes"}, {IDNO, "No"}, {IDCANCEL, "Cancel"} }; break;
    case MB_YESNO:            buttons = { {IDYES, "Yes"}, {IDNO, "No"} }; break;
    case MB_RETRYCANCEL:      buttons = { {IDRETRY, "Retry"}, {IDCANCEL, "Cancel"} }; break;
    default:                  buttons = { {IDOK, "OK"} }; break;
  }
  int default_index = (int)((_flags & 0x0F00) >> 8);        // MB_DEFMASK
  if (default_index < 0 || default_index >= (int)buttons.size()) default_index = 0;
  _default_id = buttons[default_index].id;
  int right = width - pad;
  for (int i = (int)buttons.size() - 1; i >= 0; --i) {
    const CRect rect(right - button_width, button_top, right, button_top + button_height);
    AddButton(buttons[i].id, buttons[i].label, rect, i == default_index);
    right -= button_width + button_gap;
  }
  ::MessageBeep(_flags & 0xF0);   // MB_ICONMASK
  return CSize(width, client_height);
}

void CDlgModernMessage::PaintContent(Graphics& g, float s, float width, float height) {
  const Palette& c = Theme::Colors();
  // Footer band behind the buttons
  SolidBrush footer(Rgba(c.surface));
  g.FillRectangle(&footer, RectF(0, (float)_footer_top, width, height - _footer_top));
  Pen line(Rgba(c.border), 1.0f);
  g.DrawLine(&line, 0.0f, (float)_footer_top, width, (float)_footer_top);
  // Icon
  DWORD colour = c.accent;
  const UINT icon = (_flags & 0xF0);
  if (icon == MB_ICONERROR) colour = c.danger;
  else if (icon == MB_ICONWARNING) colour = c.warning;
  const float cx = _icon_rect.X + _icon_rect.Width / 2, cy = _icon_rect.Y + _icon_rect.Height / 2;
  FillCircle(g, cx, cy, _icon_rect.Width / 2, WithAlpha(colour, 0x2A));
  const float glyph = _icon_rect.Width * 0.5f;
  const RectF glyph_rect(cx - glyph / 2, cy - glyph / 2, glyph, glyph);
  if (icon == MB_ICONERROR) {
    DrawIcon(g, Icon::Close, glyph_rect, Rgba(colour), 2.4f);
  } else if (icon == MB_ICONWARNING) {
    DrawLabel(g, L"!", GetFont(26 * s, true), _icon_rect, Rgba(colour), HAlign::Center, false);
  } else if (icon == MB_ICONQUESTION) {
    DrawIcon(g, Icon::Help, glyph_rect, Rgba(colour), 2.2f);
  } else {
    DrawIcon(g, Icon::About, glyph_rect, Rgba(colour), 2.2f);
  }
  // Text
  CString text = _message;
  text.Replace("\r\n", "\n");
  DrawParagraph(g, ToWide(text), GetFont(14.5f * s), _text_rect, Rgba(c.text));
}

// ---------------------------------------------------------------------------
// About this program
// ---------------------------------------------------------------------------

class CDlgModernAbout : public CModernDialogBase {
 public:
  CDlgModernAbout() : _hot_link(-1), _tracking(false), _separator1(0), _separator2(0) {}
 protected:
  virtual CString DialogTitle() { return "About this program"; }
  virtual CSize LayoutContent(Graphics& g, float s);
  virtual void PaintContent(Graphics& g, float s, float width, float height);
  afx_msg void OnMouseMove(UINT nFlags, CPoint point);
  afx_msg void OnMouseLeave();
  afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
  afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message);
  DECLARE_MESSAGE_MAP()
 private:
  int LinkAt(CPoint point) const;
 private:
  struct Link { const char* text; const char* url; RectF rect; };
  std::vector<Link> _links;
  RectF _disclaimer_rect;
  RectF _icon_rect, _title_rect, _version_rect, _license_rect, _caption_rect;
  int   _hot_link;
  bool  _tracking;
  int   _separator1, _separator2;
};

BEGIN_MESSAGE_MAP(CDlgModernAbout, CModernDialogBase)
  ON_WM_MOUSEMOVE()
  ON_WM_MOUSELEAVE()
  ON_WM_LBUTTONDOWN()
  ON_WM_SETCURSOR()
END_MESSAGE_MAP()

CSize CDlgModernAbout::LayoutContent(Graphics& g, float s) {
  auto px = [s](float v) { return (int)(v * s + 0.5f); };
  const int pad = px(30), width = px(520), icon = px(64);
  const int text_left = pad + icon + px(20);
  const int inner = width - 2 * pad;
  _icon_rect    = RectF((float)pad, (float)pad, (float)icon, (float)icon);
  _title_rect   = RectF((float)text_left, (float)pad - px(2), (float)(width - text_left - pad), (float)px(36));
  _version_rect = RectF((float)text_left, (float)pad + px(34), (float)(width - text_left - pad), (float)px(22));
  _license_rect = RectF((float)text_left, (float)pad + px(56), (float)(width - text_left - pad), (float)px(20));
  _separator1 = pad + icon + px(24);
  int y = _separator1 + px(18);
  _caption_rect = RectF((float)pad, (float)y, (float)inner, (float)px(18));
  y += px(26);
  _links.clear();
  const char* texts[] = { "http://www.maxinmontreal.com/forums/index.php",
                          "https://github.com/OpenHoldem/openholdembot/",
                          "http://www.gnu.org/licenses/gpl.html" };
  for (int i = 0; i < 3; ++i) {
    Link link = { texts[i], texts[i], RectF((float)pad, (float)y, (float)inner, (float)px(24)) };
    _links.push_back(link);
    y += px(26);
  }
  _separator2 = y + px(10);
  y = _separator2 + px(16);
  const float disclaimer_height = MeasureParagraphHeight(g,
    L"Disclaimer: this is an open-source project.\nWe are not related to any shops, stores and/or scam-artists.",
    GetFont(13 * s), (float)inner);
  _disclaimer_rect = RectF((float)pad, (float)y, (float)inner, disclaimer_height + 2.0f);
  y += (int)(disclaimer_height + 0.5f) + px(24);
  const int button_width = px(96), button_height = px(34);
  _default_id = IDOK;
  AddButton(IDOK, "OK", CRect(width - pad - button_width, y, width - pad, y + button_height), true);
  return CSize(width, y + button_height + px(24));
}

void CDlgModernAbout::PaintContent(Graphics& g, float s, float width, float height) {
  const Palette& c = Theme::Colors();
  // Application icon
  HICON icon = (HICON)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_MAINFRAME), IMAGE_ICON,
    (int)_icon_rect.Width, (int)_icon_rect.Height, LR_DEFAULTCOLOR);
  if (icon != NULL) {
    Bitmap* bitmap = Bitmap::FromHICON(icon);
    if (bitmap != NULL) {
      g.DrawImage(bitmap, _icon_rect);
      delete bitmap;
    }
    ::DestroyIcon(icon);
  }
  DrawLabel(g, L"OpenHoldem", GetFont(28 * s, true), _title_rect, Rgba(c.text), HAlign::Left, true);
  DrawLabel(g, ToWide(CString("Version ") + VERSION_TEXT), GetFont(15 * s, true), _version_rect,
            Rgba(c.accent), HAlign::Left, true);
  DrawLabel(g, L"Licensed under GPLv3", GetFont(13 * s), _license_rect, Rgba(c.text_dim), HAlign::Left, true);

  Pen separator(Rgba(c.border), 1.0f);
  g.DrawLine(&separator, _caption_rect.X, (float)_separator1, _caption_rect.X + _caption_rect.Width, (float)_separator1);
  g.DrawLine(&separator, _caption_rect.X, (float)_separator2, _caption_rect.X + _caption_rect.Width, (float)_separator2);

  DrawLabel(g, L"FORUMS, SOURCE CODE AND LICENSE", GetFont(11 * s, true), _caption_rect,
            Rgba(c.text_dim), HAlign::Left, true);
  for (int i = 0; i < (int)_links.size(); ++i) {
    const RectF& r = _links[i].rect;
    const bool hot = (i == _hot_link);
    if (hot) FillRoundRect(g, r, 6 * s, Rgba(c.accent_soft));
    DrawLabel(g, ToWide(_links[i].text), GetFont(14 * s, false),
              RectF(r.X + 8 * s, r.Y, r.Width - 16 * s, r.Height), Rgba(c.accent), HAlign::Left, true);
  }
  DrawParagraph(g, L"Disclaimer: this is an open-source project.\nWe are not related to any shops, stores and/or scam-artists.",
                GetFont(13 * s), _disclaimer_rect, Rgba(c.text_dim));
}

int CDlgModernAbout::LinkAt(CPoint point) const {
  for (int i = 0; i < (int)_links.size(); ++i) {
    if (_links[i].rect.Contains((float)point.x, (float)point.y)) return i;
  }
  return -1;
}

void CDlgModernAbout::OnMouseMove(UINT nFlags, CPoint point) {
  if (!_tracking) {
    TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hWnd, 0 };
    ::TrackMouseEvent(&track);
    _tracking = true;
  }
  const int hot = LinkAt(point);
  if (hot != _hot_link) {
    _hot_link = hot;
    Invalidate(FALSE);
  }
}

void CDlgModernAbout::OnMouseLeave() {
  _tracking = false;
  if (_hot_link != -1) {
    _hot_link = -1;
    Invalidate(FALSE);
  }
}

void CDlgModernAbout::OnLButtonDown(UINT nFlags, CPoint point) {
  const int link = LinkAt(point);
  if (link >= 0) {
    ::ShellExecute(m_hWnd, "open", _links[link].url, NULL, NULL, SW_SHOWNORMAL);
  }
}

BOOL CDlgModernAbout::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message) {
  if (nHitTest == HTCLIENT && _hot_link >= 0) {
    ::SetCursor(::LoadCursor(NULL, IDC_HAND));
    return TRUE;
  }
  return CDialog::OnSetCursor(pWnd, nHitTest, message);
}

}  // namespace

int ModernMessageBox(const char* message, const char* title, UINT flags, CWnd* parent /* = NULL */) {
  modernui::GdiplusAcquire();
  if (parent == NULL) parent = AfxGetMainWnd();
  CDlgModernMessage dialog(message, title, flags);
  dialog.InitModalIndirect((LPCDLGTEMPLATE)kEmptyDialogTemplate, parent);
  const int result = (int)dialog.DoModal();
  modernui::GdiplusRelease();
  return result;
}

void ShowAboutDialog(CWnd* parent /* = NULL */) {
  modernui::GdiplusAcquire();
  if (parent == NULL) parent = AfxGetMainWnd();
  CDlgModernAbout dialog;
  dialog.InitModalIndirect((LPCDLGTEMPLATE)kEmptyDialogTemplate, parent);
  dialog.DoModal();
  modernui::GdiplusRelease();
}

}  // namespace modernglue
