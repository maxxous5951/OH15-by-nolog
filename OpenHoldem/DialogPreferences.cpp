//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: The preferences of OpenHoldem, in one single themed dialog.
//   See DialogPreferences.h
//
//******************************************************************************

#include "stdafx.h"
#include "DialogPreferences.h"

#include <regex>
#include <string>
#include "CAdvisor.h"
#include "CDisplayAffinity.h"
#include "CHandHistoryWriter.h"
#include "COpenHoldemTitle.h"
#include "CPokerTrackerThread.h"
#include "MainFrm.h"
#include "OpenHoldem.h"
#include "ModernUI/ModernGfx.h"
#include "..\DLLs\Globals_DLL\globals.h"
#include "..\DLLs\Preferences_DLL\Preferences.h"
#include "..\DLLs\WindowFunctions_DLL\window_functions.h"

using namespace modernui;
using namespace Gdiplus;

// ---------------------------------------------------------------------------
// The pages, as data
// ---------------------------------------------------------------------------

namespace {

const int kFirstControlId = 4000;
const int kMaximumChatDelay = 1000000;        // seconds
const int kMaximumRebuyDelay = 3600;          // seconds

// -- writing preferences --
void SetNumber(int key, int value)       { Preferences()->SetValue(key, (double)value); }
void SetString(int key, const CString& s) { Preferences()->SetValue(key, (LPCSTR)s); }

// -- building items --
PrefsItem Heading(const char* text) {
  PrefsItem item;
  item.kind = PrefsItem::kHeading;
  item.label = text;
  return item;
}

PrefsItem Note(const char* text) {
  PrefsItem item;
  item.kind = PrefsItem::kNote;
  item.label = text;
  return item;
}

PrefsItem Check(const char* label, std::function<bool()> get, std::function<void(bool)> set) {
  PrefsItem item;
  item.kind = PrefsItem::kCheck;
  item.label = label;
  item.get_bool = get;
  item.set_bool = set;
  return item;
}

PrefsItem Number(const char* label, int min_value, int max_value, int step,
                 std::function<int()> get, std::function<void(int)> set, const char* error) {
  PrefsItem item;
  item.kind = PrefsItem::kNumber;
  item.label = label;
  item.min_value = min_value;
  item.max_value = max_value;
  item.step = step;
  item.get_int = get;
  item.set_int = set;
  item.error_text = error;
  return item;
}

PrefsItem Text(const char* label, const char* key,
               std::function<CString()> get, std::function<void(const CString&)> set) {
  PrefsItem item;
  item.kind = PrefsItem::kText;
  item.label = label;
  item.key = key;
  item.get_text = get;
  item.set_text = set;
  return item;
}

PrefsItem Radio(const std::vector<CString>& options, std::function<int()> get, std::function<void(int)> set) {
  PrefsItem item;
  item.kind = PrefsItem::kRadio;
  item.options = options;
  item.get_int = get;
  item.set_int = set;
  return item;
}

PrefsItem Button(const char* label, std::function<void(CDlgPreferences*)> on_click, bool same_row) {
  PrefsItem item;
  item.kind = PrefsItem::kButton;
  item.label = label;
  item.on_click = on_click;
  item.same_row = same_row;
  return item;
}

PrefsPage& NewPage(std::vector<PrefsPage>* pages, const char* title) {
  pages->push_back(PrefsPage());
  pages->back().title = title;
  return pages->back();
}

PrefsItem WithDefault(PrefsItem item, int default_value) {
  item.has_default = true;
  item.default_value = default_value;
  return item;
}

// After any change of a mouse setting, mouse.dll has to be told
void SendMouseSettingsAfterApply(PrefsItem* item) {
  item->after_apply = []() { theApp.SendMouseSettings(); };
}

// Shortcuts for the most common kinds of preferences
#define PREF_CHECK(label, getter, key) \
  Check(label, []() -> bool { return Preferences()->getter() != 0; }, \
        [](bool value) { SetNumber(key, value ? 1 : 0); })

#define PREF_NUMBER(label, min_value, max_value, step, getter, key, error) \
  Number(label, min_value, max_value, step, \
         []() -> int { return (int)Preferences()->getter(); }, \
         [](int value) { SetNumber(key, value); }, error)

#define PREF_TEXT(label, ident, getter, key) \
  Text(label, ident, []() -> CString { return CString(Preferences()->getter()); }, \
       [](const CString& value) { SetString(key, value); })

// -- the debug flags (same list, same order as the old "Debugging" page) --
struct DebugFlag {
  const char* label;
  bool (*get)();
  int  key;
  bool disabled;
};

const DebugFlag kDebugFlags[] = {
  { "Allin adjustment", []() -> bool { return Preferences()->debug_allin_adjustment(); }, k_prefs_debug_allin_adjustment, false },
  { "AST priority ordering", []() -> bool { return Preferences()->debug_ast_priority_ordering(); }, k_prefs_debug_ast_priority_ordering, false },
  { "Auto-connector", []() -> bool { return Preferences()->debug_autoconnector(); }, k_prefs_debug_autoconnector, false },
  { "Autoplayer", []() -> bool { return Preferences()->debug_autoplayer(); }, k_prefs_debug_autoplayer, false },
  { "Autoplayer trace", []() -> bool { return Preferences()->debug_auto_trace(); }, k_prefs_debug_auto_trace, false },
  { "Auto-starter", []() -> bool { return Preferences()->debug_autostarter(); }, k_prefs_debug_autostarter, false },
  { "Betsize adjustment", []() -> bool { return Preferences()->debug_betsize_adjustment(); }, k_prefs_debug_betsize_adjustment, false },
  { "Blind locking", []() -> bool { return Preferences()->debug_blindlocking(); }, k_prefs_debug_blindlocking, false },
  { "board$XYZ expressions", []() -> bool { return Preferences()->debug_hand_and_board_expressions(); }, k_prefs_debug_hand_and_board_expressions, false },
  { "DLL extension", []() -> bool { return Preferences()->debug_dll_extension(); }, k_prefs_debug_dll_extension, false },
  { "Engine container", []() -> bool { return Preferences()->debug_engine_container(); }, k_prefs_debug_engine_container, false },
  { "Enhanced PrWin", []() -> bool { return Preferences()->debug_enhanced_prwin(); }, k_prefs_debug_enhanced_prwin, false },
  { "Filenames", []() -> bool { return Preferences()->debug_filenames(); }, k_prefs_debug_filenames, false },
  { "Formula", []() -> bool { return Preferences()->debug_formula(); }, k_prefs_debug_formula, false },
  { "GUI", []() -> bool { return Preferences()->debug_gui(); }, k_prefs_debug_gui, false },
  { "Handhistory generator", []() -> bool { return Preferences()->debug_handhistory(); }, k_prefs_debug_handhistory, false },
  { "Handreset-detector", []() -> bool { return Preferences()->debug_handreset_detector(); }, k_prefs_debug_handreset_detector, false },
  { "Heartbeat-thread", []() -> bool { return Preferences()->debug_heartbeat(); }, k_prefs_debug_heartbeat, false },
  { "Hopper-messages", []() -> bool { return Preferences()->debug_hopper_messages(); }, k_prefs_debug_hopper_messages, false },
  { "ICM", []() -> bool { return Preferences()->debug_icm(); }, k_prefs_debug_icm, false },
  { "Istournament", []() -> bool { return Preferences()->debug_istournament(); }, k_prefs_debug_istournament, false },
  { "Lazy scraper", []() -> bool { return Preferences()->debug_lazy_scraper(); }, k_prefs_debug_lazy_scraper, false },
  { "Memory symbols", []() -> bool { return Preferences()->debug_memorysymbols(); }, k_prefs_debug_memorysymbols, false },
  { "Memory usage", []() -> bool { return Preferences()->debug_memory_usage(); }, k_prefs_debug_memory_usage, false },
  { "Multiplexer", []() -> bool { return Preferences()->debug_multiplexer(); }, k_prefs_debug_multiplexer, false },
  { "NutFullhouse", []() -> bool { return Preferences()->debug_nutfullhouse(); }, k_prefs_debug_nutfullhouse, false },
  { "Occlusion check", []() -> bool { return Preferences()->debug_occlusionchecker(); }, k_prefs_debug_occlusionchecker, false },
  { "OpenHoldem.cpp", []() -> bool { return Preferences()->debug_openholdem(); }, k_prefs_debug_openholdem, false },
  { "Parser", []() -> bool { return Preferences()->debug_parser(); }, k_prefs_debug_parser, false },
  { "Poker Tracker thread", []() -> bool { return Preferences()->debug_pokertracker(); }, k_prefs_debug_pokertracker, false },
  { "Popup blocker", []() -> bool { return Preferences()->debug_popup_blocker(); }, k_prefs_debug_popup_blocker, false },
  { "Preferences", []() -> bool { return Preferences()->debug_preferences(); }, k_prefs_debug_preferences, false },
  { "PrWin thread", []() -> bool { return Preferences()->debug_prwin(); }, k_prefs_debug_prwin, false },
  { "Rebuy management", []() -> bool { return Preferences()->debug_rebuy(); }, k_prefs_debug_rebuy, false },
  { "Replay frames", []() -> bool { return Preferences()->debug_replayframes(); }, k_prefs_debug_replayframes, false },
  { "Scraper", []() -> bool { return Preferences()->debug_scraper(); }, k_prefs_debug_scraper, false },
  { "Scraper directory monitor", []() -> bool { return Preferences()->debug_filesystem_monitor(); }, k_prefs_debug_filesystem_monitor, false },
  { "Scraper-preprocessor", []() -> bool { return Preferences()->debug_scraper_preprocessor(); }, k_prefs_debug_scraper_preprocessor, false },
  { "Session counter", []() -> bool { return Preferences()->debug_sessioncounter(); }, k_prefs_debug_sessioncounter, true },
  { "Shared memory", []() -> bool { return Preferences()->debug_sharedmem(); }, k_prefs_debug_sharedmem, false },
  { "Singletons", []() -> bool { return Preferences()->debug_singletons(); }, k_prefs_debug_singletons, false },
  { "Stable frames counter", []() -> bool { return Preferences()->debug_stableframescounter(); }, k_prefs_debug_stableframescounter, false },
  { "Symbol engine (OH-script)", []() -> bool { return Preferences()->debug_symbolengine(); }, k_prefs_debug_symbolengine, false },
  { "Symbol engine (OpenPPL)", []() -> bool { return Preferences()->debug_symbolengine_open_ppl(); }, k_prefs_debug_symbolengine_open_ppl, false },
  { "Symbol verification", []() -> bool { return Preferences()->debug_symbol_verification(); }, k_prefs_debug_symbol_verification, false },
  { "Table-limits (blind values)", []() -> bool { return Preferences()->debug_table_limits(); }, k_prefs_debug_table_limits, false },
  { "Tablemap loader", []() -> bool { return Preferences()->debug_tablemap_loader(); }, k_prefs_debug_tablemap_loader, false },
  { "Tablepoints", []() -> bool { return Preferences()->debug_tablepoints(); }, k_prefs_debug_tablepoints, false },
  { "Table positioner", []() -> bool { return Preferences()->debug_table_positioner(); }, k_prefs_debug_table_positioner, false },
  { "Timers", []() -> bool { return Preferences()->debug_timers(); }, k_prefs_debug_timers, false },
  { "Tokenizer", []() -> bool { return Preferences()->debug_tokenizer(); }, k_prefs_debug_tokenizer, false },
  { "Versus", []() -> bool { return Preferences()->debug_versus(); }, k_prefs_debug_versus, false },
  { "Watchdog", []() -> bool { return Preferences()->debug_watchdog(); }, k_prefs_debug_watchdog, false },
  { "All the rest", []() -> bool { return Preferences()->debug_alltherest(); }, k_prefs_debug_alltherest, false },
};

// -- lines of a list (unwanted scrape) --
const char* const kScrapeSeparator = "|~|";

std::vector<CString> SplitLines(const CString& text) {
  std::vector<CString> lines;
  int position = 0;
  CString line = text;
  line.Replace("\r\n", "\n");
  CString token = line.Tokenize("\n", position);
  while (position != -1) {
    if (!token.IsEmpty()) lines.push_back(token);
    token = line.Tokenize("\n", position);
  }
  return lines;
}

CString UnwantedScrapeAsLines() {
  CString lines;
  CString list = CString(Preferences()->unwanted_scrape());
  int position = 0;
  CString token = "null";
  while (!token.IsEmpty()) {
    token = list.Tokenize(kScrapeSeparator, position);
    if (!token.IsEmpty()) {
      lines += token;
      lines += "\r\n";
    }
  }
  return lines;
}

CString ValidateUnwantedScrape(const CString& text) {
  std::vector<CString> lines = SplitLines(text);
  if ((int)lines.size() > MAX_TOKENNUMBER) {
    CString message;
    message.Format("You have reached the %d maximum accepted entries.", MAX_TOKENNUMBER);
    return message;
  }
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].GetLength() > MAX_TOKENSIZE) {
      CString message;
      message.Format("Entries must not exceed limit of %d characters.", MAX_TOKENSIZE);
      return message;
    }
    try {
      std::regex test((LPCSTR)lines[i]);
    }
    catch (const std::exception&) {
      return "Invalid Regex expression(s) in your entries.";
    }
  }
  return "";
}

