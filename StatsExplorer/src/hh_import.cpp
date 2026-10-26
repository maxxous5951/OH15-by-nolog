//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: import of PokerStars-style hand histories, see hh_import.h
//
//******************************************************************************

#include "hh_import.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace explorer {

namespace {

bool StartsWith(const std::string &s, const std::string &prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string Trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace((unsigned char)s[a])) ++a;
  while (b > a && isspace((unsigned char)s[b - 1])) --b;
  return s.substr(a, b - a);
}

// "$1,234.50", "1250", "€0.25" -> number; `end` = position after the number
double ParseMoney(const std::string &s, size_t from, size_t *end = NULL) {
  size_t i = from;
  while (i < s.size() && (s[i] == '$' || (unsigned char)s[i] >= 0x80 || s[i] == ' ')) ++i;
  std::string digits;
  while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '.' || s[i] == ',')) {
    if (s[i] != ',') digits += s[i];
    ++i;
  }
  if (end) *end = i;
  return digits.empty() ? 0.0 : atof(digits.c_str());
}

int RomanToInt(const std::string &r) {
  int total = 0, previous = 0;
  for (int i = (int)r.size() - 1; i >= 0; --i) {
    int v = 0;
    switch (toupper((unsigned char)r[i])) {
      case 'I': v = 1; break; case 'V': v = 5; break; case 'X': v = 10; break; case 'L': v = 50; break;
      case 'C': v = 100; break; case 'D': v = 500; break; case 'M': v = 1000; break;
      default: return 1;
    }
    if (v < previous) total -= v; else { total += v; previous = v; }
  }
  return total < 1 ? 1 : total;
}

// "2026/10/25 19:24:00" -> seconds since 1970 (local time), 0 if not a time
long long ParseTimestamp(const std::string &s, std::string *text) {
  for (size_t i = 0; i + 19 <= s.size(); ++i) {
    int y, mo, d, h, mi, se;
    if (sscanf(s.c_str() + i, "%4d/%2d/%2d %d:%2d:%2d", &y, &mo, &d, &h, &mi, &se) == 6 && y > 1990) {
      struct tm t;
      memset(&t, 0, sizeof(t));
      t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = se;
      t.tm_isdst = -1;
      char buffer[32];
      snprintf(buffer, sizeof(buffer), "%04d/%02d/%02d %02d:%02d:%02d", y, mo, d, h, mi, se);
      if (text) *text = buffer;
      return (long long)mktime(&t);
    }
  }
  return 0;
}

bool ParseCards(const std::string &line, size_t from, std::vector<std::string> *cards, size_t *end = NULL) {
  size_t open = line.find('[', from);
  if (open == std::string::npos) return false;
  size_t close = line.find(']', open);
  if (close == std::string::npos) return false;
  std::istringstream in(line.substr(open + 1, close - open - 1));
  std::string c;
  cards->clear();
  while (in >> c) cards->push_back(c);
  if (end) *end = close + 1;
  return true;
}

class HandParser {
 public:
  bool Parse(const std::vector<std::string> &lines, hh::Hand *out, std::string *error) {
    h_ = hh::Hand();
    street_ = 1;
    in_summary_ = false;
    pending_total_ = -1; pending_rake_ = 0;
    for (int i = 0; i < hh::kMaxSeats; ++i) { put_[i] = 0; ante_[i] = 0; }
    for (size_t i = 0; i < lines.size(); ++i) {
      if (!Line(lines[i], i == 0, error)) return false;
    }
    return Finish(out, error);
  }

 private:
  bool Header(const std::string &line, std::string *error) {
    size_t pos = line.find(" Hand #");
    if (pos == std::string::npos) { *error = "no header"; return false; }
    size_t id_start = pos + 7;
    size_t colon = line.find(':', id_start);
    if (colon == std::string::npos) { *error = "bad header"; return false; }
    h_.hand_id = line.substr(id_start, colon - id_start);
    std::string rest = line.substr(colon + 1);
    size_t t = rest.find("Tournament #");
    if (t != std::string::npos) {
      h_.tournament = true;
      size_t e = rest.find(',', t);
      h_.tournament_id = Trim(rest.substr(t + 12, (e == std::string::npos ? rest.size() : e) - t - 12));
      size_t level = rest.find("Level ");
      if (level != std::string::npos) {
        size_t e2 = rest.find(' ', level + 6);
        h_.level = RomanToInt(rest.substr(level + 6, e2 - level - 6));
      }
    }
    // blinds: the last "(a/b" before " - <timestamp>"
    size_t open = std::string::npos;
    for (size_t i = 0; i < rest.size(); ++i) if (rest[i] == '(' && rest.find('/', i) != std::string::npos) open = i;
    if (open == std::string::npos) { *error = "no blinds in header"; return false; }
    size_t e1;
    h_.sblind = ParseMoney(rest, open + 1, &e1);
    size_t slash = rest.find('/', e1);
    if (slash == std::string::npos) { *error = "no blinds in header"; return false; }
    h_.bblind = ParseMoney(rest, slash + 1);
    h_.start_time = ParseTimestamp(rest, &h_.timestamp);
    if (h_.start_time == 0) { *error = "no time in header"; return false; }
    return true;
  }

