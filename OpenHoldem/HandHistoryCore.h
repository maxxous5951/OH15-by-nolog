//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Core of the hand history generator (NL Hold'em cash games and
//   tournaments; the reconstruction is identical, only the output differs).
//
//   OpenHoldem only sees the table once per heartbeat. This module takes these
//   "snapshots", reconstructs the actions of the hand and checks that the
//   result is consistent. Hands that fail the checks are reported with the
//   reasons instead of being written to the hand history.
//
//   Idea of the reconstruction:
//     * a seat that bets more than before has called / bet / raised
//     * a seat that is no longer active has folded
//     * actions that happen between two heartbeats are put in table order
//     * when a betting round ends, the last (unseen) calls and checks are
//       inferred, because the bets are swept into the pot at that moment
//     * the winnings are not scraped but computed from the change of the stacks
//
//   This header uses the C++ standard library only (no MFC / OpenHoldem),
//   so it can be unit-tested anywhere.
//
//******************************************************************************

#ifndef INC_HANDHISTORYCORE_H
#define INC_HANDHISTORYCORE_H

#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace hh {

const int    kMaxSeats = 10;
const double kEps      = 0.005;   // amounts smaller than half a cent are noise

//------------------------------------------------------------------------------
// Input: what the scraper sees at one moment
//------------------------------------------------------------------------------

struct SeatSnapshot {
  bool        seated;      // somebody sits here
  bool        active;      // still in the hand (cards visible / not folded)
  std::string name;
  double      balance;     // chips behind
  double      bet;         // chips in front of the player in the current round
  std::string cards[2];    // known hole cards like "Ah", "" if unknown
  SeatSnapshot() : seated(false), active(false), balance(0), bet(0) {}
};

struct Snapshot {
  int         betround;    // 1 = preflop, 2 = flop, 3 = turn, 4 = river
  int         nseats;      // number of chairs of the table
  int         dealer;      // chair of the button, -1 unknown
  int         hero;        // chair of the user, -1 unknown
  double      sblind, bblind, ante;
  double      pot;         // pot as scraped, 0 if unknown
  long long   time;        // seconds since 1970 (for the time of the hand)
  std::string board[5];    // known community cards, "" if not dealt yet
  SeatSnapshot seat[kMaxSeats];
  Snapshot() : betround(1), nseats(0), dealer(-1), hero(-1),
               sblind(0), bblind(0), ante(0), pot(0), time(0) {}
};

//------------------------------------------------------------------------------
// Output: the reconstructed hand
//------------------------------------------------------------------------------

enum ActionType {
  kPostAnte, kPostSmallBlind, kPostBigBlind,
  kFold, kCheck, kCall, kBet, kRaise
};

struct Action {
  int        seat;
  ActionType type;
  double     amount;     // chips added by this action
  double     to;         // total bet of the seat in this round after the action
  double     raise_by;   // for kRaise: increase over the previous bet
  bool       allin;
  int        street;     // 1 = preflop .. 4 = river
  Action() : seat(-1), type(kFold), amount(0), to(0), raise_by(0), allin(false), street(1) {}
};

struct Hand {
  // Filled in by the caller
  std::string hand_id, table_name, timestamp;
  long long   start_time;
  // Tournament hands (set by the caller, see HandHistoryFormat.h):
  // chips instead of money, no rake, "Tournament #id ... Level x" in the header
  bool        tournament;
  std::string tournament_id;
  int         level;                    // blind level, 1 = first level
  // Table
  int    nseats, dealer, hero, sb_seat, bb_seat;
  double sblind, bblind, ante;
  // Players (indexed by chair)
  bool        in_hand[kMaxSeats];       // dealt in
  bool        folded[kMaxSeats];
  std::string name[kMaxSeats];
  double      start_stack[kMaxSeats];
  double      end_stack[kMaxSeats];
  std::string hole[kMaxSeats][2];
  // Course of the hand
  std::vector<Action> actions;
  std::string board[5];
  int    board_count;
  int    last_street;
  bool   showdown;
  // Money
  double contrib[kMaxSeats];            // total chips put in the pot
  double collected[kMaxSeats];          // chips won from the pot
  double uncalled;                      // part of a bet that nobody called
  int    uncalled_seat;
  double total_pot;                     // after returning the uncalled bet, before rake
  double rake;
  // Pot at the end of each betting round: computed by us / scraped (0 = unknown)
  double pot_seen[5];
  double pot_scraped[5];
  // Everything that makes this hand unreliable. Empty = hand is consistent.
  std::vector<std::string> problems;
  // What the scraper saw (one line per change) and what the reconstructor did about it.
  // Only printed for rejected hands, to find out why a hand was rejected.
  std::vector<std::string> trace;

  Hand() { Clear(); }
  bool valid() const { return problems.empty(); }
  void Clear() {
    hand_id = table_name = timestamp = "";
    start_time = 0;
    tournament = false; tournament_id = ""; level = 1;
    nseats = 0; dealer = hero = sb_seat = bb_seat = -1;
    sblind = bblind = ante = 0;
    for (int i = 0; i < kMaxSeats; i++) {
      in_hand[i] = false; folded[i] = false; name[i] = "";
      start_stack[i] = end_stack[i] = 0;
      hole[i][0] = hole[i][1] = "";
      contrib[i] = collected[i] = 0;
    }
    actions.clear();
    for (int i = 0; i < 5; i++) { board[i] = ""; pot_seen[i] = 0; pot_scraped[i] = 0; }
    board_count = 0; last_street = 1; showdown = false;
    uncalled = 0; uncalled_seat = -1; total_pot = 0; rake = 0;
    problems.clear();
    trace.clear();
  }
};

