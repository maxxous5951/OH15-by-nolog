//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Pure text / classification logic of the HUD (advisor mode):
//   which statistics are shown in which betround, how a value is formatted
//   and which colour a player gets. No Windows, no database: tested on
//   its own (StatsDbTests).
//
//******************************************************************************

#ifndef INC_HUDFORMAT_H
#define INC_HUDFORMAT_H

#include <ctype.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace ohstats {

const int kHudMaxStats = 6;

// "vpip pfr, 3bet" -> { "vpip", "pfr", "3bet" } (spaces, commas, semicolons; lower case)
inline std::vector<std::string> ParseHudStatList(const std::string &text) {
  std::vector<std::string> result;
  std::string current;
  for (size_t i = 0; i <= text.size(); ++i) {
    const char ch = (i < text.size()) ? text[i] : ' ';
    if (ch == ' ' || ch == ',' || ch == ';' || ch == '\t' || ch == '\n' || ch == '\r') {
      if (!current.empty()) {
        if (current != "-" && (int)result.size() < kHudMaxStats) result.push_back(current);
        current.clear();
      }
    } else {
      current += (char)tolower((unsigned char)ch);
    }
  }
  return result;
}

// Short label for the "show names" mode
inline std::string HudShortName(const std::string &stat) {
  static const struct { const char *stat, *label; } kNames[] = {
    { "hands", "N" }, { "vpip", "VPIP" }, { "pfr", "PFR" }, { "rfi", "RFI" },
    { "wtsd", "WTSD" }, { "wssd", "WSSD" }, { "3bet", "3B" }, { "4bet", "4B" },
    { "fold_to_3bet", "F3B" }, { "fold_to_4bet", "F4B" }, { "steal_attempt", "ST" },
    { "bb_fold_to_steal", "BBFS" }, { "preflop_squeeze", "SQ" },
    { "flop_cbet", "CB" }, { "flop_fold_to_cbet", "FCB" }, { "flop_raise_cbet", "RCB" },
    { "turn_cbet", "TCB" }, { "turn_fold_to_cbet", "TFCB" }, { "river_fold_to_cbet", "RFCB" },
    { "flop_checkraise", "XR" }, { "flop_donkbet", "DON" }, { "river_bet", "RBET" },
    { "flop_af", "FAF" }, { "turn_af", "TAF" }, { "river_af", "RAF" }, { "total_af", "AF" },
    { "flop_afq", "FAFq" }, { "turn_afq", "TAFq" }, { "river_afq", "RAFq" }, { "total_afq", "AFq" },
  };
  for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
    if (stat == kNames[i].stat) return kNames[i].label;
  }
  std::string label;
  for (size_t i = 0; i < stat.size() && label.size() < 6; ++i) label += (char)toupper((unsigned char)stat[i]);
  return label;
}

// Aggression factors are shown with one decimal, counts as integers,
// everything else as a percentage. Undefined (negative) values show "-".
inline std::string FormatHudValue(const std::string &stat, double value) {
  if (!(value >= 0.0)) return "-";   // also catches NaN
  char buffer[32];
  if (stat == "hands") {
    snprintf(buffer, sizeof(buffer), "%d", (int)(value + 0.5));
  } else if (stat.find("_af") != std::string::npos && stat.find("afq") == std::string::npos) {
    snprintf(buffer, sizeof(buffer), "%.1f", value);
  } else {
    snprintf(buffer, sizeof(buffer), "%d", (int)(value * 100.0 + 0.5));
  }
  return buffer;
}

// "92/71/12 (119)"  or, with names, "VPIP 92 PFR 71 3B 12 (119)".
// hands < 0 = unknown count, no brackets.
inline std::string BuildHudLine(const std::vector<std::string> &stats, const std::vector<double> &values,
                                double hands, bool show_names) {
  std::string line;
  for (size_t i = 0; i < stats.size() && i < values.size(); ++i) {
    if (i > 0) line += show_names ? " " : "/";
    if (show_names) line += HudShortName(stats[i]) + " ";
    line += FormatHudValue(stats[i], values[i]);
  }
  if (hands >= 0.0) {
    if (!line.empty()) line += " ";
    line += "(" + FormatHudValue("hands", hands) + ")";
  }
  return line;
}