void SaveUnwantedScrape(const CString& text) {
  CString list;
  std::vector<CString> lines = SplitLines(text);
  for (size_t i = 0; i < lines.size(); ++i) {
    list += lines[i];
    list += kScrapeSeparator;
  }
  SetString(k_prefs_unwanted_scrape, list);
}

// -- PokerTracker: test the connection --
void TestPokerTrackerConnection(CDlgPreferences* dialog) {
  CString conn_str = "", e = "";
  CString ip_addr = dialog->TextOf("pt_ip");
  CString port = dialog->TextOf("pt_port");
  CString user = dialog->TextOf("pt_user");
  CString pass = dialog->TextOf("pt_pass");
  CString dbname = dialog->TextOf("pt_dbname");

  if (dbname.IsEmpty()) {
    // http://www.postgresql.org/docs/8.1/static/libpq.html
    write_log(Preferences()->debug_pokertracker(), "[PokerTracker] Test: PostgreSQL DB Name not set ! Default : It will be set to '%s'\n", (LPCTSTR)user);
    MessageBox_Interactive("PostgreSQL DB Name not set !\nBy Default : the same as the Username",
      "Warning", MB_OK);
  }
  conn_str = p_pokertracker_thread->CreateConnectionString(ip_addr, port, user, pass, dbname);

  // Set busy cursor
  PMainframe()->set_wait_cursor(true);
  PMainframe()->BeginWaitCursor();
  // Test the connection parameters
  PGconn *pgconn = PQconnectdb(conn_str.GetString());
  // Unset busy cursor
  PMainframe()->EndWaitCursor();
  PMainframe()->set_wait_cursor(false);

  if (PQstatus(pgconn) == CONNECTION_OK) {
    write_log(Preferences()->debug_pokertracker(), "[PokerTracker] Test: PostgreSQL DB opened successfully <%s/%s/%s>\n", (LPCTSTR)ip_addr, (LPCTSTR)port, (LPCTSTR)dbname);
    if (PQisthreadsafe()) {
      write_log(Preferences()->debug_pokertracker(), "[PokerTracker] Test: PostgreSQL library is thread safe.\n\n");
      MessageBox_Interactive("PostgreSQL DB opened successfully", "Success", MB_OK);
    } else {
      write_log(Preferences()->debug_pokertracker(), "[PokerTracker] Test: PostgreSQL library is *NOT* thread safe!  This is a problem!\n\n");
      MessageBox_Interactive("PostgreSQL DB opened successfully, but\nPostgreSQL library is *NOT* thread safe!\nThis is a problem!",
        "Success (partial)", MB_OK);
    }
    PQfinish(pgconn);
  } else {
    write_log(Preferences()->debug_pokertracker(), "[PokerTracker] Test: ERROR opening PostgreSQL DB: %s\n\n", PQerrorMessage(pgconn));
    e = "ERROR opening PostgreSQL DB:\n";
    e += PQerrorMessage(pgconn);
    e += "\nConn string:";
    e += conn_str;
    MessageBox_Interactive(e.GetString(), "ERROR", MB_OK);
    PQfinish(pgconn);
  }
}

}  // namespace

