//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Renderer of the poker table in the main window.
//
//******************************************************************************

#include "ModernTable.h"
#include <cmath>

namespace modernui {

using namespace Gdiplus;

namespace {

// Seat positions as fraction of the usable area: [number of chairs][chair][x/y].
// Same layout as in the classic view, so that tablemaps keep their seat order.
const double kSeatPos[11][10][2] = {
  { {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0} },
  { {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0} },
  { {.95,.47}, {.05,.47}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0} },
  { {.95,.47}, {.50,.83}, {.05,.47}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0} },
  { {.89,.25}, {.89,.69}, {.11,.69}, {.11,.25}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0} },
  { {.89,.25}, {.89,.69}, {.50,.83}, {.11,.69}, {.11,.25}, {0,0}, {0,0}, {0,0}, {0,0}, {0,0} },
  { {.72,.11}, {.95,.47}, {.72,.83}, {.28,.83}, {.05,.47}, {.28,.11}, {0,0}, {0,0}, {0,0}, {0,0} },
  { {.72,.11}, {.95,.47}, {.72,.83}, {.50,.83}, {.28,.83}, {.05,.47}, {.28,.11}, {0,0}, {0,0}, {0,0} },
  { {.72,.11}, {.89,.25}, {.89,.69}, {.72,.83}, {.28,.83}, {.11,.69}, {.11,.25}, {.28,.11}, {0,0}, {0,0} },
  { {.72,.11}, {.89,.25}, {.89,.69}, {.72,.83}, {.50,.83}, {.28,.83}, {.11,.69}, {.11,.25}, {.28,.11}, {0,0} },
  { {.72,.11}, {.85,.21}, {.95,.47}, {.85,.73}, {.72,.83}, {.28,.83}, {.15,.73}, {.05,.47}, {.15,.21}, {.28,.11} },
};

std::wstring RankText(char rank) {
  if (rank == 'T' || rank == 't') return L"10";
  return std::wstring(1, (wchar_t)toupper((unsigned char)rank));
}

DWORD SuitColor(const Palette& c, char suit) {
  switch (suit) {
    case 'c': case 'C': return c.card_club;
    case 'd': case 'D': return c.card_diamond;
    case 'h': case 'H': return c.card_heart;
    default:            return c.card_spade;
  }
}

void DrawCardBack(Graphics& g, const RectF& r, float s, const Palette& c) {
  LinearGradientBrush grad(r, Rgba(c.card_back_top), Rgba(c.card_back_bottom), LinearGradientModeVertical);
  FillRoundRect(g, r, 6 * s, grad);
  StrokeRoundRect(g, r, 6 * s, Rgba(c.card_back_edge), 1.0f * s);
  RectF inner(r.X + 3 * s, r.Y + 3 * s, r.Width - 6 * s, r.Height - 6 * s);
  StrokeRoundRect(g, inner, 4 * s, Rgba(c.card_back_inner), 1.0f * s);
}

// content_w: width of the part of the card that is not covered by the next card.
void DrawCardFace(Graphics& g, const RectF& r, float s, const Palette& c, const CardView& card, float content_w) {
  FillRoundRect(g, r, 6 * s, Rgba(c.card_face));
  StrokeRoundRect(g, r, 6 * s, WithAlpha(0xFF000000, 60), 1.0f * s);
  Color ink = Rgba(SuitColor(c, card.suit));
  std::wstring rank = RankText(card.rank);
  float rank_px = (rank.size() > 1 ? 15.0f : 18.0f) * s;
  RectF top(r.X, r.Y + 1 * s, content_w, r.Height * 0.50f);
  DrawLabel(g, rank, GetFont(rank_px, true), top, ink, HAlign::Center, false);
  float glyph = 13 * s;
  RectF suit(r.X + (content_w - glyph) / 2, r.Y + r.Height * 0.56f, glyph, glyph);
  DrawSuit(g, card.suit, suit, ink);
}

void DrawCardSlot(Graphics& g, const RectF& r, float s, const Palette& c) {
  FillRoundRect(g, r, 6 * s, WithAlpha(0xFF000000, 38));
  StrokeRoundRect(g, r, 6 * s, Rgba(c.dashed), 1.2f * s, DashStyleDash);
}

void DrawCard(Graphics& g, const RectF& r, float s, const Palette& c, const CardView& card, float content_w = 0) {
  if (card.kind == CardView::Back) DrawCardBack(g, r, s, c);
  else if (card.kind == CardView::Face) DrawCardFace(g, r, s, c, card, content_w > 0 ? content_w : r.Width);
}