  int SeatOfName(const std::string &name) const {
    for (int i = 0; i < hh::kMaxSeats; ++i) if (h_.in_hand[i] && h_.name[i] == name) return i;
    return -1;
  }

  // Name at the start of `line` followed by `separator`; -1 if no player
  int PlayerAtStart(const std::string &line, const char *separator, size_t *after) const {
    int best = -1; size_t best_len = 0;
    for (int i = 0; i < hh::kMaxSeats; ++i) {
      if (!h_.in_hand[i]) continue;
      const std::string key = h_.name[i] + separator;
      if (key.size() > best_len && StartsWith(line, key)) { best = i; best_len = key.size(); }
    }
    if (best >= 0 && after) *after = best_len;
    return best;
  }

  void AddAction(int seat, hh::ActionType type, double amount, double to, double raise_by, bool allin) {
    hh::Action a;
    a.seat = seat; a.type = type; a.amount = amount; a.to = to; a.raise_by = raise_by;
    a.allin = allin; a.street = street_;
    h_.actions.push_back(a);
  }

  bool Line(const std::string &raw, bool first, std::string *error) {
    const std::string line = Trim(raw);
    if (line.empty()) return true;
    if (first) return Header(line, error);
    if (StartsWith(line, "Table '")) {
      size_t close = line.rfind('\'');
      h_.table_name = line.substr(7, close - 7);
      size_t max_pos = line.find("-max", close);
      if (max_pos != std::string::npos) {
        size_t b = max_pos;
        while (b > 0 && isdigit((unsigned char)line[b - 1])) --b;
        h_.nseats = atoi(line.substr(b, max_pos - b).c_str());
      }
      size_t seat = line.find("Seat #", close);
      if (seat != std::string::npos) h_.dealer = atoi(line.c_str() + seat + 6) - 1;
      return true;
    }
    if (StartsWith(line, "*** ")) return Marker(line);
    if (!in_summary_ && StartsWith(line, "Seat ") && line.find("in chips)") != std::string::npos) {
      int seat = atoi(line.c_str() + 5) - 1;
      size_t colon = line.find(": ");
      size_t paren = line.rfind(" (", line.find("in chips)"));
      if (seat < 0 || seat >= hh::kMaxSeats || colon == std::string::npos || paren == std::string::npos) return true;
      if (line.find("sitting out") != std::string::npos) return true;
      h_.in_hand[seat] = true;
      h_.name[seat] = line.substr(colon + 2, paren - colon - 2);
      h_.start_stack[seat] = ParseMoney(line, paren + 2);
      max_seat_ = std::max(max_seat_, seat + 1);
      return true;
    }
    if (in_summary_) return Summary(line);
    if (StartsWith(line, "Dealt to ")) {
      std::vector<std::string> cards;
      size_t bracket = line.find('[');
      if (bracket == std::string::npos || !ParseCards(line, 0, &cards) || cards.size() != 2) return true;
      int seat = SeatOfName(Trim(line.substr(9, bracket - 9)));
      if (seat >= 0) { h_.hero = seat; h_.hole[seat][0] = cards[0]; h_.hole[seat][1] = cards[1]; }
      return true;
    }
    if (StartsWith(line, "Uncalled bet")) {
      size_t at = line.find("returned to ");
      if (at != std::string::npos) {
        h_.uncalled = ParseMoney(line, line.find('(') + 1);
        h_.uncalled_seat = SeatOfName(Trim(line.substr(at + 12)));
      }
      return true;
    }
    size_t after = 0;
    int seat = PlayerAtStart(line, ": ", &after);
    if (seat >= 0) return Action(seat, line.substr(after));
    seat = PlayerAtStart(line, " collected ", &after);
    if (seat >= 0) { collected_[seat] += ParseMoney(line, after); return true; }
    return true;     // chat, "is disconnected", ...
  }

