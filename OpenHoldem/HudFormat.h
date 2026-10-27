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

}  // namespace ohstats

#endif  // INC_HUDFORMAT_H