void DrawBetChip(Graphics& g, float cx, float cy, float s, const Palette& c, const std::wstring& text) {
  const Font* font = GetFont(12.0f * s, true);
  float w = MeasureLabel(g, text, font) + 18 * s;
  if (w < 24 * s) w = 24 * s;
  RectF r(cx - w / 2, cy - 10 * s, w, 20 * s);
  FillRoundRect(g, r, 10 * s, Rgba(c.chip_bg));
  DrawLabel(g, text, font, r, Rgba(c.chip_text), HAlign::Center, false);
}

void DrawDealer(Graphics& g, float cx, float cy, float s, const Palette& c) {
  FillCircle(g, cx, cy, 11 * s, Rgba(c.dealer_border));
  FillCircle(g, cx, cy, 9 * s, Rgba(c.dealer_bg));
  DrawLabel(g, L"D", GetFont(12.0f * s, true), RectF(cx - 10 * s, cy - 10 * s, 20 * s, 20 * s),
            Rgba(c.dealer_text), HAlign::Center, false);
}

void DrawBadge(Graphics& g, const RectF& r, float s, const Palette& c, wchar_t letter, bool on) {
  FillRoundRect(g, r, 6 * s, Rgba(on ? c.badge_on_bg : c.badge_off_bg));
  wchar_t text[2] = { letter, 0 };
  DrawLabel(g, text, GetFont(11.5f * s, true), r, Rgba(on ? c.badge_on_text : c.badge_off_text),
            HAlign::Center, false);
}

}  // namespace

