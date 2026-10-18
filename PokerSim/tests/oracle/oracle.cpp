// Oracle : CalculatePokerval ORIGINAL d'OpenHoldem + poker-eval, exposé en C pour les tests différentiels.
extern "C" {
#include "poker_defs.h"
#include "inlines/eval.h"
}
const unsigned int k_pokerval_hicard = 0x00000001, k_pokerval_onepair = 0x01000000, k_pokerval_twopair = 0x02000000,
    k_pokerval_threeofakind = 0x04000000, k_pokerval_straight = 0x08000000, k_pokerval_flush = 0x10000000,
    k_pokerval_fullhouse = 0x20000000, k_pokerval_fourofakind = 0x40000000, k_pokerval_straightflush = 0x80000000;
const int kNumberOfCommunityCards = 5;
static CardMask _spadesCards, _heartsCards, _diamondsCards, _clubsCards;
static void prepareSuitMasks() {
    CardMask_RESET(_heartsCards); CardMask_RESET(_diamondsCards); CardMask_RESET(_spadesCards); CardMask_RESET(_clubsCards);
    for (int rank = Rank_2; rank <= Rank_ACE; ++rank) {
        CardMask_SET(_heartsCards, StdDeck_MAKE_CARD(rank, Suit_HEARTS));
        CardMask_SET(_diamondsCards, StdDeck_MAKE_CARD(rank, Suit_DIAMONDS));
        CardMask_SET(_spadesCards, StdDeck_MAKE_CARD(rank, Suit_SPADES));
        CardMask_SET(_clubsCards, StdDeck_MAKE_CARD(rank, Suit_CLUBS));
    }
}
#include "oracle_gen.inc"

// cards : indices poker-eval (couleur*13 + rang) ; card0/card1 : cartes du joueur (ou 0xfe) ; renvoie pokerval
extern "C" unsigned oracle_pokerval(const int* cards, int n, int card0, int card1, int* pcbits, unsigned* handval, int* ncards) {
    static bool init = false;
    if (!init) { prepareSuitMasks(); init = true; }
    CardMask m; CardMask_RESET(m);
    for (int i = 0; i < n; ++i) CardMask_SET(m, cards[i]);
    int count = StdDeck_numCards(m);
    HandVal hv = Hand_EVAL_N(m, count);
    *handval = hv; *ncards = count;
    int pcb = 0;
    int pv = oracle_calc(hv, count, &pcb, card0, card1, m);
    *pcbits = pcb;
    return static_cast<unsigned>(pv);
}