enum HudPlayerType {
  kHudTypeUnknown = 0, kHudTypeNit, kHudTypeTag, kHudTypeLag, kHudTypeManiac,
  kHudTypeFish, kHudTypeCallingStation, kHudTypePassive
};

// Same rules as the type column of the stats explorer.
// vpip / pfr are ratios (0..1), negative = unknown.
inline HudPlayerType ClassifyHudPlayer(double vpip, double pfr, double hands) {
  if (hands < 30.0 || !(vpip >= 0.0)) return kHudTypeUnknown;
  const double v = vpip * 100.0, p = (pfr >= 0.0) ? pfr * 100.0 : 0.0;
  if (v >= 40 && p >= 30) return kHudTypeManiac;
  if (v >= 30 && p < 12)  return kHudTypeCallingStation;
  if (v >= 30 && p < 20)  return kHudTypeFish;
  if (v < 14)             return kHudTypeNit;
  if (p / v >= 0.65)      return v <= 25 ? kHudTypeTag : kHudTypeLag;
  return kHudTypePassive;
}

// 0xAARRGGBB, as used by the modern UI (Gdiplus::Color)
inline unsigned long HudTypeColor(HudPlayerType type) {
  switch (type) {
    case kHudTypeNit:            return 0xFF4F8DF7;   // blue
    case kHudTypeTag:            return 0xFF3FB950;   // green
    case kHudTypeLag:            return 0xFFF0883E;   // orange
    case kHudTypeManiac:         return 0xFFF85149;   // red
    case kHudTypeFish:           return 0xFFD4A72C;   // yellow
    case kHudTypeCallingStation: return 0xFFD4A72C;   // yellow
    case kHudTypePassive:        return 0xFFA371F7;   // purple
    default:                     return 0xFF8A8F98;   // grey
  }
}

inline const char *HudTypeName(HudPlayerType type) {
  switch (type) {
    case kHudTypeNit: return "Nit";
    case kHudTypeTag: return "TAG";
    case kHudTypeLag: return "LAG";
    case kHudTypeManiac: return "Maniac";
    case kHudTypeFish: return "Fish";
    case kHudTypeCallingStation: return "Calling station";
    case kHudTypePassive: return "Passive";
    default: return "?";
  }
}


// ---------------------------------------------------------------------------
// Texts of one label, for all the styles
// ---------------------------------------------------------------------------

enum HudStyle {
  kHudStylePill = 0,     // 1. one line, rounded, coloured dot
  kHudStyleTab,          // 2. two lines under the seat
  kHudStyleColumn,       // 3. labelled column next to the seat (outside the table)
  kHudStyleBadge,        // 4. coloured badge, all stats on mouse-over
  kHudNumberOfStyles
};

struct HudText {
  std::string compact;                 // "92/71/12 (119)"
  std::string named;                   // "VPIP 92 PFR 71 3B 12 (119)"
  std::vector<std::string> names;      // rows of the column / tooltip: "VPIP", "PFR", "3B", "Hands"
  std::vector<std::string> values;     // "92", "71", "12", "119"
  std::string type;                    // "Maniac", "new"
  std::string badge;                   // "Maniac 92/71"
  unsigned long colour;
  double hands;                        // < 0: not in the database
  HudText() : colour(0xFF8A8F98), hands(-1.0) {}
};

