//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: hand equity (exact enumeration or Monte Carlo) and pot odds
//
//******************************************************************************

#ifndef INC_EXPLORER_EQUITY_H
#define INC_EXPLORER_EQUITY_H

#include <string>
#include <vector>

namespace explorer {

struct PlayerCards {
  bool        known;
  std::string card1, card2;      // "As", "Td", ...
  PlayerCards() : known(false) {}
  PlayerCards(const std::string &a, const std::string &b) : known(true), card1(a), card2(b) {}
};

// Equity (share of the pot, ties split) of every player. Players with unknown cards
// get random cards. Exact if all cards are known and the number of boards is small,
// else Monte Carlo with `iterations` samples (deterministic).
// False if a card is invalid or used twice.
bool ComputeEquity(const std::vector<PlayerCards> &players, const std::vector<std::string> &board,
                   int iterations, std::vector<double> *equity, bool *exact = NULL);

// Strength of the best 5 out of 5..7 cards, higher = better; -1 for invalid cards
int EvaluateCards(const std::vector<std::string> &cards);

// Share of the final pot that a call must win: to_call / (pot + to_call)
double PotOdds(double pot, double to_call);

}  // namespace explorer

#endif  // INC_EXPLORER_EQUITY_H
