// Évaluation de main compatible OpenHoldem : HandVal (poker-eval) et pokerval / pcbits
// (CSymbolEnginePokerval::CalculatePokerval). Port littéral, validé par oracle sur le code original.
// Les cartes utilisent ici l'indexation poker-eval : couleur*13 + rang (Hearts=0, Diamonds=1, Clubs=2, Spades=3).
#pragma once
#include <cstdint>
#include "pokersim/cards.hpp"

namespace ps::oh {

constexpr int kNoCardOH = 0xfe;                 // CARD_NOCARD
constexpr int kPeHearts = 0, kPeDiamonds = 1, kPeClubs = 2, kPeSpades = 3;

// moteur (rang*4 + couleur c,d,h,s)  <->  poker-eval (couleur*13 + rang)
inline int toPe(Card c) { static const int s[4] = {kPeClubs, kPeDiamonds, kPeHearts, kPeSpades}; return s[suitOf(c)] * 13 + rankOf(c); }
inline Card fromPe(int pe) { static const int s[4] = {kPeClubs, kPeDiamonds, kPeHearts, kPeSpades}; return makeCard(pe % 13, s[pe / 13]); }   // la table est son propre inverse

// masque de cartes : bit (couleur*13 + rang)
inline uint64_t peBit(int pe) { return 1ULL << pe; }

// HandVal : type<<24 | top<<16 | second<<12 | third<<8 | fourth<<4 | fifth  (rangs 0..12)
uint32_t handValN(uint64_t cardMask, int nCards);
inline int hvType(uint32_t hv) { return static_cast<int>(hv >> 24); }
inline int hvCard(uint32_t hv, int i) { return static_cast<int>((hv >> (16 - 4 * i)) & 0xF); }   // i = 0 (top) .. 4

// pokerval (valeur signée 32 bits comme dans OpenHoldem) et pcbits
int calcPokerval(uint32_t hv, int n, int* pcbits, int card0, int card1, uint64_t allCards);

}  // namespace ps::oh
