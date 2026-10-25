// Symboles « vs$ » d'OpenHoldem (CSymbolEngineVersus) : issue de ma main contre CHAQUE main adverse possible, par énumération EXACTE
// des boards restants. Préflop : table exacte de OpenHoldem (versus.bin, 1 712 304 boards par confrontation) compactée par symétrie.
// Cartes : indexation poker-eval (couleur*13 + rang).
#pragma once
#include <cstdint>

namespace ps::oh {

constexpr uint32_t kVersusBoards = 1712304;    // C(48,5)

// données générées (src/versus_data.cpp) : triplets (clé canonique, victoires, défaites), triés par clé
extern const unsigned kVersusEntries;
extern const unsigned kVersusData[];

// Préflop : nombre de boards (sur 1 712 304) que (h0,h1) gagne / perd contre (o0,o1) ; les égalités = 1712304 - win - loss.
// false si les 4 cartes ne sont pas toutes distinctes.
bool versusPreflopLookup(int h0, int h1, int o0, int o1, uint32_t& win, uint32_t& loss);

struct VersusGroup {                          // agrégat sur un groupe de mains adverses
    unsigned nhands = 0;
    double win = 0, tie = 0, los = 0;         // sommes des boards gagnés / égalisés / perdus
    double prwin() const { double t = win + tie + los; return nhands > 0 && t > 0 ? win / t : 0.0; }
    double prtie() const { double t = win + tie + los; return nhands > 0 && t > 0 ? tie / t : 0.0; }
    double prlos() const { double t = win + tie + los; return nhands > 0 && t > 0 ? los / t : 0.0; }
};

struct VersusResult {
    bool valid = false;                       // false si le héros n'a pas 2 cartes connues
    uint32_t win[52][52] = {}, tie[52][52] = {}, loss[52][52] = {};    // par main adverse (i<j) ; 0 si impossible
    VersusGroup all, hi, ti, lo;              // toutes les mains / meilleures (win<loss) / égales / moins bonnes, sur les runouts
    VersusGroup hiNow, tiNow, loNow;          // idem selon la valeur de main actuelle (postflop seulement)
    double prwin = 0, prtie = 0, prlos = 0;   // vs$prwin (déjà élevé à la puissance du nombre d'adversaires), vs$prtie, vs$prlos
};

// board : masque des cartes communes (bit = couleur*13 + rang), nBoard = 0, 3, 4 ou 5 ; nOppPlaying = nopponentsplaying
void computeVersus(int h0, int h1, uint64_t board, int nBoard, int nOppPlaying, VersusResult& out);

}  // namespace ps::oh