struct Options {
  bool   check_pot;       // compare our pot with the scraped pot (needs a reliable pot region)
  double pot_tolerance;   // relative tolerance for that check
  double max_rake;        // rake above this share of the pot is suspicious
  // A bet has to be paid from the stack of the player: balance + bet stays constant.
  // Bets that do not fit (e.g. the pot that is shown in front of the winner at the
  // showdown) are ignored. Disable for casinos that do not reduce the balance
  // while the bet is in front of the player.
  bool   check_stack_for_bets;
  Options() : check_pot(false), pot_tolerance(0.02), max_rake(0.10), check_stack_for_bets(true) {}
};

//------------------------------------------------------------------------------
// The reconstructor
//------------------------------------------------------------------------------

class Reconstructor {
 public:
  Reconstructor() : completed_idle_(0) { Reset(); }

  // Forget the hand in progress (new connection to a table).
  // Hands that are already finished stay available through PopCompleted().
  void Reset() {
    state_ = kIdle;
    h_.Clear();
    over_ = false;
    street_ = 1; to_act_ = 0; cur_bet_ = 0; n_ = 0;
    have_last_ = false;
    hint_ = false;
    idle_ = 0;
    closing_ = false;
    closing_beats_ = 0;
    scraped_sb_ = scraped_bb_ = 0;
  }

  void SetOptions(const Options &options) { opts_ = options; }

  // OpenHoldem believes that a new hand has started. This is only a hint:
  // the reconstructor decides itself, because the signal comes too late
  // or even too early at some casinos.
  void HandResetHint() { hint_ = true; }

  // Call once per heartbeat with the current table.
  // Finished hands can be fetched with PopCompleted().
  void Feed(const Snapshot &s) {
    bool hint = hint_;
    hint_ = false;
    bool started_now = false;

    if (state_ == kRunning) {
      if (closing_ || NewHandSignal(s, hint)) {
        // The old hand is over. Its end stacks are taken from the first snapshot
        // of the new hand (balance + bet = stacks before the new blinds), but
        // balance and bet are not always updated in the same heartbeat: a blind
        // may have left the stack while its bet is not visible yet.
        // Therefore wait a few heartbeats until the blinds are visible.
        if (BlindsVisible(s) || closing_beats_ >= kClosingBeats) {
          Trace(s, "NEXT> ");
          FinishCurrent(&s);
          closing_ = false;
          closing_beats_ = 0;
          state_ = kWaitingForBlinds;
        } else {
          closing_ = true;
          closing_beats_++;
          Trace(s, "NEXT> ");
          return;       // 'last_' stays the last snapshot of the finished hand
        }
      }
    } else if (state_ == kDone) {
      state_ = kWaitingForBlinds;                  // wait for the next blinds
    } else if (state_ == kSkipped) {
      if (NewHandSignal(s, hint) && s.betround == 1) state_ = kWaitingForBlinds;
    } else if (state_ == kIdle) {
      // A hand that we did not see from the start can not be reconstructed
      state_ = (s.betround == 1) ? kWaitingForBlinds : kSkipped;
    }

    if (state_ == kWaitingForBlinds) {
      if (!TryStart(s)) { last_ = s; have_last_ = true; return; }
      started_now = true;
      Trace(s, "START ");
    }
    if (state_ == kRunning) {
      if (!started_now && have_last_ && !SameState(s, last_)) Trace(s, "");
      Update(s);
      // A finished hand on a table that does not change any more is written
      // without waiting for the next hand (e.g. last hand of a session).
      if (SameState(s, last_)) idle_++; else idle_ = 0;
      if (idle_ == kIdleBeats) FinalizeIfConsistent();
    }
    last_ = s; have_last_ = true;
  }

  // Returns the oldest finished hand (valid or not), false if there is none.
  bool PopCompleted(Hand *out) {
    if (completed_.empty()) return false;
    *out = completed_.front();
    completed_.erase(completed_.begin());
    return true;
  }

  // Finishes the hand in progress right now (using the last snapshot).
  // Returns true if a hand was observed from its start; the hand may still be
  // invalid (see Hand::problems). Afterwards the reconstructor is reset.
  bool Finish(Hand *out, const Options &options = Options()) {
    bool result = false;
    if (state_ == kRunning && have_last_) {
      FinishHand(options);
      *out = h_;
      result = true;
    }
    Reset();
    return result;
  }

  bool HandInProgress() const { return state_ == kRunning; }

 private:
  enum State { kIdle, kWaitingForBlinds, kRunning, kSkipped, kDone };
  static const int kIdleBeats = 15;      // identical snapshots before a finished hand is written
  static const int kClosingBeats = 4;    // max. heartbeats to wait for the blinds of the next hand
  static const size_t kMaxTrace = 400;   // lines of Hand::trace

