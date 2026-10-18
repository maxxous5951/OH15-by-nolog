#include <cmath>
#include <cstdio>
#include <vector>
#include "pokersim/equity.hpp"
#include "pokersim/evaluator.hpp"
#include "pokersim/handrank.hpp"
#include "pokersim/prwin.hpp"

using namespace ps;
using namespace ps::oh;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static int pe(const char* s) { return toPe(cardFromString(s)); }
static uint64_t mask(std::initializer_list<const char*> l) { uint64_t m = 0; for (auto s : l) m |= peBit(pe(s)); return m; }

static PrwinInput base(std::initializer_list<const char*> hole, std::initializer_list<const char*> board, int nOpp, int iters) {
    PrwinInput in;
    in.plCards = mask(hole); in.comCards = mask(board);
    in.nCom = static_cast<int>(board.size()); in.nOpponents = nOpp; in.iterations = iters;
    in.preflop = board.size() == 0;
    return in;
}
// écart-type d'une proportion p estimée sur n tirages
static double sigma(double p, double n) { return std::sqrt(p * (1 - p) / n); }

static void testNhands() {
    NhandsResult a = computeNhands(mask({"As", "Ah"}), 0, 2, 0, 1);
    CHECK(a.total() == 1225);                       // C(50,2)
    CHECK(a.hi == 0 && a.ti == 1 && a.lo == 1224);  // seule égalité : les deux autres As
    CHECK(std::fabs(a.prwinnow - 1224.0 / 1225) < 1e-12);
    NhandsResult b = computeNhands(mask({"As", "Ah"}), 0, 2, 0, 3);
    CHECK(std::fabs(b.prwinnow - std::pow(1224.0 / 1225, 3)) < 1e-12);
    CHECK(std::fabs(b.prlosnow - (1 - std::pow(1225.0 / 1225, 3))) < 1e-12);
    // quinte flush royale sur le board : OpenHoldem compare pokerval en unsigned (INT_MIN) -> les quintes flush sont À ÉGALITÉ
    NhandsResult c = computeNhands(mask({"As", "Ks"}), mask({"Qs", "Js", "Ts"}), 2, 3, 1);
    CHECK(c.total() == 1081);                       // C(47,2)
    CHECK(c.hi == 0 && c.ti == 1 && c.lo == 1080);  // 9s8s fait une quinte flush : « égale » à la quinte flush royale
}

static void testHandrank() {
    HandrankValues v = computeHandrank(pe("As"), pe("Ah"), 1);
    CHECK(v.r169 == 1 && v.r2652 == 12 && v.r1326 == 6 && v.r1000 == 1000 * 12 / 2652);
    CHECK(std::fabs(v.rp - 2652.0 / 2) < 1e-12);
    HandrankValues w = computeHandrank(pe("Ks"), pe("As"), 1);   // ordre des cartes indifférent
    HandrankValues x = computeHandrank(pe("As"), pe("Ks"), 1);
    CHECK(w.r169 == x.r169 && w.r169 > 0);
    HandrankValues s = computeHandrank(pe("As"), pe("Ks"), 1), o = computeHandrank(pe("As"), pe("Kh"), 1);
    CHECK(s.r169 < o.r169);                         // AKs mieux classée que AKo
    HandrankValues big = computeHandrank(pe("As"), pe("Ah"), 12);
    CHECK(big.r169 == 0);                           // > 10 adversaires : hors table (comportement indéfini côté OpenHoldem)
    CHECK(prwinPairRank(false, 12, 12) == 1 && prwinPairRank(true, 12, 11) == 3);   // AA = 1, AKs = 3 dans la table prwin
}

static void testStandardPrwin() {
    Rng rng(99);
    const int N = 200000;
    PrwinResult r = computePrwin(base({"As", "Ah"}, {}, 1, N), rng);
    double eq = r.win + r.tie / 2;
    CHECK(std::fabs(eq - 0.852) < 4 * sigma(0.852, N) + 0.002);      // AA contre une main aléatoire
    CHECK(std::fabs(r.win + r.tie + r.lose - (N - 1.0) / N) < 1e-9);  // biais (required-1)/required d'OpenHoldem
    // accord avec le Monte-Carlo indépendant du moteur
    Card h[2] = {cardFromString("Ks"), cardFromString("Qs")};
    Card b[3] = {cardFromString("Js"), cardFromString("Ts"), cardFromString("2d")};
    Equity ex = equityExactHeadsUp(h, b, 3);
    PrwinResult p = computePrwin(base({"Ks", "Qs"}, {"Js", "Ts", "2d"}, 1, N), rng);
    CHECK(std::fabs(p.win - ex.win) < 5 * sigma(ex.win, N));
    CHECK(std::fabs(p.tie - ex.tie) < 5 * sigma(ex.tie, N) + 0.001);
    // plusieurs adversaires : la défaite est testée contre chacun
    Equity e3 = equityVsRandom(h, b, 3, 3, 300000, rng);
    PrwinResult p3 = computePrwin(base({"Ks", "Qs"}, {"Js", "Ts", "2d"}, 3, N), rng);
    CHECK(std::fabs(p3.win - e3.win) < 5 * sigma(e3.win, N) + 0.003);
    CHECK(std::fabs(p3.lose - e3.lose) < 5 * sigma(e3.lose, N) + 0.003);
    // cas dégénérés
    CHECK(computePrwin(base({"As", "Ah"}, {}, 1, 1), rng).win == 0);        // iterations < 2 : rien calculé
    CHECK(computePrwin(base({"As", "Ah"}, {}, 0, 1000), rng).win == 0);     // 0 adversaire
}