void DrawTable(Graphics& g, const RectF& area, float s, const Palette& c, const TableView& v) {
  g.SetClip(area);
  SolidBrush bg(Rgba(c.window_bg));
  g.FillRectangle(&bg, area);

  // ---- Felt ---------------------------------------------------------------
  const float indicator_h = 22 * s, indicator_margin = 12 * s;
  RectF felt(area.X + 14 * s, area.Y + 9 * s,
             area.Width - 28 * s, area.Height - 9 * s - 36 * s);
  float felt_radius = felt.Height * 0.40f;
  FillRoundRect(g, felt, felt_radius, Rgba(c.felt));
  StrokeRoundRect(g, felt, felt_radius, Rgba(c.felt_rim), 3.0f * s);
  RectF inner_line(felt.X + 10 * s, felt.Y + 10 * s, felt.Width - 20 * s, felt.Height - 20 * s);
  StrokeRoundRect(g, inner_line, felt_radius - 10 * s, WithAlpha(c.felt_line, 150), 1.0f * s);

  const float cx = felt.X + felt.Width / 2;
  const float cy = felt.Y + felt.Height / 2;

  // ---- Info box (hand number, game, pot) -----------------------------------
  {
    int lines = 0;
    if (!v.hand_line.empty()) ++lines;
    if (!v.game_line.empty()) ++lines;
    if (!v.ante_line.empty()) ++lines;
    if (!v.pot_line.empty()) ++lines;
    if (lines < 3) lines = 3;
    float line_h = 18 * s;
    float h = lines * line_h + 14 * s;
    RectF box(cx - 84 * s, felt.Y + felt.Height * 0.14f, 168 * s, h);
    FillRoundRect(g, box, 12 * s, Rgba(c.info_bg));
    float y = box.Y + 7 * s;
    const Font* normal = GetFont(13.0f * s, false);
    const Font* bold = GetFont(13.0f * s, true);
    const std::wstring* texts[3] = { &v.hand_line, &v.game_line, &v.ante_line };
    for (int i = 0; i < 3; ++i) {
      if (texts[i]->empty()) continue;
      DrawLabel(g, *texts[i], normal, RectF(box.X, y, box.Width, line_h), Rgba(c.info_text), HAlign::Center);
      y += line_h;
    }
    if (!v.pot_line.empty()) {
      DrawLabel(g, v.pot_line, bold, RectF(box.X, y, box.Width, line_h), Rgba(c.info_accent), HAlign::Center);
    }
  }

  // ---- Community cards ------------------------------------------------------
  {
    const float cw = 36 * s, ch = 42 * s, gap = 5 * s;
    float total = 5 * cw + 4 * gap;
    float x = cx - total / 2;
    float y = cy - ch / 2 + 6 * s;
    for (int i = 0; i < 5; ++i) {
      RectF r(x + i * (cw + gap), y, cw, ch);
      if (v.community[i].kind == CardView::None) DrawCardSlot(g, r, s, c);
      else DrawCard(g, r, s, c, v.community[i]);
    }
  }

  // ---- Seats ----------------------------------------------------------------
  int n = v.nchairs;
  if (n < 2) n = 2;
  if (n > 10) n = 10;
  const float seat_w = 98 * s, seat_h = 38 * s;
  const float card_w = 28 * s, card_h = 40 * s, card_step = 21 * s;
  const float left_margin = 60 * s, right_margin = 60 * s;
  const float top_margin = 56 * s, bottom_margin = 40 * s;
  for (int i = 0; i < n; ++i) {
    const SeatView& seat = v.seats[i];
    float sx = area.X + left_margin + (float)kSeatPos[n][i][0] * (area.Width - left_margin - right_margin);
    float sy = area.Y + top_margin + (float)kSeatPos[n][i][1] * (area.Height - top_margin - bottom_margin);
    RectF panel(sx - seat_w / 2, sy - seat_h / 2, seat_w, seat_h);

    if (!seat.seated && !seat.active) {
      // Empty chair: dashed outline only.
      FillRoundRect(g, panel, 11 * s, WithAlpha(0xFF000000, 30));
      StrokeRoundRect(g, panel, 11 * s, Rgba(c.dashed), 1.2f * s, DashStyleDash);
      if (seat.dealer) DrawDealer(g, sx < cx ? panel.X + panel.Width + 6 * s : panel.X - 6 * s, sy, s, c);
      continue;
    }

    // Cards first: the panel overlaps their lower edge.
    if (seat.active && seat.card_count > 0) {
      int count = seat.card_count > 4 ? 4 : seat.card_count;
      float total = card_w + (count - 1) * card_step;
      float x0 = sx - total / 2;
      float y0 = panel.Y + 4 * s - card_h;
      for (int k = 0; k < count; ++k) {
        float visible = (k == count - 1) ? card_w : card_step;
        DrawCard(g, RectF(x0 + k * card_step, y0, card_w, card_h), s, c, seat.cards[k], visible);
      }
    }

    // Name panel
    if (seat.active) {
      FillRoundRect(g, panel, 11 * s, Rgba(c.seat_bg));
      StrokeRoundRect(g, panel, 11 * s, Rgba(c.seat_border), 2.0f * s);
    } else {
      FillRoundRect(g, panel, 11 * s, Rgba(c.seat_out_bg));
      StrokeRoundRect(g, panel, 11 * s, WithAlpha(c.border_strong, 120), 1.0f * s);
    }
    DWORD name_color = seat.active ? c.seat_name : c.seat_out_text;
    DWORD bal_color = seat.active ? c.seat_balance : c.seat_out_text;
    DrawLabel(g, seat.name, GetFont(13.0f * s, true),
              RectF(panel.X + 6 * s, panel.Y + 3 * s, panel.Width - 12 * s, panel.Height / 2 - 1 * s),
              Rgba(name_color), HAlign::Center);
    DrawLabel(g, seat.balance, GetFont(12.0f * s, false),
              RectF(panel.X + 6 * s, panel.Y + panel.Height / 2 - 2 * s, panel.Width - 12 * s, panel.Height / 2 - 1 * s),
              Rgba(bal_color), HAlign::Center);

    if (seat.dealer) {
      DrawDealer(g, sx < cx ? panel.X + panel.Width + 6 * s : panel.X - 6 * s, sy, s, c);
    }

    // Bet chip: towards the middle of the table.
    if (!seat.bet.empty()) {
      float bx, by;
      float dir = (sx < cx) ? 1.0f : -1.0f;
      if (sy > cy + 10 * s) {
        // Lower half: beside the cards, above the panel.
        bx = sx + dir * 44 * s;
        by = panel.Y - card_h * 0.60f;
      } else {
        bx = sx + dir * (seat_w / 2 + 10 * s);
        by = sy + (sy < cy ? 12 * s : 0);
        if (fabsf(sx - cx) < seat_w) { bx = sx; by = panel.Y + panel.Height + 14 * s; }
      }
      DrawBetChip(g, bx, by, s, c, seat.bet);
    }
  }

  // ---- Indicator badges -----------------------------------------------------
  {
    const float size = indicator_h, gap = 4 * s;
    float y = area.Y + area.Height - indicator_margin - size;
    const wchar_t right_letters[5] = { L'F', L'C', L'K', L'R', L'A' };
    const wchar_t left_letters[5]  = { L'T', L'I', L'O', L'L', L'P' };
    float x_right = area.X + area.Width - indicator_margin - (5 * size + 4 * gap);
    for (int i = 0; i < 5; ++i) {
      DrawBadge(g, RectF(x_right + i * (size + gap), y, size, size), s, c, right_letters[i], v.indicator_on[kIndFold + i]);
    }
    float x_left = area.X + indicator_margin;
    for (int i = 0; i < 5; ++i) {
      DrawBadge(g, RectF(x_left + i * (size + gap), y, size, size), s, c, left_letters[i], v.indicator_on[kIndAutopost + i]);
    }
  }
  g.ResetClip();
}

}  // namespace modernui