void CDlgPreferences::BuildPages() {
  _pages.clear();
  // The categories, alphabetically (as before)

  // ----- Auto-Connector -----
  {
    PrefsPage& p = NewPage(&_pages, "Auto-Connector");
    p.items.push_back(Heading("Auto-connect"));
    p.items.push_back(Radio({ "Never (by hopper messages)", "Permanent" },
      []() -> int { return Preferences()->autoconnector_when_to_connect() == k_AutoConnector_Connect_Never ? 0 : 1; },
      [](int index) { SetNumber(k_prefs_autoconnector_when_to_connect,
        index == 0 ? k_AutoConnector_Connect_Never : k_AutoConnector_Connect_Permanent); }));
    p.items.push_back(Heading("Window capture"));
    p.items.push_back(Radio({ "Original (BitBlt, table must be visible)", "Windows PrintWindow (works if covered)" },
      []() -> int { return Preferences()->capture_method() == k_CaptureMethod_PrintWindow ? 1 : 0; },
      [](int index) { SetNumber(k_prefs_capture_method,
        index == 1 ? k_CaptureMethod_PrintWindow : k_CaptureMethod_Original); }));
    p.items.push_back(Note("PrintWindow renders the table window into a bitmap. Takes effect immediately; "
      "falls back to the original method if PrintWindow fails."));
    p.items.push_back(Heading("Screenshot protection"));
    PrefsItem screenshots = PREF_CHECK("Hide OpenHoldem windows from screenshots", block_screenshots, k_prefs_block_screenshots);
    // Apply immediately to all open windows (including this dialog)
    screenshots.after_apply = []() { CDisplayAffinity::Apply(); };
    p.items.push_back(screenshots);
    p.items.push_back(Note("Uses SetWindowDisplayAffinity (Windows 7 or newer). Takes effect immediately; "
      "the windows stay visible on your monitor."));
  }

  // ----- Autoplayer -----
  {
    PrefsPage& p = NewPage(&_pages, "Autoplayer");
    p.items.push_back(Heading("Autoplayer delays"));
    p.items.push_back(PREF_NUMBER("Stable frames required (0-9)", 0, MAX_FRAMEDELAY, 1,
      frame_delay, k_prefs_frame_delay, "Invalid Frame Delay"));
    p.items.push_back(PREF_NUMBER("Button click delay (0-5000 ms)", 0, MAX_CLICKDELAY, 1,
      click_delay, k_prefs_click_delay, "Invalid Click Delay"));
    p.items.push_back(PREF_NUMBER("Betsize: select to delete (0-5000 ms)", 0, MAX_SWAGDELAY1, 100,
      swag_delay_1, k_prefs_swag_delay_1, "Invalid Betsize Delay (Select to Delete)"));
    p.items.push_back(PREF_NUMBER("Betsize: delete to entry (0-5000 ms)", 0, MAX_SWAGDELAY2, 100,
      swag_delay_2, k_prefs_swag_delay_2, "Invalid Betsize Delay (Delete to Entry)"));
    p.items.push_back(PREF_NUMBER("Betsize: entry to confirm (0-5000 ms)", 0, MAX_SWAGDELAY3, 100,
      swag_delay_3, k_prefs_swag_delay_3, "Invalid Betsize Delay (Entry to Confirm)"));
    p.items.push_back(Heading("Behaviour"));
    p.items.push_back(PREF_CHECK("Engage autoplayer upon connection", engage_autoplayer, k_prefs_engage_autoplayer));
    p.items.push_back(PREF_CHECK("Restore mouse position and window focus at real casinos",
      restore_position_and_focus, k_prefs_restore_position_and_focus));
    p.items.push_back(PREF_CHECK("Advance replay frames during replay", use_auto_replay, k_prefs_use_auto_replay));
    p.items.push_back(Heading("Advisor mode"));
    PrefsItem advisor = PREF_CHECK("Enable advisor (shows the best action instead of playing automatically)",
      advisor_enabled, k_prefs_advisor_enabled);
    // Keeps the toolbar button in sync and shows / hides the window immediately
    advisor.after_apply = []() { if (p_advisor != NULL) p_advisor->RefreshFromPreferences(); };
    p.items.push_back(advisor);
    p.items.push_back(Note("A small window on the table shows what the profile would do (best action) "
      "and what you did (action taken). It follows the table window; drag it to snap it to a corner."));
    p.items.push_back(Radio({ "Top left", "Top right", "Bottom left", "Bottom right" },
      []() -> int { return Preferences()->advisor_corner(); },
      [](int index) { SetNumber(k_prefs_advisor_corner, index); }));
  }

  // ----- Auto-starter -----
  {
    PrefsPage& p = NewPage(&_pages, "Auto-starter");
    p.items.push_back(PREF_CHECK("Use auto-starter", use_auto_starter, k_prefs_use_auto_starter));
    p.items.push_back(PREF_CHECK("Use auto-shutdown", use_auto_shutdown, k_prefs_use_auto_shutdown));
  }

  // ----- Chat -----
  {
    PrefsPage& p = NewPage(&_pages, "Chat");
    p.items.push_back(Heading("Safety measures"));
    p.items.push_back(PREF_NUMBER("Minimum delay (sec)", 0, kMaximumChatDelay, 1,
      chat_min_delay, k_prefs_chat_min_delay, "Invalid minimum Chat Delay"));
    p.items.push_back(PREF_NUMBER("Random delay (sec)", 0, kMaximumChatDelay, 1,
      chat_random_delay, k_prefs_chat_random_delay, "Invalid random Chat Delay"));
  }

  // ----- Configuration Check -----
  {
    PrefsPage& p = NewPage(&_pages, "Configuration Check");
    p.items.push_back(PREF_CHECK("Check keyboard layout settings (english - UK, US)",
      configurationcheck_input_settings, k_prefs_configurationcheck_input_settings));
    p.items.push_back(PREF_CHECK("Check Font Smoothing Settings",
      configurationcheck_font_settings, k_prefs_configurationcheck_font_settings));
    p.items.push_back(Note("Correct and working system settings are extremely important\n"
      "  * for the scraper\n"
      "  * for the autoplayer\n"
      "  * if you want to exchange tablemaps with other people\n"
      "Therefore the manual recommends some defaults\n"
      "and we strongly recommend to keep these checks enabled."));
  }

  // ----- Debugging -----
  {
    PrefsPage& p = NewPage(&_pages, "Debugging");
    p.columns = 3;
    p.row_height = 22;
    p.items.push_back(Heading("Write debug information to the log"));
    p.items.push_back(Button("Toggle all", [](CDlgPreferences* dialog) { dialog->ToggleChecksOfPage("Debugging"); }, true));
    for (int i = 0; i < (int)(sizeof(kDebugFlags) / sizeof(kDebugFlags[0])); ++i) {
      const DebugFlag& flag = kDebugFlags[i];
      bool (*get)() = flag.get;
      const int key = flag.key;
      PrefsItem item = Check(flag.label, [get]() -> bool { return get(); },
        [key](bool value) { SetNumber(key, value ? 1 : 0); });
      item.disabled = flag.disabled;
      p.items.push_back(item);
    }
  }

  // ----- DLL Extension -----
  {
    PrefsPage& p = NewPage(&_pages, "DLL Extension");
    p.items.push_back(Note("There are currently no DLL options.\n"
      "The user.dll gets loaded automatically on startup.\n"
      "A sample DLL can be found at \"trunk/Reference User DLL\""));
  }

  // ----- GUI -----
  {
    PrefsPage& p = NewPage(&_pages, "GUI");
    p.items.push_back(Heading("GUI Visibility"));
    PrefsItem visibility = Radio({ "First GUI visible", "All GUIs minimized", "No GUI at all" },
      []() -> int {
        if (Preferences()->gui_first_visible()) return 0;
        if (Preferences()->gui_all_minimized()) return 1;
        if (Preferences()->gui_less()) return 2;
        return -1;
      },
      [](int index) {
        SetNumber(k_prefs_gui_first_visible, index == 0 ? 1 : 0);
        SetNumber(k_prefs_gui_all_minimized, index == 1 ? 1 : 0);
        SetNumber(k_prefs_gui_less, index == 2 ? 1 : 0);
      });
    visibility.disabled_options = 1u << 2;     // "No GUI at all" is not available
    p.items.push_back(visibility);
    p.items.push_back(Heading("Messages"));
    PrefsItem msgbox = PREF_CHECK("Disable all runtime MessageBox popups", disable_msgbox, k_prefs_disable_msgbox);
    msgbox.key = "disable_msgbox";
    msgbox.on_click = [](CDlgPreferences* dialog) {
      if (dialog->CheckedOf("disable_msgbox")) {
        MessageBox_Interactive("Warning: Selecting this option instructs OpenHoldem to refrain from\n"
          "displaying ANY runtime informational or error message boxes.  Examples\n"
          "include parse errors, DLL load errors, etc.  It is strongly advised that\n"
          "this option only be used in a production environment that has been\n"
          "completely and thoroughly tested for proper behavior.\n\n"
          "Note that interactive messages are not disabled, such as when using the\n"
          "formula editor.", "WARNING", MB_OK);
      }
    };
    p.items.push_back(msgbox);
  }

  // ----- Handhistory Generator -----
  {
    PrefsPage& p = NewPage(&_pages, "Handhistory Generator");
    PrefsItem enable = PREF_CHECK("Enable handhistory generator", handhistory_generator_enable, k_prefs_handhistory_generator_enable);
    PrefsItem profile = PREF_CHECK("Also write HH + action profile (separate folder)", handhistory_profile, k_prefs_handhistory_profile);
    profile.after_apply = []() {
      if (p_handhistory_writer != NULL) {
        p_handhistory_writer->ReloadSettings();
      }
    };
    p.items.push_back(enable);
    p.items.push_back(Heading("Type of the games to record"));
    p.items.push_back(Radio({ "Cash game", "Tournament" },
      []() -> int { return Preferences()->handhistory_tournament() ? 1 : 0; },
      [](int index) { SetNumber(k_prefs_handhistory_tournament, index == 1 ? 1 : 0); }));
    p.items.push_back(profile);
  }

  // ----- Logging -----
  {
    PrefsPage& p = NewPage(&_pages, "Logging");
    p.items.push_back(Heading("Advanced Logging"));
    p.items.push_back(PREF_CHECK("Hopper Functions", log_hopper_functions, k_prefs_log_hopper_functions));
    p.items.push_back(PREF_CHECK("Ini Functions", log_ini_functions, k_prefs_log_ini_functions));
    p.items.push_back(PREF_CHECK("PrWin Functions", log_prwin_functions, k_prefs_log_prwin_functions));
    p.items.push_back(PREF_CHECK("ICM Functions", log_icm_functions, k_prefs_log_icm_functions));
    p.items.push_back(PREF_CHECK("Delay Function", log_delay_function, k_prefs_log_delay_function));
    p.items.push_back(Heading("Log files"));
    p.items.push_back(PREF_NUMBER("Erase old logs larger than (MB)", 0, 999, 1,
      log_max_logsize, k_prefs_log_max_logsize, "Invalid maximum log size"));
  }

  // ----- Mouse -----
  {
    PrefsPage& p = NewPage(&_pages, "Mouse");
    p.items.push_back(Heading("Mouse"));
    p.items.push_back(Button("Reset to defaults",
      [](CDlgPreferences* dialog) { dialog->ResetPageToDefaults("Mouse"); }, true));
    PrefsItem mode = WithDefault(Radio({ "Fast (instant jump to the target, reference behaviour)",
                                         "Human (curved path, variable speed and click timing)" },
      []() -> int { return Preferences()->mouse_mode() == 1 ? 1 : 0; },
      [](int index) { SetNumber(k_prefs_mouse_mode, index == 1 ? 1 : 0); }), 0);
    SendMouseSettingsAfterApply(&mode);
    p.items.push_back(mode);
    p.items.push_back(Note("Both mice are in the same mouse.dll. The settings below only apply to the human mouse. "
      "Changes take effect immediately after OK."));

    p.items.push_back(Heading("Movement"));
    {
      PrefsItem item = WithDefault(PREF_NUMBER("Speed (20-500 %, 100 = normal)", 20, 500, 10,
        mouse_speed_percent, k_prefs_mouse_speed_percent, "Invalid mouse speed (20-500)"), kMouseDefaultSpeedPercent);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }
    {
      PrefsItem item = WithDefault(PREF_NUMBER("Curvature of the path (0-300 %)", 0, 300, 10,
        mouse_curvature_percent, k_prefs_mouse_curvature_percent, "Invalid curvature (0-300)"), kMouseDefaultCurvaturePercent);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }
    {
      PrefsItem item = WithDefault(PREF_NUMBER("Hand tremor (0-300 %)", 0, 300, 10,
        mouse_noise_percent, k_prefs_mouse_noise_percent, "Invalid tremor (0-300)"), kMouseDefaultNoisePercent);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }
    {
      PrefsItem item = WithDefault(PREF_NUMBER("Overshoot probability on long moves (0-100 %)", 0, 100, 5,
        mouse_overshoot_percent, k_prefs_mouse_overshoot_percent, "Invalid overshoot probability (0-100)"), kMouseDefaultOvershootPercent);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }

    p.items.push_back(Heading("Click timing"));
    {
      PrefsItem item = WithDefault(PREF_NUMBER("Pause before the click, minimum (0-2000 ms)", 0, 2000, 10,
        mouse_pause_min_ms, k_prefs_mouse_pause_min_ms, "Invalid pause (0-2000)"), kMouseDefaultPauseMinMs);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }
    {
      PrefsItem item = WithDefault(PREF_NUMBER("Pause before the click, maximum (0-2000 ms)", 0, 2000, 10,
        mouse_pause_max_ms, k_prefs_mouse_pause_max_ms, "Invalid pause (0-2000)"), kMouseDefaultPauseMaxMs);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }
    {
      PrefsItem item = WithDefault(PREF_NUMBER("Click duration, minimum (10-500 ms)", 10, 500, 5,
        mouse_hold_min_ms, k_prefs_mouse_hold_min_ms, "Invalid click duration (10-500)"), kMouseDefaultHoldMinMs);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }
    {
      PrefsItem item = WithDefault(PREF_NUMBER("Click duration, maximum (10-500 ms)", 10, 500, 5,
        mouse_hold_max_ms, k_prefs_mouse_hold_max_ms, "Invalid click duration (10-500)"), kMouseDefaultHoldMaxMs);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }

    p.items.push_back(Heading("If you move the mouse yourself"));
    {
      PrefsItem item = WithDefault(PREF_CHECK("Wait until the mouse rests, then continue",
        mouse_yield_to_user, k_prefs_mouse_yield_to_user), kMouseDefaultYieldToUser);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }
    {
      PrefsItem item = WithDefault(PREF_NUMBER("Give up after (1-120 s)", 1, 120, 1,
        mouse_user_timeout_s, k_prefs_mouse_user_timeout_s, "Invalid timeout (1-120)"), kMouseDefaultUserTimeoutS);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }
    {
      PrefsItem item = WithDefault(PREF_NUMBER("Mouse has to rest for (200-5000 ms)", 200, 5000, 100,
        mouse_user_settle_ms, k_prefs_mouse_user_settle_ms, "Invalid resting time (200-5000)"), kMouseDefaultUserSettleMs);
      SendMouseSettingsAfterApply(&item);
      p.items.push_back(item);
    }
    p.items.push_back(Note("If the mouse does not rest within the timeout, the action is skipped and "
      "the autoplayer tries again in the next heartbeat."));
  }

  // ----- Poker Tracker v4 -----
  {
    PrefsPage& p = NewPage(&_pages, "Poker Tracker v4");
    p.items.push_back(Heading("PostgreSQL database"));
    p.items.push_back(PREF_TEXT("Host Name / IP Address", "pt_ip", pt_ip_addr, k_prefs_pt_ip_addr));
    p.items.push_back(PREF_TEXT("Port", "pt_port", pt_port, k_prefs_pt_port));
    p.items.push_back(PREF_TEXT("Username", "pt_user", pt_user, k_prefs_pt_user));
    p.items.push_back(PREF_TEXT("Password", "pt_pass", pt_pass, k_prefs_pt_pass));
    p.items.push_back(PREF_TEXT("DB Name", "pt_dbname", pt_dbname, k_prefs_pt_dbname));
    p.items.push_back(Button("Test connection", &TestPokerTrackerConnection, false));
  }

  // ----- Popup Blocker -----
  {
    PrefsPage& p = NewPage(&_pages, "Popup Blocker");
    p.items.push_back(Radio({ "Disabled", "Minimize Popups", "Kill Popups" },
      []() -> int {
        if (Preferences()->popup_blocker() == k_popup_minimize) return 1;
        if (Preferences()->popup_blocker() == k_popup_kill) return 2;
        return 0;
      },
      [](int index) {
        SetNumber(k_prefs_popup_blocker,
          index == 1 ? k_popup_minimize : (index == 2 ? k_popup_kill : k_popup_disabled));
      }));
    p.items.push_back(Note("You need one minimalistic \"tablemap\" and one instance of OpenHoldem for every "
      "window that shall stay alive (lobby, login-window, ...)."));
    p.items.push_back(Note("You need one more unoccupied instance of OpenHoldem to handle popups."));
    p.items.push_back(Note("Existing windows will be minimized on startup, not killed."));
    p.items.push_back(Note("OpenHoldem windows will be ignored. See Preferences -> GUI."));
  }

  // ----- Rebuy -----
  {
    PrefsPage& p = NewPage(&_pages, "Rebuy");
    p.items.push_back(Heading("Safety measures"));
    p.items.push_back(PREF_CHECK("Change in handnumber", rebuy_condition_change_in_handnumber,
      k_prefs_rebuy_condition_change_in_handnumber));
    p.items.push_back(PREF_CHECK("No cards", rebuy_condition_no_cards, k_prefs_rebuy_condition_no_cards));
    p.items.push_back(PREF_CHECK("Heuristic occlusion-check", rebuy_condition_heuristic_check_for_occlusion,
      k_prefs_rebuy_condition_heuristic_check_for_occlusion));
    p.items.push_back(PREF_NUMBER("Minimum time to next rebuy (sec)", 0, kMaximumRebuyDelay, 1,
      rebuy_minimum_time_to_next_try, k_prefs_rebuy_minimum_time_to_next_try,
      "Invalid minimum time to next rebuy"));
    p.items.push_back(Heading("Script"));
    p.items.push_back(PREF_TEXT("Rebuy-script", "rebuy_script", rebuy_script, k_prefs_rebuy_script));
  }

  // ----- Replay Frames -----
  {
    PrefsPage& p = NewPage(&_pages, "Replay Frames");
    p.items.push_back(Heading("Shoot replay-frames"));
    p.items.push_back(Radio({ "Never", "On autoplayer action (my turn)",
        "On change in table state when in hand", "On change in table state" },
      []() -> int {
        // Values start with 3 to avoid confusion with old values (on off), default: off
        switch (Preferences()->replay_record()) {
          case kShootReplyFramesOnMyTurn: return 1;
          case kShootReplyFramesOnEveryChangeWhilePlaying: return 2;
          case kShootReplyFramesOnEveryChange: return 3;
          default: return 0;
        }
      },
      [](int index) {
        const int values[4] = { kShootReplyFramesNever, kShootReplyFramesOnMyTurn,
          kShootReplyFramesOnEveryChangeWhilePlaying, kShootReplyFramesOnEveryChange };
        SetNumber(k_prefs_replay_record, values[index < 0 ? 0 : (index > 3 ? 3 : index)]);
      }));
    p.items.push_back(PREF_NUMBER("Maximum frames to store", 1, 1000, 1,
      replay_max_frames, k_prefs_replay_max_frames, "Invalid maximum number of replay-frames (1-1000)"));
    p.items.push_back(Note("If you want to shoot replay-frames for a completely new casino you will need at least "
      "a very minimalistic tablemap with correct sizes and title-string to connect to the table."));
    p.items.push_back(Note("Replay-frames are a combination of images (bitmaps) and HTML-files with additional info. "
      "If you get asked for replay-frames for trouble-shooting please include both."));
  }

  // ----- Scraper -----
  {
    PrefsPage& p = NewPage(&_pages, "Scraper");
    p.items.push_back(PREF_NUMBER("Scrape delay (100-5000 ms)", MIN_SCRAPEDELAY, MAX_SCRAPEDELAY, 100,
      scrape_delay, k_prefs_scrape_delay, "Invalid Scrape Delay"));
    p.items.push_back(Heading("Unwanted casino animations on player names"));
    p.items.push_back(Note("One entry per line (put unwanted strings to scrape or regex arguments)."));
    PrefsItem lines;
    lines.kind = PrefsItem::kLines;
    lines.get_text = &UnwantedScrapeAsLines;
    lines.set_text = &SaveUnwantedScrape;
    lines.validate_text = &ValidateUnwantedScrape;
    p.items.push_back(lines);
  }

  // ----- Stealth -----
  {
    PrefsPage& p = NewPage(&_pages, "Stealth");
    PrefsItem title = PREF_CHECK("Simple Main Window Title (Name of Executable)", simple_window_title,
      k_prefs_simple_window_title);
    title.after_apply = []() {
      if (p_openholdem_title != NULL) {
        p_openholdem_title->UpdateTitle();
      }
    };
    p.items.push_back(title);
    p.items.push_back(PREF_TEXT("Mutex (Synchronization)", "mutex", mutex_name, k_prefs_mutex_name));
    p.items.push_back(PREF_TEXT("Window Class Name", "class_name", window_class_name, k_prefs_window_class_name));
  }

  // ----- Table Positioner -----
  {
    PrefsPage& p = NewPage(&_pages, "Table Positioner");
    p.items.push_back(Heading("Table Positioner"));
    p.items.push_back(Radio({ "Tiled", "Cascading", "Never" },
      []() -> int {
        if (Preferences()->table_positioner_options() == k_position_tables_tiled) return 0;
        if (Preferences()->table_positioner_options() == k_position_tables_cascaded) return 1;
        return 2;
      },
      [](int index) {
        SetNumber(k_prefs_table_positioner_options,
          index == 0 ? k_position_tables_tiled : (index == 1 ? k_position_tables_cascaded : k_position_tables_never));
      }));
    p.items.push_back(PREF_CHECK("Always keep position", table_positioner_always_keep_position,
      k_prefs_table_positioner_always_keep_position));
    p.items.push_back(Note("Some tables might be moved to the very bottom of the screen, therefore it is "
      "recommended to minimize the Windows taskbar."));
  }

  // ----- Validator -----
  {
    PrefsPage& p = NewPage(&_pages, "Validator");
    p.items.push_back(PREF_CHECK("Enable validator on my turn", validator_enabled, k_prefs_validator_enabled));
    p.items.push_back(PREF_CHECK("Use heuristic rules", validator_use_heuristic_rules, k_prefs_validator_use_heuristic_rules));
    p.items.push_back(PREF_CHECK("Stop autoplayer on error", validator_stop_on_error, k_prefs_validator_stop_on_error));
    p.items.push_back(PREF_CHECK("Shoot replay-frames on error", validator_shoot_replayframe_on_error,
      k_prefs_validator_shoot_replayframe_on_error));
    p.items.push_back(Note("The validator is a tool that tests the combination of\n"
      "  * scraper\n"
      "  * symbol engine\n"
      "  * tablemap\n"
      "to detect incosistencies, usually caused by bad tablemaps."));
  }
}