  State    state_;
  Hand     h_;
  Snapshot last_;
  bool     have_last_;
  bool     hint_;
  int      idle_;
  int      completed_idle_;
  Options  opts_;
  std::vector<Hand> completed_;
  bool     over_;                     // only one player left
  int      n_;                        // number of chairs
  int      street_;
  int      to_act_;                   // where to look for the next action
  double   cur_bet_;                  // highest bet in the current round
  double   seat_bet_[kMaxSeats];      // bet in front of the seat in this round
  double   contrib_[kMaxSeats];       // chips from earlier rounds
  bool     acted_[kMaxSeats];
  bool     folded_[kMaxSeats];
  bool     allin_[kMaxSeats];
  bool     in_hand_[kMaxSeats];
  bool     reappeared_[kMaxSeats];
  double   last_stack_[kMaxSeats];   // chips behind at the last snapshot
  bool     closing_;                  // old hand is over, waiting for the blinds of the next one
  int      closing_beats_;
  double   scraped_sb_, scraped_bb_;  // blinds as scraped (may be wrong, see TryStart)
  bool     blind_observed_[kMaxSeats];// blind of this seat taken from its bet (not guessed)
  bool     track_stack_[kMaxSeats];   // balance of this seat is scraped (> 0 at the start)
  double   ignored_bet_[kMaxSeats];   // last bet that was ignored (to log it only once)

  // ---- helpers -------------------------------------------------------------

  bool CanAct(int i) const { return in_hand_[i] && !folded_[i] && !allin_[i]; }

  int Alive() const {
    int c = 0;
    for (int i = 0; i < n_; i++) if (in_hand_[i] && !folded_[i]) c++;
    return c;
  }

  int NextCanAct(int from) const {   // first seat after 'from' that can act
    for (int k = 1; k <= n_; k++) {
      int i = (from + k) % n_;
      if (CanAct(i)) return i;
    }
    return (from + 1) % n_;
  }

  void Add(int seat, ActionType type, double amount, double to, bool allin,
           double raise_by = 0) {
    Action a;
    a.seat = seat; a.type = type; a.amount = amount; a.to = to;
    a.allin = allin; a.raise_by = raise_by; a.street = street_;
    h_.actions.push_back(a);
  }

  void Problem(const std::string &text) { h_.problems.push_back(text); }

  // ---- plausibility of what the scraper sees ----------------------------------

  // At least two bets in front of players: the blinds of a new hand
  static bool BlindsVisible(const Snapshot &s) {
    int n = 0;
    for (int i = 0; i < s.nseats && i < kMaxSeats; i++)
      if (s.seat[i].seated && s.seat[i].active && s.seat[i].bet > kEps) n++;
    return n >= 2;
  }

  // A bet is paid from the stack: balance + contributions + bet stays constant.
  // Something else in front of a player (e.g. the pot shown in front of the winner
  // at the showdown) is not a bet. Balance and bet are not always updated in the
  // same heartbeat, therefore an unsupported bet is only postponed: it is
  // accepted as soon as the balance has caught up.
  bool BetSupported(int i, const SeatSnapshot &p) {
    if (!opts_.check_stack_for_bets || !track_stack_[i]) return true;
    double delta = p.bet - seat_bet_[i];
    double expected = h_.start_stack[i] - contrib_[i] - p.bet;   // balance if the bet came from the stack
    if (expected < 0) expected = 0;
    if (fabs(p.balance - expected) <= 0.5 * delta + kEps) return true;
    if (fabs(ignored_bet_[i] - p.bet) > kEps) {
      ignored_bet_[i] = p.bet;
      Note("seat " + Num(i + 1) + ": bet " + Money(p.bet) + " ignored, stack is " +
           Money(p.balance) + " but should be " + Money(expected) + " if it was a bet");
    }
    return false;
  }

  // At the start of a hand the balance of a blind can already be reduced while
  // its bet is not visible yet, so the stack before the blinds is too small.
  // When the bet shows up, balance + bet is more than the start stack, and the
  // difference is exactly the blind that was missing.
  void HealStart(int i, const SeatSnapshot &p) {
    if (street_ != 1 || !track_stack_[i]) return;
    if (i != h_.sb_seat && i != h_.bb_seat) return;
    double excess = p.balance + contrib_[i] + p.bet - h_.start_stack[i];
    if (excess <= 2 * kEps || excess > p.bet + kEps) return;
    h_.start_stack[i] += excess;
    Note("seat " + Num(i + 1) + ": start stack corrected by " + Money(excess) +
         " (blind was not visible in the first snapshot)");
    if (blind_observed_[i]) return;
    // The blind itself was a guess so far: it is what left the stack
    blind_observed_[i] = true;
    seat_bet_[i] = excess;
    for (size_t a = 0; a < h_.actions.size(); a++) {
      Action &x = h_.actions[a];
      if (x.seat == i && (x.type == kPostSmallBlind || x.type == kPostBigBlind)) { x.amount = excess; x.to = excess; }
    }
    if (i == h_.sb_seat) h_.sblind = excess; else h_.bblind = excess;
    Note(std::string(i == h_.sb_seat ? "small" : "big") + " blind is " + Money(excess));
    cur_bet_ = 0;
    for (int j = 0; j < n_; j++) if (seat_bet_[j] > cur_bet_) cur_bet_ = seat_bet_[j];
    RecomputePreflopAmounts();
  }

  // After a blind has been corrected: amounts of the preflop actions that were
  // computed with the wrong blind
  void RecomputePreflopAmounts() {
    double prev_to[kMaxSeats];
    for (int i = 0; i < kMaxSeats; i++) prev_to[i] = 0;
    double level = 0;
    for (size_t a = 0; a < h_.actions.size(); a++) {
      Action &x = h_.actions[a];
      if (x.street != 1 || x.seat < 0 || x.seat >= kMaxSeats) continue;
      switch (x.type) {
        case kPostSmallBlind: case kPostBigBlind:
          prev_to[x.seat] = x.to; if (x.to > level) level = x.to; break;
        case kCall:
          x.amount = x.to - prev_to[x.seat]; prev_to[x.seat] = x.to; break;
        case kBet: case kRaise:
          x.amount = x.to - prev_to[x.seat];
          x.raise_by = x.to - level;
          prev_to[x.seat] = x.to; if (x.to > level) level = x.to; break;
        default: break;
      }
    }
  }