  bool Action(int seat, const std::string &text) {
    bool allin = text.find("and is all-in") != std::string::npos;
    size_t e;
    if (StartsWith(text, "posts the ante")) {
      double v = ParseMoney(text, 14, &e);
      ante_[seat] += v;
      AddAction(seat, hh::kPostAnte, v, 0, 0, allin);
    } else if (StartsWith(text, "posts small blind")) {
      double v = ParseMoney(text, 17, &e);
      put_[seat] += v;
      AddAction(seat, hh::kPostSmallBlind, v, v, 0, allin);
      if (h_.sb_seat < 0) h_.sb_seat = seat;
    } else if (StartsWith(text, "posts big blind") || StartsWith(text, "posts small & big blinds")) {
      size_t from = StartsWith(text, "posts big blind") ? 15 : 24;
      double v = ParseMoney(text, from, &e);
      put_[seat] += v;
      AddAction(seat, hh::kPostBigBlind, v, put_[seat], 0, allin);
      if (h_.bb_seat < 0) h_.bb_seat = seat;
    } else if (StartsWith(text, "folds")) {
      AddAction(seat, hh::kFold, 0, put_[seat], 0, false);
    } else if (StartsWith(text, "checks")) {
      AddAction(seat, hh::kCheck, 0, put_[seat], 0, false);
    } else if (StartsWith(text, "calls")) {
      double v = ParseMoney(text, 5, &e);
      put_[seat] += v;
      AddAction(seat, hh::kCall, v, put_[seat], 0, allin);
    } else if (StartsWith(text, "bets")) {
      double v = ParseMoney(text, 4, &e);
      put_[seat] += v;
      AddAction(seat, hh::kBet, v, put_[seat], 0, allin);
    } else if (StartsWith(text, "raises")) {
      double by = ParseMoney(text, 6, &e);
      size_t to = text.find(" to ", e);
      double total = to == std::string::npos ? by : ParseMoney(text, to + 4);
      AddAction(seat, hh::kRaise, total - put_[seat], total, by, allin);
      put_[seat] = total;
    } else if (StartsWith(text, "shows")) {
      std::vector<std::string> cards;
      if (ParseCards(text, 0, &cards) && cards.size() == 2) { h_.hole[seat][0] = cards[0]; h_.hole[seat][1] = cards[1]; }
    }
    return true;
  }

  bool Marker(const std::string &line) {
    std::vector<std::string> cards;
    if (StartsWith(line, "*** HOLE CARDS")) return true;
    if (StartsWith(line, "*** FLOP ***") || StartsWith(line, "*** TURN ***") || StartsWith(line, "*** RIVER ***")) {
      const int street = StartsWith(line, "*** FLOP") ? 2 : StartsWith(line, "*** TURN") ? 3 : 4;
      // the new card(s) are in the last bracket (flop: all three)
      size_t last_open = line.rfind('[');
      if (last_open == std::string::npos) return true;
      ParseCards(line, last_open, &cards);
      if (street == 2) { for (size_t i = 0; i < cards.size() && i < 3; ++i) h_.board[i] = cards[i]; h_.board_count = 3; }
      else if (!cards.empty() && h_.board_count < 5) h_.board[h_.board_count++] = cards[0];
      street_ = street;
      for (int i = 0; i < hh::kMaxSeats; ++i) put_[i] = 0;
      return true;
    }
    if (StartsWith(line, "*** SHOW DOWN")) { h_.showdown = true; return true; }
    if (StartsWith(line, "*** SUMMARY")) { in_summary_ = true; return true; }
    return true;
  }

  bool Summary(const std::string &line) {
    if (StartsWith(line, "Total pot")) {
      pending_total_ = ParseMoney(line, 9);
      size_t rake = line.find("Rake");
      if (rake != std::string::npos) pending_rake_ = ParseMoney(line, rake + 4);
      return true;
    }
    if (StartsWith(line, "Seat ")) {
      int seat = atoi(line.c_str() + 5) - 1;
      if (seat < 0 || seat >= hh::kMaxSeats || !h_.in_hand[seat]) return true;
      if (line.find("(small blind)") != std::string::npos) h_.sb_seat = seat;
      if (line.find("(big blind)") != std::string::npos) h_.bb_seat = seat;
      size_t showed = line.find("showed [");
      if (showed != std::string::npos) {
        std::vector<std::string> cards;
        if (ParseCards(line, showed, &cards) && cards.size() == 2 && h_.hole[seat][0].empty()) {
          h_.hole[seat][0] = cards[0]; h_.hole[seat][1] = cards[1];
        }
      }
    }
    return true;
  }

