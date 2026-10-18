// Classements de mains OpenHoldem (handrank169/2652/1326/1000/p) et table de pondération de prwin.
// Données générées depuis les sources d'OpenHoldem par tools/gen_handrank_data.py.
#pragma once
namespace ps::oh {

extern const int kHandrank2652[10][169];             // [nombre d'adversaires - 1][rang 169 - 1]
extern const char* const kHandrank169[10][169];      // "AKs", "AKo", "AAo"
extern const char* const kPrwinHandrank169[169];     // "AA ", "AKs" ... (pondération de prwin)

struct HandrankValues { int r169 = 0, r2652 = 0, r1326 = 0, r1000 = 0; double rp = 0; };
// c0/c1 : cartes poker-eval (couleur*13 + rang) ; nOpp = f$prwin_number_of_opponents (borné)
HandrankValues computeHandrank(int c0, int c1, int nOpp);

// rang de pondération (1..169) pour IsHandInWeightedRange ; 0 si inconnu
int prwinPairRank(bool suited, int rank1, int rank2);

}  // namespace ps::oh