  // ---- diagnostics -----------------------------------------------------------

  void Note(const std::string &text) { AddTrace("# " + text); }
  void Trace(const Snapshot &s, const char *prefix) { AddTrace(Describe(s, prefix)); }

  void AddTrace(const std::string &line) {
    if (h_.trace.size() > kMaxTrace) return;
    if (h_.trace.size() == kMaxTrace) { h_.trace.push_back("... (trace truncated)"); return; }
    h_.trace.push_back(line);
  }

  std::string Describe(const Snapshot &s, const char *prefix) const {
    char b[320];
    std::string out = prefix;
    snprintf(b, sizeof(b), "+%ds br=%d btn=%d pot=%.2f blinds=%.2f/%.2f board=",
             (int)(s.time - h_.start_time), s.betround, s.dealer + 1, s.pot, s.sblind, s.bblind);
    out += b;
    bool any = false;
    for (int i = 0; i < 5; i++) {
      if (s.board[i].empty()) continue;
      if (any) out += " ";
      out += s.board[i]; any = true;
    }
    if (!any) out += "-";
    for (int i = 0; i < s.nseats && i < kMaxSeats; i++) {
      const SeatSnapshot &p = s.seat[i];
      if (!p.seated) continue;
      std::string name = p.name.substr(0, 20);
      for (size_t c = 0; c < name.size(); c++) if (name[c] == '\r' || name[c] == '\n') name[c] = '_';
      snprintf(b, sizeof(b), " | S%d%s %s%s bal=%.2f bet=%.2f", i + 1, (i == s.hero) ? "*" : "",
               name.c_str(), p.active ? "" : "(out)", p.balance, p.bet);
      out += b;
      if (!p.cards[0].empty() && !p.cards[1].empty()) out += " [" + p.cards[0] + " " + p.cards[1] + "]";
    }
    return out;
  }

  // ---- hand boundaries -----------------------------------------------------

  static bool SameState(const Snapshot &a, const Snapshot &b) {
    if (a.betround != b.betround) return false;
    for (int i = 0; i < 5; i++) if (a.board[i] != b.board[i]) return false;
    for (int i = 0; i < a.nseats && i < kMaxSeats; i++) {
      const SeatSnapshot &x = a.seat[i], &y = b.seat[i];
      if (x.seated != y.seated || x.active != y.active) return false;
      if (fabs(x.balance - y.balance) > kEps || fabs(x.bet - y.bet) > kEps) return false;
    }
    return true;
  }

  // Does snapshot 's' belong to a different hand than the previous snapshot?
  // Several independent signals, because any single one can be missing:
  // the user's cards are not always visible, the button is not always
  // detected, and at some casinos the hands follow each other within a second.
  bool NewHandSignal(const Snapshot &s, bool hint) const {
    if (!have_last_) return false;
    const Snapshot &p = last_;
    const bool running = (state_ == kRunning);

    // 1. The user got different hole cards than before
    //    (compared with the cards of this hand and with the previous snapshot,
    //    because the cards are not visible all the time, e.g. after a fold)
    if (s.hero >= 0 && s.hero < kMaxSeats && !s.seat[s.hero].cards[0].empty() && !s.seat[s.hero].cards[1].empty()) {
      const SeatSnapshot &b = s.seat[s.hero];
      const SeatSnapshot &a = p.seat[s.hero];
      if (!a.cards[0].empty() && !a.cards[1].empty() && (a.cards[0] != b.cards[0] || a.cards[1] != b.cards[1])) return true;
      if (running && !h_.hole[s.hero][0].empty() && (h_.hole[s.hero][0] != b.cards[0] || h_.hole[s.hero][1] != b.cards[1])) return true;
    }
    // 2. Betting rounds do not go backwards
    if (s.betround < p.betround) return true;
    // 3. A bet does not shrink to another positive amount within a round
    //    (while a hand is going on; after a hand the bets get returned)
    if (s.betround == p.betround && running && !over_) {
      for (int i = 0; i < s.nseats && i < kMaxSeats; i++) {
        if (!in_hand_[i] || !p.seat[i].seated || !s.seat[i].seated) continue;
        if (s.seat[i].bet > kEps && s.seat[i].bet + kEps < p.seat[i].bet) return true;
      }
    }
    // 4. The button moved (it never moves during a hand)
    {
      int ref = running ? h_.dealer : p.dealer;
      if (s.dealer >= 0 && ref >= 0 && s.dealer != ref) return true;
    }
    // 5. The blind levels changed (not within a hand)
    // (compared with the scraped values, the blinds of the hand may have been
    // corrected by the reconstructor)
    if (running && s.sblind > kEps && s.bblind > kEps && scraped_sb_ > kEps && scraped_bb_ > kEps &&
        (fabs(s.sblind - scraped_sb_) > kEps || fabs(s.bblind - scraped_bb_) > kEps)) return true;
    // 6. Players that had folded are in the game again
    if (over_ || !running) {
      for (int i = 0; i < s.nseats && i < kMaxSeats; i++)
        if (p.seat[i].seated && !p.seat[i].active && s.seat[i].seated && s.seat[i].active) return true;
    }
    (void) hint;
    return false;
  }

