// Renders the poker table with the data of the design mockup.
#include "png_util.h"
#include "../ModernTable.h"
using namespace Gdiplus; using namespace modernui;

static CardView Face(char r, char s) { CardView c; c.kind = CardView::Face; c.rank = r; c.suit = s; return c; }
static CardView Back() { CardView c; c.kind = CardView::Back; return c; }

int main(int argc, char** argv) {
  bool light = argc > 1 && std::string(argv[1]) == "light";
  GdiplusAcquire();
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
  v.seats[5].cards[0] = Face('A', 'd'); v.seats[5].cards[1] = Face('K', 's'); v.seats[5].is_user = true;
  v.indicator_on[kIndSitout] = v.indicator_on[kIndLeave] = v.indicator_on[kIndPrefold] = true;
  const Palette& pal = light ? Theme::LightPalette() : Theme::DarkPalette();
  RenderToPng(765, 360, light ? L"Z:\\tmp\\table_light.png" : L"Z:\\tmp\\table_dark.png", [&](Graphics& g) {
    DrawTable(g, RectF(0, 0, 765, 360), 1.0f, pal, v);
  });
  GdiplusRelease();
  return 0;
}
