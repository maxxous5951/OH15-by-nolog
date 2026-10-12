#include "png_util.h"
using namespace Gdiplus; using namespace modernui;
int main() {
  GdiplusAcquire();
  const Palette& c = Theme::DarkPalette();
  bool ok = RenderToPng(300, 120, L"Z:\\tmp\\smoke.png", [&](Graphics& g) {
    g.Clear(Rgba(c.surface));
    FillRoundRect(g, RectF(10, 10, 120, 40), 10, Rgba(c.accent_soft));
    StrokeRoundRect(g, RectF(10, 10, 120, 40), 10, Rgba(c.accent), 2);
    DrawLabel(g, L"Autoplayer", GetFont(13, true), RectF(10, 10, 120, 40), Rgba(c.accent), HAlign::Center);
    DrawIcon(g, Icon::Play, RectF(150, 12, 24, 24), Rgba(c.success));
    DrawIcon(g, Icon::Braces, RectF(190, 12, 24, 24), Rgba(c.text));
    DrawSuit(g, 'd', RectF(230, 12, 24, 24), Rgba(c.card_diamond));
    DrawSuit(g, 's', RectF(260, 12, 24, 24), Rgba(c.card_heart));
  });
  GdiplusRelease();
  return ok ? 0 : 1;
}