  // The hand in progress is over; 'next' (if any) is the first snapshot of the next hand
  void FinishCurrent(const Snapshot *next) {
    if (state_ != kRunning) return;
    if (next != NULL && next->betround == 1) {
      // Chips never disappear after a hand: if the payout was already visible
      // at the end of the old hand, keep that (the first snapshot of the new
      // hand can show a blind that has left the stack with its bet still missing)
      for (int i = 0; i < n_; i++) {
        if (!in_hand_[i] || !next->seat[i].seated) continue;
        double candidate = next->seat[i].balance + next->seat[i].bet;
        if (candidate > last_stack_[i] + kEps) last_stack_[i] = candidate;
      }
    }
    FinishHand(opts_);
    completed_.push_back(h_);
  }

  void FinalizeIfConsistent() {
    Reconstructor trial = *this;
    trial.FinishHand(opts_);
    if (trial.h_.problems.empty()) {
      completed_.push_back(trial.h_);
      state_ = kDone;
    }
  }

  // ---- start of the hand ---------------------------------------------------

  bool TryStart(const Snapshot &s) {
    int dealt = 0;
    bool any_bet = false;
    for (int i = 0; i < s.nseats && i < kMaxSeats; i++) {
      if (s.seat[i].seated && s.seat[i].active) dealt++;
      if (s.seat[i].bet > kEps) any_bet = true;
    }
    if (dealt < 2 || !any_bet || s.dealer < 0 || s.dealer >= s.nseats) return false;

    h_.Clear();
    n_ = s.nseats;
    h_.nseats = n_; h_.dealer = s.dealer; h_.hero = s.hero;
    h_.start_time = s.time;
    idle_ = 0;
    h_.ante = s.ante;
    scraped_sb_ = s.sblind; scraped_bb_ = s.bblind;
    street_ = 1; over_ = false; cur_bet_ = 0;

    for (int i = 0; i < kMaxSeats; i++) {
      seat_bet_[i] = contrib_[i] = 0;
      acted_[i] = folded_[i] = allin_[i] = in_hand_[i] = reappeared_[i] = false;
      last_stack_[i] = 0;
      blind_observed_[i] = track_stack_[i] = false;
      ignored_bet_[i] = 0;
    }
    for (int i = 0; i < n_; i++) {
      const SeatSnapshot &p = s.seat[i];
      if (p.seated && p.active) {
        in_hand_[i] = h_.in_hand[i] = true;
        h_.name[i] = p.name;
        h_.start_stack[i] = p.balance + p.bet;     // before the blinds
        last_stack_[i] = p.balance;                // chips behind; see ComputeMoney()
        track_stack_[i] = (p.balance > kEps);      // else the balance is not scraped (or all-in)
      }
    }

    // Blinds: first two players clockwise from the button
    // (heads-up: the button is the small blind)
    int sb = -1, bb = -1;
    if (dealt == 2) {
      if (in_hand_[s.dealer]) {
        sb = s.dealer;
        for (int k = 1; k <= n_; k++) { int i = (s.dealer + k) % n_; if (in_hand_[i] && i != sb) { bb = i; break; } }
      }
    } else {
      int found = 0;
      for (int k = 1; k <= n_ && found < 2; k++) {
        int i = (s.dealer + k) % n_;
        if (!in_hand_[i]) continue;
        if (found == 0) sb = i; else bb = i;
        found++;
      }
    }
    if (sb < 0 || bb < 0) { Problem("blinds could not be assigned"); sb = bb = -1; }
    h_.sb_seat = sb; h_.bb_seat = bb;

    // The scraped blind sizes are only a hint (some tablemaps do not scrape them
    // and OpenHoldem falls back to a default like 0.01/0.02). What the blinds
    // actually put in front of them wins; a blind that is not visible yet
    // is found later, see HealStart().
    double sbl = s.sblind, bbl = s.bblind;
    bool sb_overridden = false, bb_overridden = false;
    if (sb >= 0) {
      double ob = s.seat[sb].bet;
      if (ob > kEps) blind_observed_[sb] = true;
      // (a small blind that completes to the big blind is not a different blind)
      if (ob > kEps && s.seat[sb].balance > kEps && fabs(ob - sbl) > kEps && fabs(ob - bbl) > kEps) {
        sbl = ob; sb_overridden = true;
      }
    }
    if (bb >= 0) {
      double ob = s.seat[bb].bet;
      if (ob > kEps) blind_observed_[bb] = true;
      if (ob > kEps && s.seat[bb].balance > kEps && fabs(ob - bbl) > kEps) {
        bbl = ob; bb_overridden = true;
      }
    }
    if (sb_overridden && !bb_overridden && sbl >= bbl - kEps) bbl = 2 * sbl;   // provisional
    h_.sblind = sbl; h_.bblind = bbl;
    if (sb_overridden && !bb_overridden && sbl >= s.bblind - kEps)
      Note("small blind taken from its bet at the table: " + Money(sbl) + " (scraped blinds " +
           Money(s.sblind) + "/" + Money(s.bblind) + "), big blind not visible yet");
    else if (sb_overridden || bb_overridden)
      Note("blinds taken from the bets at the table: " + Money(sbl) + "/" + Money(bbl) +
           " (scraped " + Money(s.sblind) + "/" + Money(s.bblind) + ")");
    if (sbl <= 0 || bbl <= 0) Problem("blind sizes unknown");

    // Antes: small bets of players that are not in the blinds
    if (s.ante > kEps) {
      for (int i = 0; i < n_; i++) {
        if (!in_hand_[i] || i == sb || i == bb) continue;
        double b = s.seat[i].bet;
        if (b > kEps && b <= s.ante + kEps) {
          Add(i, kPostAnte, b, b, s.seat[i].balance <= kEps);
          contrib_[i] += b;
        }
      }
    }
    if (sb >= 0) {
      double ob = s.seat[sb].bet;
      double posted = (ob > kEps) ? (sb_overridden ? ob : (ob < sbl ? ob : sbl)) : sbl;
      seat_bet_[sb] = posted;
      Add(sb, kPostSmallBlind, posted, posted, false);
    }
    if (bb >= 0) {
      double ob = s.seat[bb].bet;
      double posted = (ob > kEps) ? (bb_overridden ? ob : (ob < bbl ? ob : bbl)) : bbl;
      seat_bet_[bb] = posted;
      Add(bb, kPostBigBlind, posted, posted, false);
    }
    cur_bet_ = 0;
    for (int i = 0; i < n_; i++) if (seat_bet_[i] > cur_bet_) cur_bet_ = seat_bet_[i];
    // Players who put everything in with the blind are all-in
    for (int i = 0; i < n_; i++) {
      if (in_hand_[i] && (seat_bet_[i] > kEps) &&
          (s.seat[i].balance <= kEps) && (i == sb || i == bb)) allin_[i] = true;
    }
    to_act_ = (bb >= 0) ? NextCanAct(bb) : NextCanAct(s.dealer);
    state_ = kRunning;
    return true;
  }