// stats / values: the list of the betround. vpip / pfr: for the type (negative = unknown).
// hands < 0: player not (yet) in the database.
inline HudText BuildHudText(const std::vector<std::string> &stats, const std::vector<double> &values,
                            double hands, double vpip, double pfr, bool show_names) {
  HudText t;
  t.hands = hands;
  if (hands < 0.0) {
    t.compact = t.named = t.badge = t.type = "new";
    return t;
  }
  t.compact = BuildHudLine(stats, values, hands, false);
  t.named = BuildHudLine(stats, values, hands, true);
  if (show_names) t.compact = t.named;
  for (size_t i = 0; i < stats.size() && i < values.size(); ++i) {
    t.names.push_back(HudShortName(stats[i]));
    t.values.push_back(FormatHudValue(stats[i], values[i]));
  }
  t.names.push_back("Hands");
  t.values.push_back(FormatHudValue("hands", hands));
  const HudPlayerType type = ClassifyHudPlayer(vpip, pfr, hands);
  t.type = HudTypeName(type);
  t.colour = HudTypeColor(type);
  t.badge = (type == kHudTypeUnknown) ? std::string("?") : t.type;
  if (!values.empty()) {
    t.badge += " " + FormatHudValue(stats[0], values[0]);
    if (values.size() > 1) t.badge += "/" + FormatHudValue(stats[1], values[1]);
  }
  return t;
}

// ---------------------------------------------------------------------------
// Placement: labels never overlap each other, the seats, cards, bets, ...
// ---------------------------------------------------------------------------

struct HudRect {
  int l, t, r, b;
  HudRect() : l(0), t(0), r(0), b(0) {}
  HudRect(int left, int top, int right, int bottom) : l(left), t(top), r(right), b(bottom) {}
  int Width() const { return r - l; }
  int Height() const { return b - t; }
  int CenterX() const { return (l + r) / 2; }
  int CenterY() const { return (t + b) / 2; }
  bool Empty() const { return r <= l || b <= t; }
};

inline bool HudInside(const HudRect &inner, const HudRect &outer) {
  return inner.l >= outer.l && inner.t >= outer.t && inner.r <= outer.r && inner.b <= outer.b;
}

// Overlap area, with a margin around both rectangles
inline long long HudOverlap(const HudRect &a, const HudRect &b, int margin) {
  const long long w = (long long)(a.r < b.r ? a.r : b.r) - (a.l > b.l ? a.l : b.l) + 2 * margin;
  const long long h = (long long)(a.b < b.b ? a.b : b.b) - (a.t > b.t ? a.t : b.t) + 2 * margin;
  if (w <= 0 || h <= 0) return 0;
  return w * h;
}

struct HudPlaceItem {
  HudRect seat;     // name / balance of the player (never covered)
  int     width, height;
  int     style;
  bool    valid;
  HudPlaceItem() : width(0), height(0), style(kHudStylePill), valid(false) {}
};