// ---------------------------------------------------------------------------
// The dialog
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CDlgPreferences, CDialog)
  ON_MESSAGE(WMU_PREFS_SELECT, &CDlgPreferences::OnSelectPage)
  ON_MESSAGE(modernui::WMU_THEME_CHANGED, &CDlgPreferences::OnThemeChanged)
  ON_WM_ERASEBKGND()
  ON_WM_CTLCOLOR()
  ON_WM_DESTROY()
  ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

CDlgPreferences::CDlgPreferences(CWnd* pParent /*=NULL*/)
    : CDialog(CDlgPreferences::IDD, pParent) {
  _current_page = 0;
  _content_rect = CRect(0, 0, 0, 0);
  // GDI+ is needed by all themed controls
  modernui::GdiplusAcquire();
}

CDlgPreferences::~CDlgPreferences() {
  _owned.clear();
  modernui::GdiplusRelease();
}

CWnd* CDlgPreferences::Own(CWnd* window) {
  _owned.push_back(std::unique_ptr<CWnd>(window));
  return window;
}

BOOL CDlgPreferences::OnInitDialog() {
  CDialog::OnInitDialog();
  ModifyStyle(0, WS_CLIPCHILDREN);
  SetWindowText("OpenHoldem Preferences");

  const float s = modernui::DpiScale(m_hWnd);
  _font.CreateFont(-(int)(13 * s + 0.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
  _font_bold.CreateFont(-(int)(14 * s + 0.5f), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
  _font_title.CreateFont(-(int)(22 * s + 0.5f), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");

  BuildPages();
  LayoutDialog();   // sizes the window, creates sidebar, title and buttons

  for (int i = 0; i < (int)_pages.size(); ++i) {
    CreateControls(i);
    LayoutPage(i);
  }
  LoadValues();

  modernui::Theme::RegisterWindow(m_hWnd);
  ApplyTheme();
  ShowPage(0);
  CenterWindow();
  _sidebar.SetFocus();
  return FALSE;   // we have set the focus ourselves
}

BOOL CDlgPreferences::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) {
  // The wheel scrolls what is under the mouse, not what has the focus
  static bool forwarding = false;     // a control may hand the wheel back to us
  if (!forwarding) {
    HWND target = ::WindowFromPoint(pt);
    if (target != NULL && target != m_hWnd && ::IsChild(m_hWnd, target)) {
      forwarding = true;
      // Controls hand the wheel to their parent (the page) if they do not use it themselves
      ::SendMessage(target, WM_MOUSEWHEEL, MAKEWPARAM(nFlags, zDelta), MAKELPARAM(pt.x, pt.y));
      forwarding = false;
      return TRUE;
    }
  }
  return CDialog::OnMouseWheel(nFlags, zDelta, pt);
}

void CDlgPreferences::OnDestroy() {
  modernui::Theme::UnregisterWindow(m_hWnd);
  CDialog::OnDestroy();
}

void CDlgPreferences::LayoutDialog() {
  const float s = modernui::DpiScale(m_hWnd);
  auto px = [s](float value) { return (int)(value * s + 0.5f); };
  const int sidebar_width = px(210), pad = px(28), content_width = px(600);
  const int title_height = px(44), button_height = px(34), gap = px(16);
  const int pad_top = px(22), pad_bottom = px(22);

  // The dialog must fit on small screens
  CRect work;
  ::SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
  int content_height = px(470);
  const int overhead = pad_top + title_height + gap + button_height + pad_bottom
    + GetSystemMetrics(SM_CYCAPTION) + 2 * GetSystemMetrics(SM_CYFIXEDFRAME);
  if (content_height > work.Height() - overhead) content_height = work.Height() - overhead;
  if (content_height < px(300)) content_height = px(300);

  const int client_width = sidebar_width + pad + content_width + pad;
  const int client_height = pad_top + title_height + content_height + gap + button_height + pad_bottom;
  CRect frame(0, 0, client_width, client_height);
  ::AdjustWindowRectEx(&frame, GetStyle(), FALSE, GetExStyle());
  SetWindowPos(NULL, 0, 0, frame.Width(), frame.Height(), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

  // Category list
  std::vector<CString> titles;
  for (size_t i = 0; i < _pages.size(); ++i) titles.push_back(_pages[i].title);
  _sidebar.CreateSidebar(this, CRect(0, 0, sidebar_width, client_height), 3999);
  _sidebar.SetTitles(titles);

  // Title of the current category and the area of the pages
  const int content_left = sidebar_width + pad;
  _title.Create("", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE,
    CRect(content_left, pad_top, content_left + content_width, pad_top + title_height), this, 3998);
  _title.SetFont(&_font_title);
  _content_rect = CRect(content_left, pad_top + title_height,
    content_left + content_width, pad_top + title_height + content_height);

  // Buttons
  const int button_width = px(96);
  const int button_top = client_height - pad_bottom - button_height;
  const int right = content_left + content_width;
  _button_cancel.Create("Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
    CRect(right - button_width, button_top, right, button_top + button_height), this, IDCANCEL);
  _button_ok.Create("OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
    CRect(right - 2 * button_width - px(8), button_top, right - button_width - px(8), button_top + button_height),
    this, IDOK);
  _button_ok.SetPrimary(true);
}

void CDlgPreferences::CreateControls(int page_index) {
  PrefsPage& page = _pages[page_index];
  modernglue::CPrefsPageWnd* window = new modernglue::CPrefsPageWnd();
  Own(window);
  window->CreatePage(this, this, _content_rect);
  page.window = window;

  const DWORD child = WS_CHILD | WS_VISIBLE;
  const CRect nowhere(0, 0, 10, 10);
  for (int i = 0; i < (int)page.items.size(); ++i) {
    PrefsItem& item = page.items[i];
    switch (item.kind) {
      case PrefsItem::kHeading:
      case PrefsItem::kNote: {
        CStatic* text = new CStatic();
        Own(text);
        text->Create(item.label, child | SS_LEFT | SS_NOPREFIX, nowhere, window);
        text->SetFont(item.kind == PrefsItem::kHeading ? &_font_bold : &_font);
        ::SetWindowLongPtr(text->GetSafeHwnd(), GWLP_USERDATA, item.kind == PrefsItem::kNote ? 1 : 2);
        item.ctrl = text;
        break;
      }
      case PrefsItem::kCheck: {
        item.first_id = kFirstControlId + (int)_refs.size();
        CtrlRef ref = { page_index, i, 0 };
        _refs.push_back(ref);
        modernglue::CModernCheck* check = new modernglue::CModernCheck();
        Own(check);
        check->Create(item.label, child | WS_TABSTOP | BS_AUTOCHECKBOX | (item.disabled ? WS_DISABLED : 0),
          nowhere, window, item.first_id);
        item.ctrl = check;
        break;
      }
      case PrefsItem::kNumber:
      case PrefsItem::kText: {
        CStatic* label = new CStatic();
        Own(label);
        label->Create(item.label, child | SS_LEFT | SS_NOPREFIX | SS_CENTERIMAGE, nowhere, window);
        label->SetFont(&_font);
        item.label_ctrl = label;
        item.first_id = kFirstControlId + (int)_refs.size();
        CtrlRef ref = { page_index, i, 0 };
        _refs.push_back(ref);
        CEdit* edit = new CEdit();
        Own(edit);
        edit->CreateEx(WS_EX_CLIENTEDGE, "EDIT", "",
          child | WS_TABSTOP | ES_AUTOHSCROLL | (item.kind == PrefsItem::kNumber ? ES_NUMBER : 0),
          nowhere, window, item.first_id);
        edit->SetFont(&_font);
        modernglue::ApplyControlTheme(edit->GetSafeHwnd());
        item.ctrl = edit;
        break;
      }
      case PrefsItem::kLines: {
        item.first_id = kFirstControlId + (int)_refs.size();
        CtrlRef ref = { page_index, i, 0 };
        _refs.push_back(ref);
        CEdit* edit = new CEdit();
        Own(edit);
        edit->CreateEx(WS_EX_CLIENTEDGE, "EDIT", "",
          child | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
          nowhere, window, item.first_id);
        edit->SetFont(&_font);
        modernglue::ApplyControlTheme(edit->GetSafeHwnd());
        item.ctrl = edit;
        break;
      }
      case PrefsItem::kRadio: {
        item.first_id = kFirstControlId + (int)_refs.size();
        for (int k = 0; k < (int)item.options.size(); ++k) {
          CtrlRef ref = { page_index, i, k + 1 };
          _refs.push_back(ref);
          const bool disabled = (item.disabled_options & (1u << k)) != 0;
          modernglue::CModernCheck* radio = new modernglue::CModernCheck();
          Own(radio);
          radio->Create(item.options[k], child | WS_TABSTOP | BS_RADIOBUTTON | (disabled ? WS_DISABLED : 0),
            nowhere, window, item.first_id + k);
          item.radios.push_back(radio);
        }
        break;
      }
      case PrefsItem::kButton: {
        item.first_id = kFirstControlId + (int)_refs.size();
        CtrlRef ref = { page_index, i, 0 };
        _refs.push_back(ref);
        modernglue::CModernButton* button = new modernglue::CModernButton();
        Own(button);
        button->Create(item.label, child | WS_TABSTOP | BS_PUSHBUTTON, nowhere, window, item.first_id);
        item.ctrl = button;
        break;
      }
    }
  }
}

void CDlgPreferences::LayoutPage(int page_index) {
  PrefsPage& page = _pages[page_index];
  const float s = modernui::DpiScale(m_hWnd);
  auto px = [s](float value) { return (int)(value * s + 0.5f); };
  // The right part is reserved for the scrollbar of the page
  const int width = _content_rect.Width() - ::GetSystemMetrics(SM_CXVSCROLL) - px(4);
  int y = 0, last_row_y = 0;

  CClientDC dc(page.window);
  CFont* old_font = dc.SelectObject(&_font);

  size_t i = 0;
  while (i < page.items.size()) {
    PrefsItem& item = page.items[i];
    switch (item.kind) {
      case PrefsItem::kHeading:
        if (i > 0) y += px(14);
        item.ctrl->MoveWindow(0, y, width, px(24));
        last_row_y = y;
        y += px(32);
        ++i;
        break;
      case PrefsItem::kNote: {
        CRect measured(0, 0, width, 0);
        dc.DrawText(item.label, &measured, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        const int height = measured.Height() + px(4);
        item.ctrl->MoveWindow(0, y, width, height);
        y += height + px(10);
        ++i;
        break;
      }
      case PrefsItem::kCheck: {
        if (page.columns > 1) {
          // A run of check boxes: columns, filled top to bottom
          size_t end = i;
          while (end < page.items.size() && page.items[end].kind == PrefsItem::kCheck) ++end;
          const int count = (int)(end - i);
          const int rows = (count + page.columns - 1) / page.columns;
          const int cell_width = width / page.columns;
          const int row_height = px((float)page.row_height);
          for (int k = 0; k < count; ++k) {
            page.items[i + k].ctrl->MoveWindow((k / rows) * cell_width, y + (k % rows) * row_height,
              cell_width - px(6), row_height);
          }
          y += rows * row_height + px(6);
          i = end;
        } else {
          item.ctrl->MoveWindow(0, y, width, px(28));
          y += px(30);
          ++i;
        }
        break;
      }
      case PrefsItem::kNumber: {
        const int edit_width = px(90);
        item.label_ctrl->MoveWindow(0, y, width - edit_width - px(16), px(28));
        item.ctrl->MoveWindow(width - edit_width, y + px(1), edit_width, px(26));
        y += px(36);
        ++i;
        break;
      }
      case PrefsItem::kText: {
        const int label_width = width * 38 / 100;
        item.label_ctrl->MoveWindow(0, y, label_width - px(8), px(28));
        item.ctrl->MoveWindow(label_width, y + px(1), width - label_width, px(26));
        y += px(36);
        ++i;
        break;
      }
      case PrefsItem::kLines:
        item.ctrl->MoveWindow(0, y, width, px(190));
        y += px(198);
        ++i;
        break;
      case PrefsItem::kRadio:
        for (size_t k = 0; k < item.radios.size(); ++k) {
          item.radios[k]->MoveWindow(0, y + (int)k * px(30), width, px(28));
        }
        y += (int)item.radios.size() * px(30) + px(6);
        ++i;
        break;
      case PrefsItem::kButton:
        if (item.same_row) {
          item.ctrl->MoveWindow(width - px(120), last_row_y - px(2), px(120), px(28));
        } else {
          item.ctrl->MoveWindow(0, y + px(4), px(150), px(32));
          y += px(44);
        }
        ++i;
        break;
    }
  }
  dc.SelectObject(old_font);
  static_cast<modernglue::CPrefsPageWnd*>(page.window)->SetContentHeight(y + px(8));
}

void CDlgPreferences::ShowPage(int page_index) {
  if (page_index < 0 || page_index >= (int)_pages.size()) return;
  for (int i = 0; i < (int)_pages.size(); ++i) {
    if (_pages[i].window != NULL) {
      _pages[i].window->ShowWindow(i == page_index ? SW_SHOW : SW_HIDE);
    }
  }
  _current_page = page_index;
  _title.SetWindowText(_pages[page_index].title);
  if (_sidebar.Selected() != page_index) _sidebar.Select(page_index, false);
}

LRESULT CDlgPreferences::OnSelectPage(WPARAM wParam, LPARAM) {
  ShowPage((int)wParam);
  return 0;
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

bool CDlgPreferences::GetCheck(const PrefsItem& item) const {
  return static_cast<CButton*>(item.ctrl)->GetCheck() == BST_CHECKED;
}

CString CDlgPreferences::GetText(const PrefsItem& item) const {
  CString text;
  item.ctrl->GetWindowText(text);
  return text;
}

void CDlgPreferences::SetRadio(PrefsItem& item, int index) {
  for (int k = 0; k < (int)item.radios.size(); ++k) {
    static_cast<CButton*>(item.radios[k])->SetCheck(k == index ? BST_CHECKED : BST_UNCHECKED);
  }
}

int CDlgPreferences::GetRadio(const PrefsItem& item) const {
  for (int k = 0; k < (int)item.radios.size(); ++k) {
    if (static_cast<CButton*>(item.radios[k])->GetCheck() == BST_CHECKED) return k;
  }
  return -1;
}

void CDlgPreferences::LoadValues() {
  for (size_t p = 0; p < _pages.size(); ++p) {
    for (size_t i = 0; i < _pages[p].items.size(); ++i) {
      PrefsItem& item = _pages[p].items[i];
      switch (item.kind) {
        case PrefsItem::kCheck:
          static_cast<CButton*>(item.ctrl)->SetCheck(item.get_bool() ? BST_CHECKED : BST_UNCHECKED);
          break;
        case PrefsItem::kNumber: {
          CString text;
          text.Format("%d", item.get_int());
          item.ctrl->SetWindowText(text);
          break;
        }
        case PrefsItem::kText:
        case PrefsItem::kLines:
          item.ctrl->SetWindowText(item.get_text());
          break;
        case PrefsItem::kRadio:
          SetRadio(item, item.get_int());
          break;
        default:
          break;
      }
    }
  }
}

bool CDlgPreferences::Validate() {
  for (size_t p = 0; p < _pages.size(); ++p) {
    for (size_t i = 0; i < _pages[p].items.size(); ++i) {
      PrefsItem& item = _pages[p].items[i];
      CString error;
      if (item.kind == PrefsItem::kNumber) {
        const long value = strtol(GetText(item).GetString(), NULL, 10);
        if (value < item.min_value || value > item.max_value) error = item.error_text;
      } else if ((item.kind == PrefsItem::kText || item.kind == PrefsItem::kLines) && item.validate_text) {
        error = item.validate_text(GetText(item));
      }
      if (!error.IsEmpty()) {
        // Show the page with the problem and put the cursor into the field
        ShowPage((int)p);
        MessageBox_Interactive(error.GetString(), "ERROR", MB_OK);
        item.ctrl->SetFocus();
        static_cast<modernglue::CPrefsPageWnd*>(_pages[p].window)->EnsureVisible(item.ctrl);
        return false;
      }
    }
  }
  return true;
}

void CDlgPreferences::ApplyValues() {
  for (size_t p = 0; p < _pages.size(); ++p) {
    for (size_t i = 0; i < _pages[p].items.size(); ++i) {
      PrefsItem& item = _pages[p].items[i];
      switch (item.kind) {
        case PrefsItem::kCheck:
          if (!item.disabled) item.set_bool(GetCheck(item));
          break;
        case PrefsItem::kNumber:
          item.set_int((int)strtol(GetText(item).GetString(), NULL, 10));
          break;
        case PrefsItem::kText:
        case PrefsItem::kLines:
          item.set_text(GetText(item));
          break;
        case PrefsItem::kRadio: {
          const int index = GetRadio(item);
          if (index >= 0) item.set_int(index);
          break;
        }
        default:
          break;
      }
    }
  }
  // Things that have to happen after the values got saved
  for (size_t p = 0; p < _pages.size(); ++p) {
    for (size_t i = 0; i < _pages[p].items.size(); ++i) {
      if (_pages[p].items[i].after_apply) _pages[p].items[i].after_apply();
    }
  }
}

void CDlgPreferences::OnOK() {
  // Validate everything first, so that nothing gets half-saved
  if (!Validate()) return;
  ApplyValues();
  CDialog::OnOK();
}

PrefsItem* CDlgPreferences::FindItem(const char* key) {
  for (size_t p = 0; p < _pages.size(); ++p) {
    for (size_t i = 0; i < _pages[p].items.size(); ++i) {
      if (_pages[p].items[i].key == key) return &_pages[p].items[i];
    }
  }
  return NULL;
}

CString CDlgPreferences::TextOf(const char* key) {
  PrefsItem* item = FindItem(key);
  return (item != NULL && item->ctrl != NULL) ? GetText(*item) : CString("");
}

bool CDlgPreferences::CheckedOf(const char* key) {
  PrefsItem* item = FindItem(key);
  return (item != NULL && item->ctrl != NULL) ? GetCheck(*item) : false;
}

void CDlgPreferences::ResetPageToDefaults(const char* title) {
  for (size_t p = 0; p < _pages.size(); ++p) {
    if (_pages[p].title != title) continue;
    for (size_t i = 0; i < _pages[p].items.size(); ++i) {
      PrefsItem& item = _pages[p].items[i];
      if (!item.has_default || (item.ctrl == NULL && item.radios.empty())) continue;
      switch (item.kind) {
        case PrefsItem::kCheck:
          static_cast<CButton*>(item.ctrl)->SetCheck(item.default_value ? BST_CHECKED : BST_UNCHECKED);
          break;
        case PrefsItem::kNumber: {
          CString text;
          text.Format("%d", item.default_value);
          item.ctrl->SetWindowText(text);
          break;
        }
        case PrefsItem::kRadio:
          SetRadio(item, item.default_value);
          break;
        default:
          break;
      }
    }
  }
}

void CDlgPreferences::ToggleChecksOfPage(const char* title) {
  for (size_t p = 0; p < _pages.size(); ++p) {
    if (_pages[p].title != title) continue;
    for (size_t i = 0; i < _pages[p].items.size(); ++i) {
      PrefsItem& item = _pages[p].items[i];
      if (item.kind == PrefsItem::kCheck && !item.disabled) {
        static_cast<CButton*>(item.ctrl)->SetCheck(GetCheck(item) ? BST_UNCHECKED : BST_CHECKED);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Commands, theme
// ---------------------------------------------------------------------------

void CDlgPreferences::OnItemClicked(UINT id) {
  const int index = (int)id - kFirstControlId;
  if (index < 0 || index >= (int)_refs.size()) return;
  const CtrlRef& ref = _refs[index];
  PrefsItem& item = _pages[ref.page].items[ref.item];
  if (item.kind == PrefsItem::kRadio) {
    SetRadio(item, ref.sub - 1);
  } else if (item.kind == PrefsItem::kCheck || item.kind == PrefsItem::kButton) {
    if (item.on_click) item.on_click(this);
  }
}

BOOL CDlgPreferences::OnCommand(WPARAM wParam, LPARAM lParam) {
  const UINT id = LOWORD(wParam);
  if (HIWORD(wParam) == BN_CLICKED && (int)id >= kFirstControlId
      && (int)id < kFirstControlId + (int)_refs.size()) {
    OnItemClicked(id);
    return TRUE;
  }
  return CDialog::OnCommand(wParam, lParam);
}

BOOL CDlgPreferences::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  return CDialog::OnNotify(wParam, lParam, pResult);
}

void CDlgPreferences::ApplyTheme() {
  const modernui::Palette& c = modernui::Theme::Colors();
  modernui::Theme::ApplyFrameAttributes(m_hWnd);
  _brush_window.DeleteObject();
  _brush_window.CreateSolidBrush(modernui::ToColorRef(c.window_bg));
  for (size_t p = 0; p < _pages.size(); ++p) {
    modernglue::CPrefsPageWnd* window = static_cast<modernglue::CPrefsPageWnd*>(_pages[p].window);
    if (window != NULL) window->ApplyTheme();
    for (size_t i = 0; i < _pages[p].items.size(); ++i) {
      PrefsItem& item = _pages[p].items[i];
      if (item.kind == PrefsItem::kText || item.kind == PrefsItem::kNumber || item.kind == PrefsItem::kLines) {
        modernglue::ApplyControlTheme(item.ctrl->GetSafeHwnd());
      }
    }
  }
}

LRESULT CDlgPreferences::OnThemeChanged(WPARAM, LPARAM) {
  ApplyTheme();
  RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
  return 0;
}

BOOL CDlgPreferences::OnEraseBkgnd(CDC* pDC) {
  CRect client;
  GetClientRect(&client);
  pDC->FillSolidRect(&client, modernui::ToColorRef(modernui::Theme::Colors().window_bg));
  return TRUE;
}

HBRUSH CDlgPreferences::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
  if (_brush_window.GetSafeHandle() == NULL) {
    return CDialog::OnCtlColor(pDC, pWnd, nCtlColor);
  }
  const modernui::Palette& c = modernui::Theme::Colors();
  pDC->SetTextColor(modernui::ToColorRef(c.text));
  pDC->SetBkColor(modernui::ToColorRef(c.window_bg));
  return (HBRUSH)_brush_window.GetSafeHandle();
}

// ---------------------------------------------------------------------------
// The list of categories
// ---------------------------------------------------------------------------

namespace modernglue {

BEGIN_MESSAGE_MAP(CPrefsSidebar, CWnd)
  ON_WM_PAINT()
  ON_WM_ERASEBKGND()
  ON_WM_LBUTTONDOWN()
  ON_WM_MOUSEMOVE()
  ON_MESSAGE(WM_MOUSELEAVE, &CPrefsSidebar::OnMouseLeave)
  ON_WM_KEYDOWN()
  ON_WM_GETDLGCODE()
  ON_WM_SETFOCUS()
  ON_WM_KILLFOCUS()
  ON_WM_MOUSEWHEEL()
END_MESSAGE_MAP()

BOOL CPrefsSidebar::CreateSidebar(CWnd* parent, const CRect& rect, UINT id) {
  LPCTSTR class_name = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
  return CWnd::CreateEx(0, class_name, "", WS_CHILD | WS_VISIBLE | WS_TABSTOP, rect, parent, id);
}

float CPrefsSidebar::TopOffset(float scale) const {
  return 58.0f * scale;       // below the "Preferences" heading
}

float CPrefsSidebar::RowHeight(float scale) const {
  // Rows shrink a bit so that all categories fit; the list scrolls if still too long
  CRect client;
  GetClientRect(&client);
  float row = 36.0f * scale;
  if (!_titles.empty()) {
    const float available = (float)client.Height() - TopOffset(scale) - 8.0f * scale;
    const float fitting = available / (float)_titles.size();
    if (fitting < row) row = fitting;
  }
  if (row < 26.0f * scale) row = 26.0f * scale;
  return row;
}

float CPrefsSidebar::MaxScroll(float scale) const {
  CRect client;
  GetClientRect(&client);
  const float total = TopOffset(scale) + RowHeight(scale) * (float)_titles.size() + 8.0f * scale;
  const float max_scroll = total - (float)client.Height();
  return max_scroll > 0.0f ? max_scroll : 0.0f;
}

void CPrefsSidebar::ScrollTo(float pixels) {
  const float s = DpiScale(m_hWnd);
  if (pixels > MaxScroll(s)) pixels = MaxScroll(s);
  if (pixels < 0.0f) pixels = 0.0f;
  if (pixels != _scroll) {
    _scroll = pixels;
    if (m_hWnd) Invalidate(FALSE);
  }
}

void CPrefsSidebar::EnsureSelectedVisible() {
  if (m_hWnd == NULL) return;
  const float s = DpiScale(m_hWnd);
  CRect client;
  GetClientRect(&client);
  const float top = _selected * RowHeight(s);                  // relative to the list
  const float bottom = top + RowHeight(s);
  const float visible = (float)client.Height() - TopOffset(s);
  if (top < _scroll) ScrollTo(top);
  else if (bottom > _scroll + visible - 8.0f * s) ScrollTo(bottom - visible + 8.0f * s);
}

BOOL CPrefsSidebar::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) {
  const float s = DpiScale(m_hWnd);
  ScrollTo(_scroll - (float)zDelta / (float)WHEEL_DELTA * RowHeight(s) * 2.0f);
  return TRUE;
}

void CPrefsSidebar::Select(int index, bool notify) {
  if (index < 0 || index >= (int)_titles.size()) return;
  _selected = index;
  EnsureSelectedVisible();
  if (m_hWnd) Invalidate(FALSE);
  if (notify && GetParent() != NULL) {
    GetParent()->SendMessage(WMU_PREFS_SELECT, (WPARAM)index, 0);
  }
}

int CPrefsSidebar::HitTest(CPoint point) const {
  const float s = DpiScale(m_hWnd);
  if ((float)point.y < TopOffset(s)) return -1;
  const float y = (float)point.y - TopOffset(s) + _scroll;
  const int index = (int)(y / RowHeight(s));
  return (index >= 0 && index < (int)_titles.size()) ? index : -1;
}

void CPrefsSidebar::OnPaint() {
  BufferedPaint paint(m_hWnd);
  Graphics& g = paint.G();
  const Palette& c = Theme::Colors();
  const float s = DpiScale(m_hWnd);
  const float w = (float)paint.Width(), h = (float)paint.Height();

  SolidBrush background(Rgba(c.surface));
  g.FillRectangle(&background, RectF(0, 0, w, h));
  Pen border(Rgba(c.border), 1.0f);

  const bool focused = (::GetFocus() == m_hWnd);
  const float row_height = RowHeight(s);
  for (int i = 0; i < (int)_titles.size(); ++i) {
    const RectF r(10 * s, TopOffset(s) + i * row_height - _scroll, w - 21 * s, row_height - 4 * s);
    if (r.Y + r.Height < TopOffset(s) || r.Y > h) continue;
    DWORD text = c.text_dim;
    if (i == _selected) {
      FillRoundRect(g, r, 8 * s, Rgba(c.accent_soft));
      if (focused) StrokeRoundRect(g, r, 8 * s, WithAlpha(c.accent, 0x90), 1.2f);
      text = c.accent;
    } else if (i == _hot) {
      FillRoundRect(g, r, 8 * s, Rgba(c.surface_hover));
      text = c.text;
    }
    DrawLabel(g, ToWide((LPCTSTR)_titles[i]), modernui::GetFont(13.5f * s, i == _selected),
              RectF(r.X + 14 * s, r.Y, r.Width - 20 * s, r.Height), Rgba(text), HAlign::Left, true);
  }

  // The heading covers the rows that scrolled underneath it
  g.FillRectangle(&background, RectF(0, 0, w - 1.0f, TopOffset(s) - 4 * s));
  DrawLabel(g, L"Preferences", modernui::GetFont(17.0f * s, true), RectF(22 * s, 14 * s, w - 44 * s, 32 * s),
            Rgba(c.text), HAlign::Left, true);
  g.DrawLine(&border, w - 0.5f, 0.0f, w - 0.5f, h);
}

void CPrefsSidebar::OnLButtonDown(UINT nFlags, CPoint point) {
  SetFocus();
  const int index = HitTest(point);
  if (index >= 0) Select(index, true);
  CWnd::OnLButtonDown(nFlags, point);
}

void CPrefsSidebar::OnMouseMove(UINT nFlags, CPoint point) {
  if (!_tracking) {
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, m_hWnd, 0 };
    TrackMouseEvent(&tme);
    _tracking = true;
  }
  const int hot = HitTest(point);
  if (hot != _hot) { _hot = hot; Invalidate(FALSE); }
  CWnd::OnMouseMove(nFlags, point);
}

LRESULT CPrefsSidebar::OnMouseLeave(WPARAM, LPARAM) {
  _tracking = false;
  if (_hot != -1) { _hot = -1; Invalidate(FALSE); }
  return 0;
}

UINT CPrefsSidebar::OnGetDlgCode() {
  return DLGC_WANTARROWS;
}

void CPrefsSidebar::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
  switch (nChar) {
    case VK_UP:   case VK_LEFT:  Select(_selected - 1, true); return;
    case VK_DOWN: case VK_RIGHT: Select(_selected + 1, true); return;
    case VK_HOME: Select(0, true); return;
    case VK_END:  Select((int)_titles.size() - 1, true); return;
  }
  CWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CPrefsSidebar::OnSetFocus(CWnd* pOld)  { CWnd::OnSetFocus(pOld);  Invalidate(FALSE); }
void CPrefsSidebar::OnKillFocus(CWnd* pNew) { CWnd::OnKillFocus(pNew); Invalidate(FALSE); }

// ---------------------------------------------------------------------------
// The window that holds the controls of one page
// ---------------------------------------------------------------------------

BEGIN_MESSAGE_MAP(CPrefsPageWnd, CWnd)
  ON_WM_ERASEBKGND()
  ON_WM_CTLCOLOR()
  ON_WM_VSCROLL()
  ON_WM_MOUSEWHEEL()
  ON_WM_SIZE()
END_MESSAGE_MAP()

BOOL CPrefsPageWnd::CreatePage(CWnd* parent, CWnd* owner, const CRect& rect) {
  _owner = owner;
  LPCTSTR class_name = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW, ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);
  // Not visible until the page gets selected. CONTROLPARENT: Tab moves between the controls.
  return CWnd::CreateEx(WS_EX_CONTROLPARENT, class_name, "", WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL,
    rect, parent, 0);
}

void CPrefsPageWnd::UpdateScrollbar() {
  if (m_hWnd == NULL) return;
  CRect client;
  GetClientRect(&client);
  SCROLLINFO info = { sizeof(SCROLLINFO) };
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  info.nMin = 0;
  info.nMax = (_content_height > 0 ? _content_height : 1) - 1;
  info.nPage = client.Height();
  info.nPos = _scroll_pos;
  SetScrollInfo(SB_VERT, &info, TRUE);
}

void CPrefsPageWnd::SetContentHeight(int height) {
  _content_height = height;
  UpdateScrollbar();
}

void CPrefsPageWnd::ScrollTo(int position) {
  CRect client;
  GetClientRect(&client);
  int max_position = _content_height - client.Height();
  if (max_position < 0) max_position = 0;
  if (position > max_position) position = max_position;
  if (position < 0) position = 0;
  if (position == _scroll_pos) return;
  const int delta = _scroll_pos - position;
  _scroll_pos = position;
  ScrollWindowEx(0, delta, NULL, NULL, NULL, NULL, SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
  UpdateScrollbar();
  UpdateWindow();
}

void CPrefsPageWnd::EnsureVisible(CWnd* control) {
  if (control == NULL || m_hWnd == NULL) return;
  CRect rect, client;
  control->GetWindowRect(&rect);
  ScreenToClient(&rect);
  GetClientRect(&client);
  if (rect.top < 0) ScrollTo(_scroll_pos + rect.top - 8);
  else if (rect.bottom > client.Height()) ScrollTo(_scroll_pos + rect.bottom - client.Height() + 8);
}

void CPrefsPageWnd::OnSize(UINT nType, int cx, int cy) {
  CWnd::OnSize(nType, cx, cy);
  UpdateScrollbar();
}

void CPrefsPageWnd::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar* pScrollBar) {
  CRect client;
  GetClientRect(&client);
  SCROLLINFO info = { sizeof(SCROLLINFO) };
  info.fMask = SIF_ALL;
  GetScrollInfo(SB_VERT, &info);
  int position = _scroll_pos;
  switch (nSBCode) {
    case SB_LINEUP:        position -= 30; break;
    case SB_LINEDOWN:      position += 30; break;
    case SB_PAGEUP:        position -= client.Height() - 30; break;
    case SB_PAGEDOWN:      position += client.Height() - 30; break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: position = info.nTrackPos; break;
    case SB_TOP:           position = 0; break;
    case SB_BOTTOM:        position = _content_height; break;
    default: return;
  }
  ScrollTo(position);
}

BOOL CPrefsPageWnd::OnMouseWheel(UINT nFlags, short zDelta, CPoint pt) {
  ScrollTo(_scroll_pos - (int)((float)zDelta / (float)WHEEL_DELTA * 60.0f));
  return TRUE;
}

void CPrefsPageWnd::ApplyTheme() {
  const Palette& c = Theme::Colors();
  _brush_window.DeleteObject();
  _brush_window.CreateSolidBrush(ToColorRef(c.window_bg));
  _brush_field.DeleteObject();
  _brush_field.CreateSolidBrush(ToColorRef(c.surface));
  if (m_hWnd) ApplyControlTheme(m_hWnd);   // themed scrollbar
  if (m_hWnd) RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}

BOOL CPrefsPageWnd::OnEraseBkgnd(CDC* pDC) {
  CRect client;
  GetClientRect(&client);
  pDC->FillSolidRect(&client, ToColorRef(Theme::Colors().window_bg));
  return TRUE;
}

HBRUSH CPrefsPageWnd::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) {
  if (_brush_window.GetSafeHandle() == NULL) ApplyTheme();
  const Palette& c = Theme::Colors();
  if (nCtlColor == CTLCOLOR_EDIT) {
    pDC->SetTextColor(ToColorRef(c.text));
    pDC->SetBkColor(ToColorRef(c.surface));
    return (HBRUSH)_brush_field.GetSafeHandle();
  }
  // Notes (tag 1) are a bit dimmer than labels and headings
  const bool note = (pWnd != NULL) && (::GetWindowLongPtr(pWnd->GetSafeHwnd(), GWLP_USERDATA) == 1);
  pDC->SetTextColor(ToColorRef(note ? c.text_dim : c.text));
  pDC->SetBkColor(ToColorRef(c.window_bg));
  return (HBRUSH)_brush_window.GetSafeHandle();
}

BOOL CPrefsPageWnd::OnCommand(WPARAM wParam, LPARAM lParam) {
  if (_owner != NULL) _owner->SendMessage(WM_COMMAND, wParam, lParam);
  return TRUE;
}

BOOL CPrefsPageWnd::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* pResult) {
  *pResult = (_owner != NULL) ? _owner->SendMessage(WM_NOTIFY, wParam, lParam) : 0;
  return TRUE;
}

}  // namespace modernglue