  // ---- during the hand -----------------------------------------------------

  void Update(const Snapshot &s) {
    // Board and hole cards: remember what we see
    for (int i = 0; i < 5; i++)
      if (h_.board[i].empty() && !s.board[i].empty()) {
        h_.board[i] = s.board[i];
        if (i + 1 > h_.board_count) h_.board_count = i + 1;
      }
    for (int i = 0; i < n_; i++) {
      if (!in_hand_[i]) continue;
      if (!s.seat[i].cards[0].empty() && !s.seat[i].cards[1].empty()) {
        h_.hole[i][0] = s.seat[i].cards[0];
        h_.hole[i][1] = s.seat[i].cards[1];
      }
    }

    // A player who folded must not come back
    for (int i = 0; i < n_; i++) {
      if (in_hand_[i] && folded_[i] && s.seat[i].active && s.betround == street_ &&
          !reappeared_[i]) {
        reappeared_[i] = true;
        Problem("seat " + Num(i + 1) + " is active again after folding");
      }
    }
    if (!over_) {
      // New betting round(s)?
      int target = s.betround > 4 ? 4 : s.betround;
      while (street_ < target && !over_) {
        if (!CloseStreet(s)) break;
      }
      if (!over_) ProcessChanges(s);
    }

    // Keep track of the chips behind (the last snapshot defines the end stacks).
    // Only 'balance' is used: a bet that is still displayed in front of a player
    // is not his money any more, and a payout shows up as a higher balance.
    for (int i = 0; i < n_; i++)
      if (in_hand_[i] && s.seat[i].seated)
        last_stack_[i] = s.seat[i].balance;

    // Pot seen at the end of the previous round is recorded in CloseStreet()
    CheckOver(s);
  }

  void CheckOver(const Snapshot &s) {
    if (over_) return;
    // Folds that have been seen so far
    if (Alive() <= 1) over_ = true;
    (void) s;
  }

  // Changes since the last snapshot of this betting round
  void ProcessChanges(const Snapshot &s) {
    // Collect the seats that did something, in table order starting at to_act_
    std::vector<int> actors;
    for (int k = 0; k < n_; k++) {
      int i = (to_act_ + k) % n_;
      if (!in_hand_[i] || folded_[i] || allin_[i]) continue;
      const SeatSnapshot &p = s.seat[i];
      bool fold = !p.active;
      bool more = false;
      if (!fold && p.bet > seat_bet_[i] + kEps) {
        HealStart(i, p);
        more = (p.bet > seat_bet_[i] + kEps) && BetSupported(i, p);
      }
      if (fold || more) actors.push_back(i);
    }
    for (size_t a = 0; a < actors.size(); a++) {
      int i = actors[a];
      const SeatSnapshot &p = s.seat[i];
      if (!p.active) {
        // Fold. Folding out of turn is possible, so it does not imply
        // anything for the other seats.
        Add(i, kFold, 0, seat_bet_[i], false);
        folded_[i] = true;
        acted_[i] = true;
        if (Alive() <= 1) { over_ = true; return; }
        if (i == to_act_) to_act_ = NextCanAct(i);
        continue;
      }
      // Bet / call / raise: everybody between the pointer and this seat
      // must have acted before, without changing their bet => checks
      for (int k = 0; k < n_; k++) {
        int j = (to_act_ + k) % n_;
        if (j == i) break;
        if (!CanAct(j) || acted_[j]) continue;
        if (seat_bet_[j] + kEps >= cur_bet_) {
          Add(j, kCheck, 0, seat_bet_[j], false);
        } else {
          Problem("action order unclear in front of seat " + Num(i + 1));
        }
        acted_[j] = true;
      }
      double newbet = p.bet;
      double delta = newbet - seat_bet_[i];
      bool allin = p.balance <= kEps;
      if (newbet > cur_bet_ + kEps) {
        if (cur_bet_ < kEps) Add(i, kBet, delta, newbet, allin, newbet);
        else                 Add(i, kRaise, delta, newbet, allin, newbet - cur_bet_);
        cur_bet_ = newbet;
        // after a raise everybody else has to act again
        for (int j = 0; j < n_; j++) if (j != i) acted_[j] = false;
      } else {
        Add(i, kCall, delta, newbet, allin);
      }
      acted_[i] = true;
      seat_bet_[i] = newbet;
      if (allin) allin_[i] = true;
      to_act_ = NextCanAct(i);
    }
  }

