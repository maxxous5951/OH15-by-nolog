// prwin / prtie / prlos d'OpenHoldem (CIteratorThread) et nhands* / prwinnow / prlosnow (CSymbolEnginePrwin).
// Port de l'algorithme original, calculé de façon synchrone (OpenHoldem le calcule dans un thread).
#pragma once
#include <cstdint>
#include "pokersim/pokerval.hpp"
#include "pokersim/rng.hpp"

namespace ps::oh {

constexpr int kMaxOpponents = 22;

// Plages pondérées par siège (« prw1326 ») : ranklo/rankhi = cartes poker-eval, weight = poids
struct Prw1326Chair {
    int limit = 0, level = 100, ignore = 0;
    int rankhi[1326] = {0}, ranklo[1326] = {0}, weight[1326] = {0};
};
struct Prw1326 { int useme = 0, preflop = 0, usecallback = 0; Prw1326Chair chair[10]; };

struct PrwinInput {
    uint64_t plCards = 0, comCards = 0;      // cartes connues du joueur et du board (masque poker-eval)
    int nCom = 0;                            // nombre de cartes communes connues
    int iterations = 1000;                   // f$prwin_number_of_iterations
    int nOpponents = 1;                      // f$prwin_number_of_opponents (déjà borné)
    int willplay = 0, wontplay = 0, topclip = 0, mustplay = 0;   // pondération standard (willplay = 0 : désactivée)
    int userchair = 0, playersPlayingBits = 0;
    bool preflop = false;
    const Prw1326* prw = nullptr;            // plages enhanced ; ignorées si useme != 1326
};
struct PrwinResult { double win = 0, tie = 0, lose = 0; };   // -1,-1,-1 si les plages sont impossibles (kUndefined)

PrwinResult computePrwin(const PrwinInput& in, Rng& rng);

struct NhandsResult {
    int hi = 0, lo = 0, ti = 0;              // mains adverses meilleures / moins bonnes / égales
    double prwinnow = 0, prlosnow = 0;
    int total() const { return hi + lo + ti; }
};
// Énumération exacte des mains adverses possibles (CalculateNhands)
NhandsResult computeNhands(uint64_t plCards, uint64_t comCards, int nPl, int nCom, int nOppForPrwin);

// Pour les tests : pokerval non signé (référence) et clé de comparaison rapide équivalente
uint32_t pokervalUnsigned(uint64_t mask, int n);
uint32_t fastCompareKey(uint64_t mask, int n);

// Filtrage pondéré des mains adverses (PrWinHandranges.cpp), r = rand() de 0 à 32767
bool isHandInWeightedRange(int card1, int card2, int willplay, int wontplay, int topclip, int mustplay, Rng& rng);

}  // namespace ps::oh