static void testWeighted() {
    Rng rng(5);
    // fonction de filtrage : mains dans [mustplay..willplay] toujours acceptées, >= wontplay jamais
    CHECK(isHandInWeightedRange(pe("As"), pe("Ah"), 40, 60, 0, 0, rng));       // AA rang 1 : zone des 100 %
    int low = 0;
    for (int i = 0; i < 200; ++i) low += isHandInWeightedRange(pe("7s"), pe("2h"), 40, 60, 0, 0, rng);   // 72o rang ~169 >= wontplay
    CHECK(low == 0);
    // restreindre les adversaires aux mains fortes baisse l'équité du héros
    const int N = 60000;
    PrwinResult any = computePrwin(base({"Qs", "Jh"}, {}, 1, N), rng);
    PrwinInput w = base({"Qs", "Jh"}, {}, 1, N);
    w.willplay = 20; w.wontplay = 22;
    PrwinResult tight = computePrwin(w, rng);
    CHECK((tight.win + tight.tie / 2) < (any.win + any.tie / 2) - 0.08);
}

static void testEnhanced() {
    Rng rng(11);
    // siège 1 : uniquement AA et KK (tous les combos), poids égaux ; héros 7h2d, joueur 1 actif
    Prw1326 prw;
    prw.useme = 1326; prw.preflop = 1326;      // préflop : OpenHoldem exige aussi prw1326_usepreflop
    Prw1326Chair& c = prw.chair[1];
    int n = 0;
    for (int rank : {12, 11})
        for (int s1 = 0; s1 < 4; ++s1) for (int s2 = s1 + 1; s2 < 4; ++s2) {
            c.rankhi[n] = s1 * 13 + rank; c.ranklo[n] = s2 * 13 + rank; c.weight[n] = 100; ++n;
        }
    c.limit = n;
    PrwinInput in = base({"7h", "2d"}, {}, 1, 120000);
    in.userchair = 0; in.playersPlayingBits = 0b11; in.prw = &prw;
    PrwinResult r = computePrwin(in, rng);
    // vérification indépendante par énumération : 7h2d contre chaque combo AA/KK, board complété au hasard
    double eq = 0; long cnt = 0;
    Card hole[2] = {cardFromString("7h"), cardFromString("2d")};
    for (int k = 0; k < n; ++k) {
        Card opp[2] = {fromPe(c.rankhi[k]), fromPe(c.ranklo[k])};
        // équité du héros contre cette main précise : on la calcule par tirage de board
        Deck d; d.remove(hole[0]); d.remove(hole[1]); d.remove(opp[0]); d.remove(opp[1]);
        Rng r2(1000 + k);
        long w = 0, t = 0, tot = 20000;
        for (long it = 0; it < tot; ++it) {
            Deck dk = d; Card board[5]; for (int i = 0; i < 5; ++i) board[i] = dk.draw(r2);
            Card a[7] = {hole[0], hole[1], board[0], board[1], board[2], board[3], board[4]};
            Card o[7] = {opp[0], opp[1], board[0], board[1], board[2], board[3], board[4]};
            uint32_t x = evaluate(a, 7), y = evaluate(o, 7);
            if (x > y) ++w; else if (x == y) ++t;
        }
        eq += (w + t / 2.0) / tot; ++cnt;
    }
    eq /= cnt;
    CHECK(std::fabs((r.win + r.tie / 2) - eq) < 0.01);
    CHECK(eq > 0.10 && eq < 0.25);                   // 7h2d contre AA/KK : ~ 12-15 %
    // plage impossible : le héros détient déjà toutes les cartes du siège -> échec (-1)
    Prw1326 dead;
    dead.useme = 1326; dead.preflop = 1326;
    dead.chair[1].limit = 1; dead.chair[1].rankhi[0] = pe("As"); dead.chair[1].ranklo[0] = pe("Ah"); dead.chair[1].weight[0] = 100;
    PrwinInput bad = base({"As", "Ah"}, {}, 1, 1000);
    bad.userchair = 0; bad.playersPlayingBits = 0b11; bad.prw = &dead;
    PrwinResult f = computePrwin(bad, rng);
    CHECK(f.win == -1.0 && f.tie == -1.0 && f.lose == -1.0);
    dead.preflop = 0;                                // sans usepreflop, préflop : tirage standard, pas d'échec
    CHECK(computePrwin(bad, rng).win > 0);
}

int main() {
    testNhands();
    testHandrank();
    testStandardPrwin();
    testWeighted();
    testEnhanced();
    if (failures) { std::printf("%d échec(s)\n", failures); return 1; }
    std::puts("prwin : OK");
    return 0;
}