  // At the end of a round the missing folds, calls and checks are inferred.
  // Returns the number of players still in the hand.
  int InferClosingActions(const Snapshot &s) {
    // Folds first: they decide whether the round ended by everybody folding
    std::vector<int> order;
    for (int k = 0; k < n_; k++) order.push_back((to_act_ + k) % n_);
    for (size_t o = 0; o < order.size(); o++) {
      int i = order[o];
      if (!in_hand_[i] || folded_[i]) continue;
      if (!s.seat[i].active) {
        Add(i, kFold, 0, seat_bet_[i], false);
        folded_[i] = true;
      }
    }
    int alive = Alive();
    if (alive <= 1) { over_ = true; return alive; }
    for (size_t o = 0; o < order.size(); o++) {
      int i = order[o];
      if (!in_hand_[i] || folded_[i] || allin_[i]) continue;
      if (seat_bet_[i] + kEps < cur_bet_) {
        double amount = cur_bet_ - seat_bet_[i];
        bool allin = s.seat[i].balance <= kEps;
        Add(i, kCall, amount, cur_bet_, allin);
        seat_bet_[i] = cur_bet_;
        if (allin) allin_[i] = true;
        acted_[i] = true;
      } else if (!acted_[i]) {
        Add(i, kCheck, 0, seat_bet_[i], false);
        acted_[i] = true;
      }
    }
    return alive;
  }

  // Move to the next betting round. Returns false if that is not possible.
  bool CloseStreet(const Snapshot &s) {
    int alive = InferClosingActions(s);
    if (alive <= 1) return false;
    double total = 0;
    for (int i = 0; i < n_; i++) {
      if (!in_hand_[i]) continue;
      contrib_[i] += seat_bet_[i];
      seat_bet_[i] = 0;
      acted_[i] = false;
      ignored_bet_[i] = 0;
      total += contrib_[i];
    }
    if (street_ <= 4) { h_.pot_seen[street_] = total; h_.pot_scraped[street_] = s.pot; }
    cur_bet_ = 0;
    street_++;
    if (street_ > 4) street_ = 4;
    // Postflop the first player left of the button acts first
    to_act_ = NextCanAct(h_.dealer);
    // More than one street skipped without everybody being all-in?
    return true;
  }

  // ---- end of the hand -----------------------------------------------------

  void FinishHand(const Options &options) {
    const Snapshot &s = last_;
    std::vector<int> ambiguous;
    unsigned chosen = 0;
    if (!over_ && Alive() >= 2) {
      // The last betting round is open: a player who has not matched the highest
      // bet either called (=> showdown) or folded without us noticing (the
      // "active" flag can lag behind). The money tells which one is right:
      // try the possibilities, fewest folds first, and keep the one that
      // leaves the fewest inconsistencies.
      bool opponent_cards_seen = false;
      for (int i = 0; i < n_; i++) {
        if (!in_hand_[i] || folded_[i] || i == h_.hero) continue;
        if (!h_.hole[i][0].empty() && !h_.hole[i][1].empty()) opponent_cards_seen = true;
      }
      for (int i = 0; i < n_; i++)
        if (in_hand_[i] && !folded_[i] && !allin_[i] && seat_bet_[i] + kEps < cur_bet_)
          ambiguous.push_back(i);
      if (!ambiguous.empty() && !opponent_cards_seen && ambiguous.size() <= 8) {
        size_t best_problems = 0; bool have_best = false;
        const unsigned total = 1u << ambiguous.size();
        for (size_t folds = 0; folds <= ambiguous.size(); folds++) {
          for (unsigned m = 0; m < total; m++) {
            if (PopCount(m) != folds) continue;
            Reconstructor trial = *this;
            trial.Conclude(s, ambiguous, m, options);
            size_t n = trial.h_.problems.size();
            if (!have_best || n < best_problems) { have_best = true; best_problems = n; chosen = m; }
          }
        }
      }
    }
    Conclude(s, ambiguous, chosen, options);
  }

  static size_t PopCount(unsigned v) { size_t c = 0; while (v) { c += v & 1u; v >>= 1; } return c; }

  // Completes the hand under the assumption that the players of 'ambiguous'
  // that are marked in 'fold_mask' folded and the others called.
  void Conclude(const Snapshot &s, const std::vector<int> &ambiguous, unsigned fold_mask,
                const Options &options) {
    for (size_t k = 0; k < ambiguous.size(); k++) {
      if (fold_mask & (1u << k)) {
        int i = ambiguous[k];
        Add(i, kFold, 0, seat_bet_[i], false);
        folded_[i] = true;
      }
    }
    int alive = Alive();
    if (!over_ && alive >= 2) {
      // Showdown: the last round was closed by the unseen calls and checks
      InferClosingActions(s);
      alive = Alive();
    }
    // Remaining bets of the last round count as contributions
    for (int i = 0; i < n_; i++) {
      if (!in_hand_[i]) continue;
      contrib_[i] += seat_bet_[i];
      seat_bet_[i] = 0;
    }
    h_.last_street = street_;
    h_.showdown = (alive >= 2);
    for (int i = 0; i < n_; i++) {
      h_.folded[i] = folded_[i];
      h_.contrib[i] = contrib_[i];
      h_.end_stack[i] = last_stack_[i];
    }
    ComputeMoney();
    Validate(options);
  }

