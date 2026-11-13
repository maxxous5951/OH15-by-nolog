//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: PokerStars hand-history parser and step-by-step replay,
//   used by ManualMode to simulate a recorded hand.
//   Pure C++ (no MFC), so it can be unit-tested stand-alone.
//
//******************************************************************************

#pragma once

#include <map>
#include <string>
#include <vector>

namespace HandHistory {

struct Seat {
  int         seat;         // 1-based, as in the hand-history
  std::string name;
  double      stack;
  bool        sitting_out;
};

struct Event {
  enum Type { Post, Ante, Fold, Check, Call, Bet, Raise,
              Street, Uncalled, Collect, Show };
  Type                     type;
  std::string              player;
  double                   amount;  // calls/bets/posts: chips added; raises: raise-by
  double                   to;      // raises only: total bet after the raise
  std::vector<std::string> cards;   // streets: new board cards; show: shown cards
  std::string              text;    // original line, for display
};

struct Hand {
  std::string hand_id;
  double      sblind = 0, bblind = 0, ante = 0;
  int         limit = 0;            // 0 = NL, 1 = PL, 2 = FL (same as LIMIT_* in ManualModeDlg.h)
  bool        omaha = false;
  bool        tournament = false;
  int         button_seat = 0;
  std::vector<Seat> seats;
  std::map<std::string, std::vector<std::string> > dealt;  // "Dealt to" lines
  std::vector<Event> events;
  std::string default_hero;         // first "Dealt to" player
};

struct PlayerState {
  bool   in_hand = false;
  bool   folded = false;
  double stack = 0;                 // behind, i.e. without the current bet
  double bet = 0;                   // current bet of this street
  std::vector<std::string> cards;   // known cards (hero, or shown at showdown)
};

struct State {
  double pot = 0;                   // collected pot, without current bets
  std::vector<std::string> board;
  std::map<std::string, PlayerState> players;  // by name
};

// Returns false and fills *error if the text does not look like a hand-history.
bool Parse(const std::string& text, Hand* hand, std::string* error);

// Same, for a text with several hands (each starts with "...Hand #123:").
// Hands that can not be parsed are skipped; fails only if none is usable.
bool ParseAll(const std::string& text, std::vector<Hand>* hands, std::string* error);

// Is this event an action of the hero (fold, check, call, bet, raise)?
bool IsDecision(const Event& event, const std::string& hero);

// State of the table after the first <steps> events. steps == 0 is the
// situation before any blind is posted. Replaying from scratch every time
// makes "step back" trivial.
State Replay(const Hand& hand, const std::string& hero, size_t steps);

}  // namespace HandHistory
