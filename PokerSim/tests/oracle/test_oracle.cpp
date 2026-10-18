// Test différentiel : port C++ (pokerval.cpp) contre le code ORIGINAL d'OpenHoldem (oracle) sur des tirages aléatoires.
#include <cstdio>
#include <cstdlib>
#include "pokersim/pokerval.hpp"
#include "pokersim/rng.hpp"
#include "pokersim/evaluator.hpp"

extern "C" unsigned oracle_pokerval(const int* cards, int n, int card0, int card1, int* pcbits, unsigned* handval, int* ncards);

using namespace ps;
using namespace ps::oh;

int main(int argc, char** argv) {
    const long trials = argc > 1 ? std::atol(argv[1]) : 3000000;
    Rng rng(2024);
    long bad = 0, byN[8] = {}, byType[9] = {}, badEval = 0;
    for (long it = 0; it < trials; ++it) {
        int n = static_cast<int>(rng.below(8));              // 0..7 cartes
        if (it % 4 != 0) n = 2 + static_cast<int>(rng.below(6)); // surtout 2..7 (cas réels)
        int deck[52];
        for (int i = 0; i < 52; ++i) deck[i] = i;
        for (int i = 0; i < n; ++i) std::swap(deck[i], deck[i + static_cast<int>(rng.below(52 - i))]);
        int cards[8];
        uint64_t mask = 0;
        for (int i = 0; i < n; ++i) { cards[i] = deck[i]; mask |= peBit(cards[i]); }
        int mode = static_cast<int>(rng.below(4));
        int c0 = (n >= 1) ? cards[0] : kNoCardOH, c1 = (n >= 2) ? cards[1] : kNoCardOH;
        if (mode == 1) { c0 = c1 = kNoCardOH; }                         // pokervalplayer / pokervalcommon
        if (mode == 2) { c0 = deck[10]; c1 = deck[11]; }               // cartes du joueur hors de l'ensemble évalué
        unsigned ohv = 0; int ocb = 0, on = 0;
        unsigned opv = oracle_pokerval(cards, n, c0, c1, &ocb, &ohv, &on);
        uint32_t hv = handValN(mask, n);
        int pcb = 0;
        int pv = calcPokerval(hv, n, &pcb, c0, c1, mask);
        ++byN[n]; ++byType[hvType(hv)];
        if (hv != ohv || static_cast<unsigned>(pv) != opv || pcb != ocb) {
            if (++bad <= 8) {
                std::printf("ECART n=%d mode=%d cartes:", n, mode);
                for (int i = 0; i < n; ++i) std::printf(" %s", cardToString(fromPe(cards[i])).c_str());
                std::printf("\n   hv  oracle=%08x port=%08x | pokerval oracle=%08x port=%08x | pcbits oracle=%d port=%d\n", ohv, hv, opv, static_cast<unsigned>(pv), ocb, pcb);
            }
        }
        if (n >= 5) {   // recoupement avec l'évaluateur du moteur (catégorie)
            Card ec[7]; for (int i = 0; i < n; ++i) ec[i] = fromPe(cards[i]);
            if (categoryOf(evaluate(ec, n)) != hvType(hv)) ++badEval;
        }
    }
    std::printf("tirages=%ld  écarts oracle=%ld  écarts catégorie moteur=%ld\n", trials, bad, badEval);
    std::printf("répartition n : "); for (int i = 0; i < 8; ++i) std::printf("%d:%ld ", i, byN[i]);
    std::printf("\nrépartition type : "); for (int i = 0; i < 9; ++i) std::printf("%d:%ld ", i, byType[i]);
    std::printf("\n");
    return (bad || badEval) ? 1 : 0;
}