  void ComputeMoney() {
    // Uncalled bet: the highest contribution that nobody matched
    int top = -1;
    double c_top = 0, c_second = 0;
    for (int i = 0; i < n_; i++) {
      if (!in_hand_[i]) continue;
      if (h_.contrib[i] > c_top) { c_second = c_top; c_top = h_.contrib[i]; top = i; }
      else if (h_.contrib[i] > c_second) { c_second = h_.contrib[i]; }
    }
    h_.uncalled = (top >= 0 && c_top - c_second > kEps) ? (c_top - c_second) : 0;
    h_.uncalled_seat = (h_.uncalled > 0) ? top : -1;

    double sum_contrib = 0, sum_collected = 0;
    for (int i = 0; i < n_; i++) {
      h_.collected[i] = 0;
      if (!in_hand_[i]) continue;
      sum_contrib += h_.contrib[i];
      // balance_end = start - contrib + (returned uncalled bet) + collected
      double c = h_.end_stack[i] - h_.start_stack[i] + h_.contrib[i]
                 - ((i == h_.uncalled_seat) ? h_.uncalled : 0);
      if (c > kEps) h_.collected[i] = c;
      sum_collected += h_.collected[i];
    }
    h_.total_pot = sum_contrib - h_.uncalled;
    h_.rake = h_.total_pot - sum_collected;
  }

  void Validate(const Options &options) {
    int dealt = 0;
    for (int i = 0; i < n_; i++) if (in_hand_[i]) dealt++;
    if (dealt < 2) Problem("fewer than 2 players");
    if (h_.bb_seat < 0) Problem("big blind not found");
    if (h_.hero >= 0 && h_.hero < n_ && in_hand_[h_.hero] &&
        (h_.hole[h_.hero][0].empty() || h_.hole[h_.hero][1].empty()))
      Problem("hero cards unknown");

    // Board must match the last round
    static const int expected_board[5] = { 0, 0, 3, 4, 5 };
    int expected = expected_board[h_.last_street];
    if (h_.showdown) expected = 5;
    if (h_.board_count < expected) Problem("board incomplete (" + Num(h_.board_count) + " cards)");

    // Side pots are not supported (yet): all players still in the hand
    // must have put in the same amount (after the uncalled part is returned)
    double ref = -1;
    for (int i = 0; i < n_; i++) {
      if (!in_hand_[i] || folded_[i]) continue;
      double c = h_.contrib[i] - ((i == h_.uncalled_seat) ? h_.uncalled : 0);
      if (ref < 0) ref = c;
      else if (fabs(c - ref) > kEps) { Problem("side pot (not supported yet)"); break; }
    }

    double sum_collected = 0;
    for (int i = 0; i < n_; i++) sum_collected += h_.collected[i];
    if (sum_collected < kEps) Problem("no winner found (payout not seen before the next hand)");
    // In a single pot everybody who wins gets the same share (a chop);
    // anything else means that our picture of the betting is wrong.
    {
      double lo = -1, hi = -1;
      for (int i = 0; i < n_; i++) {
        if (h_.collected[i] <= kEps) continue;
        if (lo < 0 || h_.collected[i] < lo) lo = h_.collected[i];
        if (hi < 0 || h_.collected[i] > hi) hi = h_.collected[i];
      }
      double odd_chip = h_.sblind > 0.011 ? h_.sblind : 0.011;
      if (lo >= 0 && hi - lo > odd_chip)
        Problem("payout does not fit a single pot (" + Money(lo) + " and " + Money(hi) + " collected)");
    }
    if (h_.rake < -0.011) Problem("collected more than the pot");
    if (h_.total_pot > kEps && h_.rake > options.max_rake * h_.total_pot + kEps)
      Problem("rake is " + Pct(h_.rake / h_.total_pot) + " of the pot");

    if (options.check_pot) {
      for (int st = 1; st <= 4; st++) {
        double ours = h_.pot_seen[st], scraped = h_.pot_scraped[st];
        if (ours > kEps && scraped > kEps &&
            fabs(ours - scraped) > options.pot_tolerance * ours + kEps)
          Problem("pot after round " + Num(st) + " is " + Money(ours) + " but the table shows " + Money(scraped));
      }
    }
    // Every betting round must end with the same bet for everybody (inferred),
    // therefore only the structure of the action list is checked here
    for (size_t a = 0; a < h_.actions.size(); a++) {
      const Action &x = h_.actions[a];
      if (x.amount < -kEps) { Problem("negative amount"); break; }
    }
  }

  static std::string Num(int v) { char b[32]; snprintf(b, sizeof(b), "%d", v); return b; }
  static std::string Money(double v) { char b[32]; snprintf(b, sizeof(b), "%.2f", v); return b; }
  static std::string Pct(double v) { char b[32]; snprintf(b, sizeof(b), "%.0f%%", v * 100.0); return b; }
};

} // namespace hh

#endif // INC_HANDHISTORYCORE_H