  bool Finish(hh::Hand *out, std::string *error) {
    int players = 0;
    for (int i = 0; i < hh::kMaxSeats; ++i) if (h_.in_hand[i]) ++players;
    if (players < 2) { *error = "fewer than 2 players"; return false; }
    if (h_.actions.empty()) { *error = "no actions"; return false; }
    if (h_.nseats <= 0) h_.nseats = max_seat_;
    h_.nseats = std::max(h_.nseats, max_seat_);
    for (size_t i = 0; i < h_.actions.size(); ++i) {
      const hh::Action &a = h_.actions[i];
      h_.contrib[a.seat] += a.amount;
      if (a.type == hh::kFold) h_.folded[a.seat] = true;
      if (a.type == hh::kPostAnte && h_.ante == 0) h_.ante = a.amount;
    }
    for (int i = 0; i < hh::kMaxSeats; ++i) {
      h_.collected[i] = collected_[i];
      h_.end_stack[i] = h_.start_stack[i] - h_.contrib[i] + collected_[i] +
                        (i == h_.uncalled_seat ? h_.uncalled : 0);
    }
    double sum = 0;
    for (int i = 0; i < hh::kMaxSeats; ++i) sum += h_.contrib[i];
    h_.total_pot = pending_total_ >= 0 ? pending_total_ : sum - h_.uncalled;
    h_.rake = pending_rake_;
    h_.last_street = street_;
    *out = h_;
    return true;
  }

  hh::Hand h_;
  int      street_;
  bool     in_summary_;
  int      max_seat_ = 0;
  double   put_[hh::kMaxSeats], ante_[hh::kMaxSeats], collected_[hh::kMaxSeats] = {0};
  double   pending_total_, pending_rake_;
};

bool LooksLikeHeader(const std::string &line) {
  if (line.empty() || isspace((unsigned char)line[0])) return false;
  size_t pos = line.find(" Hand #");
  return pos != std::string::npos && pos < 40 && line.find(':', pos) != std::string::npos &&
         !StartsWith(line, "Seat ") && !StartsWith(line, "Table ");
}

}  // namespace

int ParsePokerStars(const std::string &text, std::vector<hh::Hand> *hands, std::vector<std::string> *errors) {
  hands->clear();
  std::vector<std::vector<std::string> > blocks;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
    if (!line.empty() && (unsigned char)line[0] == 0xEF && line.size() >= 3) line.erase(0, 3);   // BOM
    if (LooksLikeHeader(line)) blocks.push_back(std::vector<std::string>());
    if (!blocks.empty()) blocks.back().push_back(line);
  }
  for (size_t b = 0; b < blocks.size(); ++b) {
    HandParser parser;
    hh::Hand hand;
    std::string error;
    if (parser.Parse(blocks[b], &hand, &error)) hands->push_back(hand);
    else if (errors) errors->push_back(blocks[b][0] + ": " + error);
  }
  return (int)blocks.size();
}

bool ImportText(ohstats::Db *db, const std::string &text, const std::string &site, ImportResult *result) {
  std::vector<hh::Hand> hands;
  result->hands_found = ParsePokerStars(text, &hands, &result->errors);
  result->failed = result->hands_found - (int)hands.size();
  for (size_t i = 0; i < hands.size(); ++i) {
    std::string error;
    if (db->InsertHand(hands[i], site, &error)) ++result->imported;
    else if (error == "duplicate hand") ++result->duplicates;
    else { ++result->failed; result->errors.push_back("hand " + hands[i].hand_id + ": " + error); }
  }
  return true;
}

bool ImportFile(ohstats::Db *db, const std::string &path, const std::string &site, ImportResult *result) {
  std::ifstream file(path.c_str(), std::ios::binary);
  if (!file) { result->errors.push_back("can't read " + path); return false; }
  std::stringstream buffer;
  buffer << file.rdbuf();
  return ImportText(db, buffer.str(), site, result);
}

bool ImportPath(ohstats::Db *db, const std::string &path, const std::string &site, ImportResult *result) {
  namespace fs = std::filesystem;
  std::error_code ec;
  if (!fs::is_directory(fs::path(path), ec)) return ImportFile(db, path, site, result);
  bool any = false;
  for (fs::recursive_directory_iterator it(fs::path(path), fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    std::string extension = it->path().extension().string();
    for (size_t i = 0; i < extension.size(); ++i) extension[i] = (char)tolower((unsigned char)extension[i]);
    if (extension != ".txt") continue;
    ImportResult one;
    ImportFile(db, it->path().string(), site, &one);
    result->hands_found += one.hands_found; result->imported += one.imported;
    result->duplicates += one.duplicates; result->failed += one.failed;
    for (size_t i = 0; i < one.errors.size() && result->errors.size() < 50; ++i) result->errors.push_back(one.errors[i]);
    any = true;
  }
  if (!any) result->errors.push_back("no .txt files in " + path);
  return any;
}

}  // namespace explorer
