//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Writes a reconstructed hand (see HandHistoryCore.h) as text in the
//   format of PokerStars, which tracking software like PokerTracker and
//   Hold'em Manager imports. Also describes the best hand of a player
//   ("a pair of Kings") like PokerStars does at the showdown.
//
//   C++ standard library only.
//
//******************************************************************************

#ifndef INC_HANDHISTORYFORMAT_H
#define INC_HANDHISTORYFORMAT_H

#include <algorithm>
#include <functional>
#include <string>
#include <vector>
#include "HandHistoryCore.h"

namespace hh {

struct FormatOptions {
  std::string site;            // first word of the header
  std::string currency_symbol; // "$"
  std::string currency_code;   // "USD"
  std::string timezone;        // label printed behind the time
  // Tournaments (Hand::tournament): buy-in and fee, both 0 = freeroll.
  // The buy-in can not be scraped, it can be set in the settings file.
  double      tournament_buyin;
  double      tournament_fee;
  // Action profile: print Action::note (formula line) behind the action
  bool        show_notes;
  FormatOptions() : site("PokerStars"), currency_symbol("$"),
                    currency_code("USD"), timezone("ET"),
                    tournament_buyin(0), tournament_fee(0), show_notes(false) {}
};

//------------------------------------------------------------------------------
// Best 5-card hand out of up to 7 cards
//------------------------------------------------------------------------------

namespace detail {

inline int RankOf(char c) {            // 2..14, 0 = invalid
  const char *ranks = "23456789TJQKA";
  for (int i = 0; i < 13; i++) if (ranks[i] == c) return i + 2;
  return 0;
}
inline int SuitOf(char c) {
  const char *suits = "cdhs";
  for (int i = 0; i < 4; i++) if (suits[i] == c) return i;
  return -1;
}
inline const char *RankName(int r, bool plural) {
  static const char *singular[] = { "", "", "Deuce", "Three", "Four", "Five", "Six", "Seven",
                                    "Eight", "Nine", "Ten", "Jack", "Queen", "King", "Ace" };
  static const char *plural_[]  = { "", "", "Deuces", "Threes", "Fours", "Fives", "Sixes", "Sevens",
                                    "Eights", "Nines", "Tens", "Jacks", "Queens", "Kings", "Aces" };
  if (r < 2 || r > 14) return "?";
  return plural ? plural_[r] : singular[r];
}

struct Score {
  int category;          // 0 high card .. 8 straight flush
  int k[5];              // tie-breakers, most important first
  Score() : category(-1) { for (int i = 0; i < 5; i++) k[i] = 0; }
  bool operator<(const Score &o) const {
    if (category != o.category) return category < o.category;
    for (int i = 0; i < 5; i++) if (k[i] != o.k[i]) return k[i] < o.k[i];
    return false;
  }
};

inline Score Evaluate5(const int r[5], const int su[5]) {
  Score sc;
  int sorted[5];
  for (int i = 0; i < 5; i++) sorted[i] = r[i];
  std::sort(sorted, sorted + 5, std::greater<int>());
  bool flush = true;
  for (int i = 1; i < 5; i++) if (su[i] != su[0]) flush = false;
  // straight (ace can be low)
  bool straight = false; int high = 0;
  if (sorted[0]-1 == sorted[1] && sorted[1]-1 == sorted[2] && sorted[2]-1 == sorted[3] && sorted[3]-1 == sorted[4]) {
    straight = true; high = sorted[0];
  } else if (sorted[0] == 14 && sorted[1] == 5 && sorted[2] == 4 && sorted[3] == 3 && sorted[4] == 2) {
    straight = true; high = 5;
  }
  int count[15] = {0};
  for (int i = 0; i < 5; i++) count[r[i]]++;
  // groups ordered by (count, rank)
  std::vector<std::pair<int,int> > groups;
  for (int v = 14; v >= 2; v--) if (count[v]) groups.push_back(std::make_pair(count[v], v));
  std::sort(groups.begin(), groups.end(), std::greater<std::pair<int,int> >());

  if (straight && flush)            { sc.category = 8; sc.k[0] = high; }
  else if (groups[0].first == 4)    { sc.category = 7; sc.k[0] = groups[0].second; sc.k[1] = groups[1].second; }
  else if (groups[0].first == 3 && groups[1].first == 2) { sc.category = 6; sc.k[0] = groups[0].second; sc.k[1] = groups[1].second; }
  else if (flush)                   { sc.category = 5; for (int i = 0; i < 5; i++) sc.k[i] = sorted[i]; }
  else if (straight)                { sc.category = 4; sc.k[0] = high; }
  else if (groups[0].first == 3)    { sc.category = 3; sc.k[0] = groups[0].second; sc.k[1] = groups[1].second; sc.k[2] = groups[2].second; }
  else if (groups[0].first == 2 && groups[1].first == 2) { sc.category = 2; sc.k[0] = groups[0].second; sc.k[1] = groups[1].second; sc.k[2] = groups[2].second; }
  else if (groups[0].first == 2)    { sc.category = 1; sc.k[0] = groups[0].second; sc.k[1] = groups[1].second; sc.k[2] = groups[2].second; sc.k[3] = groups[3].second; }
  else                              { sc.category = 0; for (int i = 0; i < 5; i++) sc.k[i] = sorted[i]; }
  return sc;
}

inline std::string StraightText(int high) {
  int low = (high == 5) ? 14 : high - 4;
  return std::string(RankName(low, false)) + " to " + RankName(high, false);
}

} // namespace detail

// Description like PokerStars: "a pair of Kings", "a flush, Ace high", ...
// Returns "" if there are fewer than 5 valid cards.
inline std::string DescribeHand(const std::vector<std::string> &cards) {
  std::vector<int> r, su;
  for (size_t i = 0; i < cards.size(); i++) {
    if (cards[i].size() < 2) continue;
    int rank = detail::RankOf(cards[i][0]), suit = detail::SuitOf(cards[i][1]);
    if (rank == 0 || suit < 0) continue;
    r.push_back(rank); su.push_back(suit);
  }
  int n = (int) r.size();
  if (n < 5) return "";
  detail::Score best;
  // all 5-card combinations
  for (int a = 0; a < n; a++) for (int b = a+1; b < n; b++) for (int c = b+1; c < n; c++)
  for (int d = c+1; d < n; d++) for (int e = d+1; e < n; e++) {
    int rr[5] = { r[a], r[b], r[c], r[d], r[e] };
    int ss[5] = { su[a], su[b], su[c], su[d], su[e] };
    detail::Score sc = detail::Evaluate5(rr, ss);
    if (best < sc) best = sc;
  }
  using detail::RankName;
  switch (best.category) {
    case 8: return "a straight flush, " + detail::StraightText(best.k[0]);
    case 7: return std::string("four of a kind, ") + RankName(best.k[0], true);
    case 6: return std::string("a full house, ") + RankName(best.k[0], true) + " full of " + RankName(best.k[1], true);
    case 5: return std::string("a flush, ") + RankName(best.k[0], false) + " high";
    case 4: return "a straight, " + detail::StraightText(best.k[0]);
    case 3: return std::string("three of a kind, ") + RankName(best.k[0], true);
    case 2: return std::string("two pair, ") + RankName(best.k[0], true) + " and " + RankName(best.k[1], true);
    case 1: return std::string("a pair of ") + RankName(best.k[0], true);
    default: return std::string("high card ") + RankName(best.k[0], false);
  }
}

//------------------------------------------------------------------------------
// PokerStars format
//------------------------------------------------------------------------------

namespace detail {

inline std::string Money(double v, const FormatOptions &o) {
  char b[48];
  double cents = floor(v * 100.0 + 0.5) / 100.0;
  if (fabs(cents - floor(cents + 0.5)) < 0.001) snprintf(b, sizeof(b), "%.0f", cents);
  else snprintf(b, sizeof(b), "%.2f", cents);
  return o.currency_symbol + b;
}

inline std::string PlayerName(const Hand &h, int seat) {
  std::string n = h.name[seat];
  for (size_t i = 0; i < n.size(); i++) if (n[i] == ':' || n[i] == '\r' || n[i] == '\n') n[i] = '_';
  if (n.empty()) { char b[16]; snprintf(b, sizeof(b), "Seat%d", seat + 1); n = b; }
  return n;
}

inline std::string StreetName(int street) {
  return street == 1 ? "Flop" : street == 2 ? "Flop" : street == 3 ? "Turn" : "River";
}

// Blind level like PokerStars: I, II, III, IV, ..., XL, ...
inline std::string RomanNumeral(int n) {
  if (n < 1) n = 1;
  static const int    value[]  = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
  static const char  *symbol[] = { "M", "CM", "D", "CD", "C", "XC", "L", "XL", "X", "IX", "V", "IV", "I" };
  std::string s;
  for (int i = 0; i < 13; i++) while (n >= value[i]) { s += symbol[i]; n -= value[i]; }
  return s;
}

// "$9.00+$1.00 USD" or "Freeroll"
inline std::string BuyInText(const FormatOptions &o) {
  if (o.tournament_buyin <= 0 && o.tournament_fee <= 0) return "Freeroll";
  char b[96];
  snprintf(b, sizeof(b), "%s%.2f+%s%.2f %s", o.currency_symbol.c_str(), o.tournament_buyin,
           o.currency_symbol.c_str(), o.tournament_fee, o.currency_code.c_str());
  return b;
}

inline std::string CardsText(const std::string &a, const std::string &b) { return "[" + a + " " + b + "]"; }

inline std::string BoardText(const Hand &h, int from, int to) {   // cards [from, to)
  std::string s = "[";
  for (int i = from; i < to; i++) { if (i > from) s += " "; s += h.board[i]; }
  return s + "]";
}

} // namespace detail

inline std::string FormatPokerStars(const Hand &h, const FormatOptions &options = FormatOptions()) {
  using namespace detail;
  std::string out;
  const char *nl = "\r\n";
  // Tournaments are played with chips: no currency symbol for the amounts
  FormatOptions o = options;
  if (h.tournament) o.currency_symbol = "";

  // ---- header ----
  if (h.tournament) {
    // PokerStars Hand #123: Tournament #456, $9.00+$1.00 USD Hold'em No Limit - Level I (10/20) - 2026/10/11 09:32:00 ET
    out += o.site + " Hand #" + h.hand_id + ": Tournament #" + h.tournament_id + ", " +
           BuyInText(options) + " Hold'em No Limit - Level " + RomanNumeral(h.level) + " (" +
           Money(h.sblind, o) + "/" + Money(h.bblind, o) + ") - " + h.timestamp + " " + o.timezone + nl;
  } else {
    out += o.site + " Hand #" + h.hand_id + ":  Hold'em No Limit (" + Money(h.sblind, o) + "/" +
           Money(h.bblind, o) + " " + o.currency_code + ") - " + h.timestamp + " " + o.timezone + nl;
  }
  const char *max_text = (h.nseats <= 2) ? "2-max" : (h.nseats <= 6) ? "6-max" : "9-max";
  char tmp[64];
  snprintf(tmp, sizeof(tmp), " %s Seat #%d is the button", max_text, h.dealer + 1);
  out += "Table '" + h.table_name + "'" + tmp + nl;
  for (int i = 0; i < h.nseats; i++) {
    if (!h.in_hand[i]) continue;
    snprintf(tmp, sizeof(tmp), "Seat %d: ", i + 1);
    out += tmp + PlayerName(h, i) + " (" + Money(h.start_stack[i], o) + " in chips)" + nl;
  }

  // ---- blinds and antes ----
  for (size_t a = 0; a < h.actions.size(); a++) {
    const Action &x = h.actions[a];
    if (x.type == kPostAnte)       out += PlayerName(h, x.seat) + ": posts the ante " + Money(x.amount, o) + nl;
  }
  for (size_t a = 0; a < h.actions.size(); a++) {
    const Action &x = h.actions[a];
    if (x.type == kPostSmallBlind) out += PlayerName(h, x.seat) + ": posts small blind " + Money(x.amount, o) + nl;
  }
  for (size_t a = 0; a < h.actions.size(); a++) {
    const Action &x = h.actions[a];
    if (x.type == kPostBigBlind)   out += PlayerName(h, x.seat) + ": posts big blind " + Money(x.amount, o) + nl;
  }

  // ---- hole cards ----
  out += std::string("*** HOLE CARDS ***") + nl;
  if (h.hero >= 0 && h.hero < h.nseats && h.in_hand[h.hero] && !h.hole[h.hero][0].empty())
    out += "Dealt to " + PlayerName(h, h.hero) + " " + CardsText(h.hole[h.hero][0], h.hole[h.hero][1]) + nl;

  // ---- betting rounds ----
  for (int st = 1; st <= 4; st++) {
    if (st == 2) { if (h.board_count < 3) break; out += "*** FLOP *** " + BoardText(h, 0, 3) + nl; }
    if (st == 3) { if (h.board_count < 4) break; out += "*** TURN *** " + BoardText(h, 0, 3) + " " + BoardText(h, 3, 4) + nl; }
    if (st == 4) { if (h.board_count < 5) break; out += "*** RIVER *** " + BoardText(h, 0, 4) + " " + BoardText(h, 4, 5) + nl; }
    for (size_t a = 0; a < h.actions.size(); a++) {
      const Action &x = h.actions[a];
      if (x.street != st) continue;
      if (x.type == kPostAnte || x.type == kPostSmallBlind || x.type == kPostBigBlind) continue;
      std::string line = PlayerName(h, x.seat) + ":";
      switch (x.type) {
        case kFold:  line += " folds"; break;
        case kCheck: line += " checks"; break;
        case kCall:  line += " calls " + Money(x.amount, o); break;
        case kBet:   line += " bets " + Money(x.amount, o); break;
        case kRaise: line += " raises " + Money(x.raise_by, o) + " to " + Money(x.to, o); break;
        default: break;
      }
      if (x.allin && x.type != kFold && x.type != kCheck) line += " and is all-in";
      out += line + nl;
      if (o.show_notes && !x.note.empty()) out += "(" + x.note + ")" + nl;
    }
  }

  if (h.uncalled > kEps && h.uncalled_seat >= 0)
    out += "Uncalled bet (" + Money(h.uncalled, o) + ") returned to " + PlayerName(h, h.uncalled_seat) + nl;

  // ---- showdown ----
  std::vector<std::string> description(kMaxSeats);
  if (h.showdown) {
    out += std::string("*** SHOW DOWN ***") + nl;
    for (int k = 1; k <= h.nseats; k++) {
      int i = (h.dealer + k) % h.nseats;
      if (!h.in_hand[i] || h.folded[i]) continue;
      if (!h.hole[i][0].empty() && !h.hole[i][1].empty()) {
        std::vector<std::string> cards;
        cards.push_back(h.hole[i][0]); cards.push_back(h.hole[i][1]);
        for (int b = 0; b < h.board_count; b++) cards.push_back(h.board[b]);
        description[i] = DescribeHand(cards);
        out += PlayerName(h, i) + ": shows " + CardsText(h.hole[i][0], h.hole[i][1]);
        if (!description[i].empty()) out += " (" + description[i] + ")";
        out += nl;
      } else {
        out += PlayerName(h, i) + ": mucks hand" + nl;
      }
    }
  }
  for (int i = 0; i < h.nseats; i++)
    if (h.in_hand[i] && h.collected[i] > kEps)
      out += PlayerName(h, i) + " collected " + Money(h.collected[i], o) + " from pot" + nl;

  // ---- summary ----
  out += std::string("*** SUMMARY ***") + nl;
  out += "Total pot " + Money(h.total_pot, o) + " | Rake " + Money(h.rake > 0 ? h.rake : 0, o) + nl;
  if (h.board_count >= 3) out += "Board " + BoardText(h, 0, h.board_count) + nl;
  for (int i = 0; i < h.nseats; i++) {
    if (!h.in_hand[i]) continue;
    snprintf(tmp, sizeof(tmp), "Seat %d: ", i + 1);
    std::string line = tmp + PlayerName(h, i);
    if (i == h.dealer)  line += " (button)";
    if (i == h.sb_seat) line += " (small blind)";
    if (i == h.bb_seat) line += " (big blind)";
    if (h.folded[i]) {
      int fold_street = 1;
      for (size_t a = 0; a < h.actions.size(); a++)
        if (h.actions[a].seat == i && h.actions[a].type == kFold) fold_street = h.actions[a].street;
      if (fold_street == 1) {
        line += " folded before Flop";
        if (h.contrib[i] < kEps) line += " (didn't bet)";
      } else {
        line += " folded on the " + StreetName(fold_street);
      }
    } else if (h.showdown) {
      if (!h.hole[i][0].empty() && !h.hole[i][1].empty()) {
        line += " showed " + CardsText(h.hole[i][0], h.hole[i][1]);
        if (h.collected[i] > kEps) line += " and won (" + Money(h.collected[i], o) + ")";
        else                       line += " and lost";
        if (!description[i].empty()) line += " with " + description[i];
      } else {
        line += " mucked";
      }
    } else if (h.collected[i] > kEps) {
      line += " collected (" + Money(h.collected[i], o) + ")";
    }
    out += line + nl;
  }
  out += nl;
  return out;
}

// Text for a hand that failed the checks: reasons first, then the hand as far as it was reconstructed
inline std::string FormatRejected(const Hand &h, const FormatOptions &o = FormatOptions()) {
  std::string out = "######## REJECTED HAND - not imported ########\r\n";
  for (size_t i = 0; i < h.problems.size(); i++) out += " - " + h.problems[i] + "\r\n";
  out += "##############################################\r\n";
  out += FormatPokerStars(h, o);
  if (!h.trace.empty()) {
    // What the scraper saw (one line per change) and what the reconstructor did about it
    out += "---- snapshots (one line per change; # = decisions of the reconstructor) ----\r\n";
    for (size_t i = 0; i < h.trace.size(); i++) out += h.trace[i] + "\r\n";
    out += "\r\n";
  }
  return out;
}

} // namespace hh

#endif // INC_HANDHISTORYFORMAT_H
