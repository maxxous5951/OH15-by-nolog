//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: PokerStars hand-history parser and replay (see HandHistory.h)
//
//******************************************************************************

#include "stdafx.h"
#include "HandHistory.h"
#include <cstdlib>
#include <regex>
#include <sstream>

namespace HandHistory {

// Keeps digits and '.', so "$1,042", "€6" and "1042" all work.
static double Money(const std::string& s) {
  std::string digits;
  for (size_t i = 0; i < s.size(); ++i) {
    if ((s[i] >= '0' && s[i] <= '9') || s[i] == '.') digits += s[i];
  }
  return digits.empty() ? 0.0 : atof(digits.c_str());
}

static std::string Trim(const std::string& s) {
  size_t b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return "";
  size_t e = s.find_last_not_of(" \t\r\n");
  return s.substr(b, e - b + 1);
}

// Cards inside the LAST [...] of a line:
//   "*** TURN *** [6d Jh Th] [6h]"  ->  { "6h" }
static std::vector<std::string> LastBracketCards(const std::string& line) {
  std::vector<std::string> cards;
  size_t end = line.rfind(']');
  if (end == std::string::npos) return cards;
  size_t begin = line.rfind('[', end);
  if (begin == std::string::npos) return cards;
  std::istringstream in(line.substr(begin + 1, end - begin - 1));
  std::string c;
  while (in >> c) cards.push_back(c);
  return cards;
}

bool Parse(const std::string& text, Hand* hand, std::string* error) {
  *hand = Hand();
  const std::string money = R"re((\S*?\d[\d.,]*))re";
  const std::regex re_header(R"re(Hand #(\d+):.*?\()re" + money + "/" + money);
  const std::regex re_button(R"re(Seat #(\d+) is the button)re");
  const std::regex re_seat(R"re(^Seat (\d+): (.+) \()re" + money + R"re( in chips\)(.*)$)re");
  const std::regex re_dealt(R"re(^Dealt to (.+?) \[([^\]]+)\])re");
  const std::regex re_post(R"re(^(.+?): posts (the ante|small blind|big blind|small & big blinds) )re" + money + ".*");
  const std::regex re_action(R"re(^(.+?): (folds|checks|calls|bets|raises)(?: )re" + money + R"re()?(?: to )re" + money + R"re()?.*$)re");
  const std::regex re_street(R"re(^\*\*\* (FLOP|TURN|RIVER) \*\*\* .*$)re");
  const std::regex re_uncalled(R"re(^Uncalled bet \()re" + money + R"re(\) returned to (.+)$)re");
  const std::regex re_collect(R"re(^(.+?) collected )re" + money + R"re( from (?:the )?(?:main |side )?pot.*$)re");
  const std::regex re_show(R"re(^(.+?): shows \[([^\]]+)\].*$)re");

  std::istringstream in(text);
  std::string raw;
  bool first_line = true;
  while (std::getline(in, raw)) {
    const std::string line = Trim(raw);
    if (line.empty()) continue;
    if (line.find("*** SUMMARY ***") != std::string::npos) break;
    std::smatch m;
    if (first_line || line.find("Hand #") != std::string::npos) {
      if (std::regex_search(line, m, re_header)) {
        hand->hand_id = m[1];
        hand->sblind = Money(m[2]);
        hand->bblind = Money(m[3]);
        hand->omaha = line.find("Omaha") != std::string::npos;
        hand->tournament = line.find("Tournament #") != std::string::npos;
        if (line.find("No Limit") != std::string::npos) hand->limit = 0;
        else if (line.find("Pot Limit") != std::string::npos) hand->limit = 1;
        else hand->limit = 2;
        first_line = false;
        continue;
      }
    }
    first_line = false;
    if (std::regex_search(line, m, re_button)) { hand->button_seat = atoi(m[1].str().c_str()); continue; }
    if (std::regex_match(line, m, re_seat)) {
      Seat s;
      s.seat = atoi(m[1].str().c_str());
      s.name = m[2];
      s.stack = Money(m[3]);
      s.sitting_out = m[4].str().find("sitting out") != std::string::npos;
      hand->seats.push_back(s);
      continue;
    }
    if (std::regex_search(line, m, re_dealt)) {
      std::vector<std::string> cards;
      std::istringstream cs(m[2]);
      std::string c;
      while (cs >> c) cards.push_back(c);
      hand->dealt[m[1]] = cards;
      if (hand->default_hero.empty()) hand->default_hero = m[1];
      continue;
    }
    Event e;
    e.amount = 0; e.to = 0; e.text = line;
    if (std::regex_match(line, m, re_post)) {
      e.type = (m[2] == "the ante") ? Event::Ante : Event::Post;
      e.player = m[1];
      e.amount = Money(m[3]);
      if (e.type == Event::Ante && hand->ante == 0) hand->ante = e.amount;
    } else if (std::regex_match(line, m, re_action)) {
      const std::string verb = m[2];
      e.type = verb == "folds" ? Event::Fold : verb == "checks" ? Event::Check :
               verb == "calls" ? Event::Call : verb == "bets" ? Event::Bet : Event::Raise;
      e.player = m[1];
      e.amount = m[3].matched ? Money(m[3]) : 0;
      e.to = m[4].matched ? Money(m[4]) : 0;
      // Old-style "raises to X" without "by": treat X as the total.
      if (e.type == Event::Raise && e.to == 0) e.to = e.amount;
    } else if (std::regex_match(line, m, re_street)) {
      e.type = Event::Street;
      e.cards = LastBracketCards(line);
    } else if (std::regex_match(line, m, re_uncalled)) {
      e.type = Event::Uncalled;
      e.amount = Money(m[1]);
      e.player = m[2];
    } else if (std::regex_match(line, m, re_collect)) {
      e.type = Event::Collect;
      e.player = m[1];
      e.amount = Money(m[2]);
    } else if (std::regex_match(line, m, re_show)) {
      e.type = Event::Show;
      e.player = m[1];
      std::istringstream cs(m[2]);
      std::string c;
      while (cs >> c) e.cards.push_back(c);
    } else {
      continue;  // mucks, chat, "*** HOLE CARDS ***", ...
    }
    hand->events.push_back(e);
  }
  if (hand->seats.empty()) {
    *error = "No \"Seat N: name ($x in chips)\" lines found.\nIs this a PokerStars hand-history?";
    return false;
  }
  if (hand->events.empty()) {
    *error = "No actions found in this hand-history.";
    return false;
  }
  return true;
}

bool ParseAll(const std::string& text, std::vector<Hand>* hands, std::string* error) {
  hands->clear();
  const std::regex re_start(R"re(Hand #\d+:)re");
  std::vector<std::string> chunks;
  std::string current, line;
  std::istringstream in(text);
  while (std::getline(in, line)) {
    if (std::regex_search(line, re_start)) {
      if (!Trim(current).empty()) chunks.push_back(current);
      current.clear();
    }
    current += line;
    current += "\n";
  }
  if (!Trim(current).empty()) chunks.push_back(current);
  std::string first_error;
  for (size_t i = 0; i < chunks.size(); ++i) {
    Hand hand;
    std::string chunk_error;
    if (Parse(chunks[i], &hand, &chunk_error)) hands->push_back(hand);
    else if (first_error.empty()) first_error = chunk_error;
  }
  if (hands->empty()) {
    *error = first_error.empty() ? "No hand found." : first_error;
    return false;
  }
  return true;
}

bool IsDecision(const Event& e, const std::string& hero) {
  if (hero.empty() || e.player != hero) return false;
  return e.type == Event::Fold || e.type == Event::Check || e.type == Event::Call ||
         e.type == Event::Bet || e.type == Event::Raise;
}

State Replay(const Hand& hand, const std::string& hero, size_t steps) {
  State s;
  for (size_t i = 0; i < hand.seats.size(); ++i) {
    PlayerState p;
    p.in_hand = !hand.seats[i].sitting_out;
    p.stack = hand.seats[i].stack;
    s.players[hand.seats[i].name] = p;
  }
  std::map<std::string, std::vector<std::string> >::const_iterator d = hand.dealt.find(hero);
  if (d != hand.dealt.end() && s.players.count(hero)) s.players[hero].cards = d->second;

  if (steps > hand.events.size()) steps = hand.events.size();
  for (size_t i = 0; i < steps; ++i) {
    const Event& e = hand.events[i];
    if (e.type == Event::Street) {
      for (std::map<std::string, PlayerState>::iterator it = s.players.begin(); it != s.players.end(); ++it) {
        s.pot += it->second.bet;
        it->second.bet = 0;
      }
      s.board.insert(s.board.end(), e.cards.begin(), e.cards.end());
      continue;
    }
    std::map<std::string, PlayerState>::iterator it = s.players.find(e.player);
    if (it == s.players.end()) continue;
    PlayerState& p = it->second;
    switch (e.type) {
      case Event::Post:  p.stack -= e.amount; p.bet += e.amount; break;
      case Event::Ante:  p.stack -= e.amount; s.pot += e.amount; break;
      case Event::Call:  p.stack -= e.amount; p.bet += e.amount; break;
      case Event::Bet:   p.stack -= e.amount; p.bet = e.amount; break;
      case Event::Raise: p.stack -= (e.to - p.bet); p.bet = e.to; break;
      case Event::Fold:  p.folded = true; break;
      case Event::Uncalled: p.bet -= e.amount; p.stack += e.amount; break;
      case Event::Show:  p.cards = e.cards; break;
      case Event::Collect: {
        for (std::map<std::string, PlayerState>::iterator q = s.players.begin(); q != s.players.end(); ++q) {
          s.pot += q->second.bet;
          q->second.bet = 0;
        }
        p.stack += e.amount;
        s.pot = (s.pot > e.amount) ? s.pot - e.amount : 0;
        break;
      }
      default: break;
    }
  }
  return s;
}

}  // namespace HandHistory