namespace detail {

enum Slot { kAbove, kBelow, kLeft, kRight };

inline void HudCandidates(const HudPlaceItem &item, const HudRect &inner, const HudRect &outer,
                          std::vector<HudRect> *out) {
  const int gaps[] = { 2, 6, 12, 24, 40, 64, 96 };
  const bool left_half = item.seat.CenterX() < (inner.l + inner.r) / 2;
  // Order of the sides
  std::vector<int> primary;     // tried inside the table window first
  std::vector<int> outside_first;
  switch (item.style) {
    case kHudStyleTab:
      primary = { kBelow, kAbove, kLeft, kRight };
      break;
    case kHudStyleColumn:
      // next to the seat, on the outside of the table, then the other side
      outside_first = { left_half ? (int)kLeft : (int)kRight };
      primary = { left_half ? (int)kRight : (int)kLeft, kAbove, kBelow };
      break;
    default:
      primary = { kAbove, kBelow, kLeft, kRight };
      break;
  }
  auto generate = [&](int slot, const HudRect &bounds) {
    const bool vertical = (slot == kAbove || slot == kBelow);
    const int span = vertical ? (item.width + item.seat.Width()) : (item.height + item.seat.Height());
    for (size_t g = 0; g < sizeof(gaps) / sizeof(gaps[0]); ++g) {
      for (int k = 0; k <= span / 8 + 1; ++k) {
        for (int sign = 0; sign < (k == 0 ? 1 : 2); ++sign) {
          const int shift = (sign == 0 ? 1 : -1) * k * 8;
          HudRect r;
          if (slot == kAbove)      r = HudRect(item.seat.CenterX() - item.width / 2 + shift, item.seat.t - gaps[g] - item.height, 0, 0);
          else if (slot == kBelow) r = HudRect(item.seat.CenterX() - item.width / 2 + shift, item.seat.b + gaps[g], 0, 0);
          else if (slot == kLeft)  r = HudRect(item.seat.l - gaps[g] - item.width, item.seat.CenterY() - item.height / 2 + shift, 0, 0);
          else                     r = HudRect(item.seat.r + gaps[g], item.seat.CenterY() - item.height / 2 + shift, 0, 0);
          r.r = r.l + item.width;
          r.b = r.t + item.height;
          if (HudInside(r, bounds)) out->push_back(r);
        }
      }
    }
  };
  for (size_t i = 0; i < outside_first.size(); ++i) generate(outside_first[i], outer);
  for (size_t i = 0; i < primary.size(); ++i) generate(primary[i], inner);
  for (size_t i = 0; i < outside_first.size(); ++i) generate(outside_first[i], inner);
  for (size_t i = 0; i < primary.size(); ++i) generate(primary[i], outer);
}

}  // namespace detail

// One rectangle per item (empty for invalid items): the first candidate position that touches
// nothing (obstacles, the seats of all players, labels placed before); if there is none
// the position with the smallest overlap. "inner" = table window, "outer" = where labels may
// stick out of it (outside of the table, but only if there is no room inside, except for the
// column style which prefers the outside).
inline std::vector<HudRect> PlaceHudLabels(const std::vector<HudPlaceItem> &items,
                                           const std::vector<HudRect> &obstacles,
                                           const HudRect &inner, const HudRect &outer) {
  std::vector<HudRect> placed(items.size());
  std::vector<HudRect> fixed = obstacles;
  for (size_t i = 0; i < items.size(); ++i) {
    if (items[i].valid && !items[i].seat.Empty()) fixed.push_back(items[i].seat);
  }
  std::vector<HudRect> taken;      // labels placed so far
  for (size_t i = 0; i < items.size(); ++i) {
    if (!items[i].valid || items[i].seat.Empty() || items[i].width <= 0 || items[i].height <= 0) continue;
    std::vector<HudRect> candidates;
    detail::HudCandidates(items[i], inner, outer, &candidates);
    if (candidates.empty()) {
      // Larger than the allowed area: centred on the seat, whatever happens
      HudRect r(items[i].seat.CenterX() - items[i].width / 2, items[i].seat.t - items[i].height,
                0, 0);
      r.r = r.l + items[i].width; r.b = r.t + items[i].height;
      placed[i] = r;
      taken.push_back(r);
      continue;
    }
    long long best_overlap = -1;
    size_t best = 0;
    bool done = false;
    for (size_t c = 0; c < candidates.size() && !done; ++c) {
      long long overlap = 0;
      for (size_t k = 0; k < fixed.size(); ++k) {
        // the own seat is part of "fixed" too: labels never cover it
        overlap += HudOverlap(candidates[c], fixed[k], 0);
      }
      for (size_t k = 0; k < taken.size(); ++k) overlap += HudOverlap(candidates[c], taken[k], 1);
      if (overlap == 0) { best = c; best_overlap = 0; done = true; }
      else if (best_overlap < 0 || overlap < best_overlap) { best_overlap = overlap; best = c; }
    }
    placed[i] = candidates[best];
    taken.push_back(candidates[best]);
  }
  return placed;
}

}  // namespace ohstats

#endif  // INC_HUDFORMAT_H
