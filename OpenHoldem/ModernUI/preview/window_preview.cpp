// Composes header + table + status bar (+ a popup menu) like the real main window.
#include "png_util.h"
#include "../ModernHeader.h"
#include "../ModernTable.h"
using namespace Gdiplus; using namespace modernui;

static CardView Face(char r, char s) { CardView c; c.kind = CardView::Face; c.rank = r; c.suit = s; return c; }
static CardView Back() { CardView c; c.kind = CardView::Back; return c; }

static MenuItem Item(int id, const wchar_t* text, const wchar_t* shortcut, Icon icon, bool enabled = true) {
  MenuItem m; m.id = id; m.text = text; m.shortcut = shortcut; m.icon = icon; m.enabled = enabled; return m;
}
static MenuItem Sep() { MenuItem m; m.separator = true; return m; }

int main(int argc, char** argv) {
  bool light = argc > 1 && std::string(argv[1]) == "light";
  bool with_menu = !(argc > 2 && std::string(argv[2]) == "nomenu");
  GdiplusAcquire();
  Theme::SetMode(light ? ThemeMode::Light : ThemeMode::Dark);   // header/popups read the global theme
  const Palette& pal = Theme::Colors();

  // ---- menu bar ----
  MenuBar bar;
  { Menu m; m.text = L"File"; m.mnemonic = L'F';
    m.items.push_back(Item(1, L"New", L"Ctrl+N", Icon::FileNew, false));
    m.items.push_back(Item(2, L"Open...", L"Ctrl+O", Icon::FolderOpen, false));
    m.items.push_back(Item(3, L"Save", L"Ctrl+S", Icon::Save));
    m.items.push_back(Item(4, L"Save As...", L"Ctrl+A", Icon::SaveAs));
    m.items.push_back(Sep());
    m.items.push_back(Item(5, L"C:\\Bots\\Gecko_NL_6Max.ohf", L"", Icon::Files));
    m.items.push_back(Sep());
    m.items.push_back(Item(6, L"Exit", L"", Icon::Exit));
    bar.push_back(m); }
  { Menu m; m.text = L"Edit"; m.mnemonic = L'E'; bar.push_back(m); }
  { Menu m; m.text = L"Problem Solver"; m.mnemonic = L'P'; m.command = 99; bar.push_back(m); }
  { Menu m; m.text = L"Help"; m.mnemonic = L'H'; bar.push_back(m); }

  ModernHeader header;
  header.SetMenuBar(bar);
  std::vector<ToolButtonDef> tools = {
    { 10, Icon::Plus,       L"New",         false, false, false },
    { 11, Icon::FileOpen,   L"Open",        false, false, false },
    { 12, Icon::Save,       L"Save",        false, false, false },
    { 13, Icon::Play,       L"Autoplayer",  true,  true,  true  },
    { 14, Icon::Braces,     L"Formula",     true,  false, false },
    { 15, Icon::Check,      L"Validator",   true,  false, false },
    { 16, Icon::Tag,        L"Tag log",     true,  false, false },
    { 17, Icon::Eye,        L"Scraper",     true,  false, true  },
    { 18, Icon::Camera,     L"Shoot frame", false, false, false },
    { 19, Icon::ManualMode, L"Manual mode", false, false, false },
    { 20, Icon::Help,       L"Help",        false, false, true  },
  };
  header.SetToolbarButtons(tools);
  int flags[20]; for (int i = 0; i < 20; ++i) flags[i] = 100 + i;
  header.SetFlagCommandIds(flags, 20);
  header.SetTitle(L"Gecko_NL_6Max_FR_BSS | openholdem | OpenHoldem Poker - Not connected");
  header.SetConnected(true);
  header.EnableButton(10, false); header.EnableButton(11, false);
  header.CheckButton(13, true);
  header.CheckButton(100 + 3, true);
  header.CheckButton(100 + 7, true);

  TableView v;
  v.nchairs = 9;
  v.hand_line = L"Hand #: 2"; v.game_line = L"NL 5/10/20"; v.pot_line = L"Pot: 255";
  const wchar_t* names[9] = { L"Player0", L"Player1", L"Player2", L"Player3", L"Player4", L"Player5", L"Player6", L"", L"" };
  const wchar_t* bal[9]   = { L"1000", L"995", L"990", L"960", L"Out (0)", L"800", L"1000", L"", L"" };
  const wchar_t* bet[9]   = { L"", L"5", L"10", L"40", L"", L"200", L"", L"", L"" };
  for (int i = 0; i < 7; ++i) {
    SeatView& s = v.seats[i];
    s.seated = true; s.active = (i != 4); s.name = names[i]; s.balance = bal[i]; s.bet = bet[i];
    if (s.active) { s.card_count = 2; s.cards[0] = Back(); s.cards[1] = Back(); }
  }
  v.seats[0].dealer = true;
  v.seats[5].cards[0] = Face('A', 'd'); v.seats[5].cards[1] = Face('K', 's');
  v.indicator_on[kIndSitout] = v.indicator_on[kIndLeave] = v.indicator_on[kIndPrefold] = true;

  const int W = 765, H = 560;
  const float s = 1.0f;
  int header_h = header.Height(s), status_h = StatusbarHeight(s);
  RenderToPng(W, H, light ? (with_menu ? L"Z:\\tmp\\window_light_menu.png" : L"Z:\\tmp\\window_light.png") : (with_menu ? L"Z:\\tmp\\window_dark.png" : L"Z:\\tmp\\window_dark_nomenu.png"), [&](Graphics& g) {
    g.Clear(Rgba(pal.window_bg));
    header.Paint(g, W, s);
    GraphicsState st = g.Save();
    g.TranslateTransform(0, (REAL)header_h);
    DrawTable(g, RectF(0, 0, (REAL)W, (REAL)(H - header_h - status_h)), s, pal, v);
    g.Restore(st);
    st = g.Save();
    g.TranslateTransform(0, (REAL)(H - status_h));
    ModernStatusbar::Paint(g, W, status_h, s, pal, L"f$betsize = 200.00", L"9/169", L"PrWin 37.4/2.1/60.5");
    g.Restore(st);
    // File menu popup, as it appears when opened (hover on "Save")
    if (!with_menu) return;
    Menu& file = bar[0];
    MenuLayout layout = LayoutMenu(g, file, s);
    st = g.Save();
    g.TranslateTransform(10, 78);
    PaintMenu(g, file, layout, 2, s, pal);
    g.Restore(st);
  });
  GdiplusRelease();
  return 0;
}
