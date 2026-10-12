//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: The thin MFC glue between the main frame and the ModernUI classes.
//
//******************************************************************************

#include "stdafx.h"
#include "ModernFrameSupport.h"

#include "resource.h"

namespace modernglue {

using namespace modernui;

namespace {

// Lets the existing ON_UPDATE_COMMAND_UI handlers (Enable / SetCheck / SetText)
// work on our own menu items instead of a native HMENU.
class CModernCmdUI : public CCmdUI {
 public:
  CModernCmdUI() : item(NULL) {}
  MenuItem* item;
  virtual void Enable(BOOL bOn = TRUE) {
    m_bEnableChanged = TRUE;
    item->enabled = (bOn != FALSE);
  }
  virtual void SetCheck(int nCheck = 1) {
    item->checked = (nCheck != 0);
  }
  virtual void SetRadio(BOOL bOn = TRUE) {
    item->checked = (bOn != FALSE);
    item->radio = true;
  }
  virtual void SetText(LPCTSTR lpszText) {
    std::wstring text, shortcut;
    wchar_t mnemonic = 0;
    ParseMenuText(ToWide(lpszText), &text, &mnemonic, &shortcut);
    item->text = text;
    if (!shortcut.empty()) item->shortcut = shortcut;
  }
};

bool IsRecentFileCommand(int id) {
  return id >= ID_FILE_MRU_FILE1 && id <= ID_FILE_MRU_FILE16;
}

// The classic menu has one placeholder "Recent File"; MFC used to replace it
// by the real list. We do the same for our own menu.
void FillRecentFiles(Menu* menu) {
  size_t pos = menu->items.size();
  for (size_t i = 0; i < menu->items.size(); ++i) {
    if (menu->items[i].id == ID_FILE_MRU_FILE1) pos = i;
  }
  if (pos == menu->items.size()) return;
  // Remove entries of a previous call (all but the anchor).
  for (size_t i = menu->items.size(); i-- > 0;) {
    int id = menu->items[i].id;
    if (id > ID_FILE_MRU_FILE1 && id <= ID_FILE_MRU_FILE16) menu->items.erase(menu->items.begin() + i);
  }
  CRecentFileList* list = AfxGetApp()->m_pRecentFileList;
  if (list == NULL || list->GetSize() == 0 || (*list)[0].IsEmpty()) {
    menu->items[pos].text = L"Recent files";
    menu->items[pos].enabled = false;
    return;
  }
  char current_dir[MAX_PATH] = { 0 };
  GetCurrentDirectory(MAX_PATH, current_dir);
  int inserted = 0;
  for (int i = 0; i < list->GetSize(); ++i) {
    if ((*list)[i].IsEmpty()) break;
    CString name;
    if (!list->GetDisplayName(name, i, current_dir, (int)strlen(current_dir))) continue;
    MenuItem item;
    item.id = ID_FILE_MRU_FILE1 + i;
    item.text = ToWide(name);
    item.mnemonic = (i < 9) ? (wchar_t)(L'1' + i) : 0;
    item.icon = Icon::Files;
    item.enabled = true;
    if (inserted == 0) {
      menu->items[pos] = item;
    } else {
      menu->items.insert(menu->items.begin() + pos + inserted, item);
    }
    ++inserted;
  }
}

// Runs the update handlers of all commands of the menu.
void UpdateMenuFromCommandUI(CFrameWnd* frame, Menu* menu) {
  int count = (int)menu->items.size();
  for (int i = 0; i < count; ++i) {
    MenuItem& item = menu->items[i];
    if (item.separator || item.id == 0 || IsRecentFileCommand(item.id)) continue;
    item.enabled = true;
    item.checked = false;
    CModernCmdUI ui;
    ui.item = &item;
    ui.m_nID = item.id;
    ui.m_nIndex = i;
    ui.m_nIndexMax = count;
    // TRUE: commands without any handler get disabled, exactly like in native menus.
    ui.DoUpdate(frame, TRUE);
  }
}

}  // namespace

Icon IconForCommand(int id) {
  switch (id) {
    case ID_FILE_NEW:                return Icon::FileNew;
    case ID_FILE_OPEN:               return Icon::FolderOpen;
    case ID_FILE_SAVE:               return Icon::Save;
    case ID_FILE_SAVE_AS:            return Icon::SaveAs;
    case ID_APP_EXIT:                return Icon::Exit;
    case ID_EDIT_FORMULA:            return Icon::Braces;
    case ID_EDIT_PREFERENCES:        return Icon::Settings;
    case ID_EDIT_TAGLOG:             return Icon::Tag;
    case ID_EDIT_VIEWLOG:            return Icon::ViewLog;
    case ID_EDIT_CLEARLOG:           return Icon::ClearLog;
    case ID_VIEW_SCRAPEROUTPUT:      return Icon::Eye;
    case ID_VIEW_SHOOTREPLAYFRAME:   return Icon::Camera;
    case ID_HELP_HELP:               return Icon::Manual;
    case ID_HELP_OPEN_PPL:           return Icon::Manual;
    case ID_HELP_FORUMS:             return Icon::Forums;
    case ID_HELP_PROBLEMSOLVER:      return Icon::ProblemSolver;
    case ID_APP_ABOUT:               return Icon::About;
    default: break;
  }
  if (IsRecentFileCommand(id)) return Icon::Files;
  return Icon::None;
}

const int* FlagCommandIds() {
  static const int ids[kFlagButtons] = {
    ID_NUMBER0,  ID_NUMBER1,  ID_NUMBER2,  ID_NUMBER3,  ID_NUMBER4,
    ID_NUMBER5,  ID_NUMBER6,  ID_NUMBER7,  ID_NUMBER8,  ID_NUMBER9,
    ID_NUMBER10, ID_NUMBER11, ID_NUMBER12, ID_NUMBER13, ID_NUMBER14,
    ID_NUMBER15, ID_NUMBER16, ID_NUMBER17, ID_NUMBER18, ID_NUMBER19,
  };
  return ids;
}

void ConfigureHeader(ModernHeader* header, CFrameWnd* frame) {
  // Menu bar: single source of truth is still the .rc file.
  MenuBar bar;
  LoadMenuBarFromResource(AfxGetInstanceHandle(), IDR_MAINFRAME, &bar);
  header->SetMenuBar(bar);
  header->SetCommandIconResolver(&IconForCommand);
  header->SetMenuHooks([frame](Menu& menu) {
    FillRecentFiles(&menu);
    UpdateMenuFromCommandUI(frame, &menu);
  });

  // Main toolbar: same buttons, same order and IDs as the old IDR_MAINFRAME toolbar.
  std::vector<ToolButtonDef> tools;
  ToolButtonDef d;
  d.checkable = false; d.success_tint = false; d.group_start = false;
  #define OH_TOOL(ID, ICON, LABEL, CHECKABLE, SUCCESS, GROUP) \
    d.id = ID; d.icon = ICON; d.label = LABEL; d.checkable = CHECKABLE; \
    d.success_tint = SUCCESS; d.group_start = GROUP; tools.push_back(d);
  OH_TOOL(ID_FILE_NEW,                    Icon::Plus,       L"New",         false, false, false)
  OH_TOOL(ID_FILE_OPEN,                   Icon::FileOpen,   L"Open",        false, false, false)
  OH_TOOL(ID_FILE_SAVE,                   Icon::Save,       L"Save",        false, false, false)
  OH_TOOL(ID_MAIN_TOOLBAR_AUTOPLAYER,     Icon::Play,       L"Autoplayer",  true,  true,  true)
  OH_TOOL(ID_MAIN_TOOLBAR_FORMULA,        Icon::Braces,     L"Formula",     true,  false, false)
  OH_TOOL(ID_MAIN_TOOLBAR_VALIDATOR,      Icon::Check,      L"Validator",   true,  false, false)
  OH_TOOL(ID_MAIN_TOOLBAR_TAGLOGFILE,     Icon::Tag,        L"Tag log",     true,  false, false)
  OH_TOOL(ID_MAIN_TOOLBAR_SCRAPER_OUTPUT, Icon::Eye,        L"Scraper",     true,  false, true)
  OH_TOOL(ID_MAIN_TOOLBAR_SHOOTFRAME,     Icon::Camera,     L"Shoot frame", false, false, false)
  OH_TOOL(ID_MAIN_TOOLBAR_MANUALMODE,     Icon::ManualMode, L"Manual mode", false, false, false)
  OH_TOOL(ID_MAIN_TOOLBAR_HELP,           Icon::Help,       L"Help",        false, false, true)
  #undef OH_TOOL
  header->SetToolbarButtons(tools);
  header->SetFlagCommandIds(FlagCommandIds(), kFlagButtons);

  // Application icon in the title bar (16x16)
  HICON icon = (HICON)::LoadImage(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDI_ICON1),
                                  IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
  if (icon != NULL) {
    header->SetAppIcon(icon);
    DestroyIcon(icon);
  }
}

}  // namespace modernglue
