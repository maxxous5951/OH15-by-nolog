// Équité (prwin / prtie / prlos). Phase 1 : adversaires aléatoires ; énumération exacte quand c'est peu coûteux.
#pragma once
#include "pokersim/cards.hpp"
#include "pokersim/rng.hpp"

namespace ps {

struct Equity {
    double win = 0, tie = 0, lose = 0;   // fréquences (somme = 1)
    long samples = 0;
    bool exact = false;
};

// Héros (2 cartes) + board (0..5 cartes) contre nOpp adversaires à mains aléatoires.
Equity equityVsRandom(const Card hole[2], const Card* board, int nBoard, int nOpp, int iterations, Rng& rng);

// Énumération exacte (1 adversaire, board complet à terminer) : renvoie samples=0 si trop coûteux.
Equity equityExactHeadsUp(const Card hole[2], const Card* board, int nBoard);

}  // namespace ps
