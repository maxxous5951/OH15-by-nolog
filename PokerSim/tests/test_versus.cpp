#include <cmath>
#include <cstdio>
#include "pokersim/equity.hpp"
#include "pokersim/pokerval.hpp"
#include "pokersim/ohsymbols.hpp"
#include "pokersim/versus.hpp"

using namespace ps;
using namespace ps::oh;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static int pe(const char* s) { return toPe(cardFromString(s)); }

// Référence par force brute : tous les boards de 5 cartes parmi les 48 restantes
static void brute(int h0, int h1, int o0, int o1, uint32_t& win, uint32_t& loss, uint32_t& tie) {
    int rest[48], n = 0;
    for (int c = 0; c < 52; ++c) if (c != h0 && c != h1 && c != o0 && c != o1) rest[n++] = c;
    const uint64_t hero = peBit(h0) | peBit(h1), opp = peBit(o0) | peBit(o1);
    win = loss = tie = 0;
    for (int a = 0; a < 44; ++a) for (int b = a + 1; b < 45; ++b) for (int c = b + 1; c < 46; ++c) {
        const uint64_t b3 = peBit(rest[a]) | peBit(rest[b]) | peBit(rest[c]);
        for (int d = c + 1; d < 47; ++d) {
            const uint64_t b4 = b3 | peBit(rest[d]);
            for (int e = d + 1; e < 48; ++e) {
                const uint64_t bd = b4 | peBit(rest[e]);
                const uint32_t x = handValN(hero | bd, 7), y = handValN(opp | bd, 7);
                if (x > y) ++win; else if (x < y) ++loss; else ++tie;
            }
        }
    }
}

static void testTableAgainstBruteForce() {
    struct M { const char *h0, *h1, *o0, *o1; };
    const M fixed[] = {{"As", "Ah", "Ks", "Kh"}, {"As", "Ah", "Kc", "Kd"}, {"As", "Ks", "Qs", "Js"}, {"Ah", "Kh", "Qh", "Jh"}, {"As", "Kd", "Ah", "Qc"},
                       {"7c", "2d", "As", "Ah"}, {"9h", "9d", "Th", "Jh"}, {"Ts", "9s", "8c", "8d"}, {"2c", "2d", "3c", "3d"}, {"Ac", "Kc", "Ad", "Kd"}};
    int checked = 0, bad = 0;
    for (const M& m : fixed) {
        uint32_t w, l, t, bw, bl, bt;
        CHECK(versusPreflopLookup(pe(m.h0), pe(m.h1), pe(m.o0), pe(m.o1), w, l));
        brute(pe(m.h0), pe(m.h1), pe(m.o0), pe(m.o1), bw, bl, bt);
        t = kVersusBoards - w - l;
        if (w != bw || l != bl || t != bt) { ++bad; std::printf("  écart %s%s vs %s%s : table %u/%u/%u, brute %u/%u/%u\n", m.h0, m.h1, m.o0, m.o1, w, t, l, bw, bt, bl); }
        ++checked;
    }
    Rng rng(31);
    for (int it = 0; it < 18; ++it) {            // matchups aléatoires
        int c[4]; bool dup;
        do { dup = false; for (int i = 0; i < 4; ++i) c[i] = static_cast<int>(rng.below(52)); for (int i = 0; i < 4; ++i) for (int j = i + 1; j < 4; ++j) dup |= c[i] == c[j]; } while (dup);
        uint32_t w, l, bw, bl, bt;
        CHECK(versusPreflopLookup(c[0], c[1], c[2], c[3], w, l));
        brute(c[0], c[1], c[2], c[3], bw, bl, bt);
        if (w != bw || l != bl || kVersusBoards - w - l != bt) { ++bad; std::printf("  écart aléatoire %d %d %d %d\n", c[0], c[1], c[2], c[3]); }
        ++checked;
    }
    std::printf("  table contre force brute : %d confrontations, %d écart(s)\n", checked, bad);
    CHECK(bad == 0);
    // cartes en double : refus
    uint32_t w, l;
    CHECK(!versusPreflopLookup(0, 0, 1, 2, w, l) && !versusPreflopLookup(0, 1, 1, 2, w, l) && !versusPreflopLookup(0, 1, 2, 60, w, l));
    // symétrie héros/adversaire
    uint32_t w1, l1, w2, l2;
    versusPreflopLookup(pe("As"), pe("Kd"), pe("7h"), pe("7c"), w1, l1);
    versusPreflopLookup(pe("7h"), pe("7c"), pe("As"), pe("Kd"), w2, l2);
    CHECK(w1 == l2 && l1 == w2);
    // invariance par permutation des couleurs
    versusPreflopLookup(pe("Ad"), pe("Ks"), pe("7c"), pe("7h"), w2, l2);
    CHECK(w1 == w2 && l1 == l2);
}

static void testPreflopAggregates() {
    VersusResult R;
    computeVersus(pe("As"), pe("Ah"), 0, 0, 1, R);
    CHECK(R.valid && R.all.nhands == 1225 && R.hi.nhands + R.ti.nhands + R.lo.nhands == 1225);
    CHECK(std::fabs(R.all.win + R.all.tie + R.all.los - 1225.0 * kVersusBoards) < 1);
    const double eq = R.prwin + R.prtie / 2;
    CHECK(std::fabs(eq - 0.8520) < 0.0005);                       // AA contre une main aléatoire
    CHECK(R.hi.nhands == 0 && R.ti.nhands == 1 && R.lo.nhands == 1224);   // seules les deux autres As égalisent
    CHECK(R.hiNow.nhands + R.tiNow.nhands + R.loNow.nhands == 0); // « now » n'existe pas préflop
    VersusResult R3;
    computeVersus(pe("As"), pe("Ah"), 0, 0, 3, R3);
    CHECK(std::fabs(R3.prwin - std::pow(R.prwin, 3)) < 1e-12 && std::fabs(R3.prtie - R.prtie) < 1e-15);   // seul prwin est élevé à la puissance
    Rng rng(3);
    Card h[2] = {cardFromString("As"), cardFromString("Ah")};
    Equity mc = equityVsRandom(h, nullptr, 0, 1, 300000, rng);
    CHECK(std::fabs(mc.win - R.prwin) < 0.004);                   // recoupement Monte-Carlo
    // 72o contre AA : très défavorable ; classé « hi » pour toutes les mains
    VersusResult W;
    computeVersus(pe("7c"), pe("2d"), 0, 0, 1, W);
    CHECK(W.hi.nhands > 600 && std::fabs(W.prwin + W.prtie / 2 - 0.3452) < 0.002);   // 72o contre une main aléatoire ≈ 34,5 %
}

static void testPostflopAgainstExact() {
    struct S { const char *h0, *h1; std::initializer_list<const char*> board; };
    const S cases[] = {{"Ks", "Qs", {"Js", "Ts", "2d"}}, {"Ah", "Ad", {"Ac", "7s", "7d"}}, {"9c", "9d", {"2h", "5s", "Kd", "Jc"}}, {"Qh", "Jh", {"Kh", "Th", "3c", "2d"}},
                       {"As", "Kd", {"Ah", "7s", "2d", "Kc", "9h"}}, {"6h", "6d", {"6s", "6c", "Ad", "Ah", "2c"}}, {"7s", "8s", {"Ts", "9s", "6s"}}};
    int bad = 0;
    for (const S& s : cases) {
        uint64_t bm = 0; Card bc[5]; int nb = 0;
        for (auto c : s.board) { bm |= peBit(pe(c)); bc[nb++] = cardFromString(c); }
        VersusResult R;
        computeVersus(pe(s.h0), pe(s.h1), bm, nb, 1, R);
        Card h[2] = {cardFromString(s.h0), cardFromString(s.h1)};
        Equity ex = equityExactHeadsUp(h, bc, nb);
        const double tot = R.all.win + R.all.tie + R.all.los;
        const bool ok = std::fabs(R.all.win / tot - ex.win) < 1e-12 && std::fabs(R.all.tie / tot - ex.tie) < 1e-12 && std::fabs(R.all.los / tot - ex.lose) < 1e-12 && R.prwin == R.all.win / tot;
        if (!ok) { ++bad; std::printf("  écart %s%s sur %d cartes : vs %.6f exact %.6f\n", s.h0, s.h1, nb, R.all.win / tot, ex.win); }
        const int expectHands = (52 - 2 - nb) * (52 - 3 - nb) / 2;
        CHECK(R.all.nhands == static_cast<unsigned>(expectHands));
        CHECK(R.hi.nhands + R.ti.nhands + R.lo.nhands == R.all.nhands);
        CHECK(R.hiNow.nhands + R.tiNow.nhands + R.loNow.nhands == R.all.nhands);
        if (nb == 5) CHECK(R.hi.nhands == R.hiNow.nhands && R.lo.nhands == R.loNow.nhands);   // river : plus de runout, « now » = résultat final
    }
    std::printf("  postflop contre l'énumérateur exact : %d écart(s)\n", bad);
    CHECK(bad == 0);
    // le carré d'as au flop (AdAh + Ac 7s 7d) ne perd que contre 7-7 ou As + 7 : peu de mains « hi »
    VersusResult R;
    computeVersus(pe("Ah"), pe("Ad"), peBit(pe("Ac")) | peBit(pe("7s")) | peBit(pe("7d")), 3, 1, R);
    CHECK(R.prwin > 0.9 && R.hi.nhands < 100);
}

// ---- symboles vs$ évalués sur un état de jeu réel
static void testSymbols() {
    SimConfig cfg; cfg.format = GameFormat::Cash; cfg.numPlayers = 3; cfg.numGames = 1;
    for (int i = 0; i < 3; ++i) cfg.players.push_back({std::string(1, static_cast<char>('A' + i)), "builtin:call"});
    cfg.finalize();
    std::vector<std::unique_ptr<IBrain>> own; std::vector<IBrain*> br;
    for (int i = 0; i < 3; ++i) { own.push_back(makeBuiltinBrain("call", 1, cfg)); br.push_back(own.back().get()); }
    Game g(cfg, br, {"A", "B", "C"}, {0, 1, 2}, 0, 1, false);
    for (auto& st : g.t.seats) st.dealt = st.inHand = true;
    g.t.seats[0].cards = {cardFromString("As"), cardFromString("Ah")};
    auto fc = std::make_shared<oh::FormulaCollection>();
    fc->addText("##listKK##\nKK\n##list7##\nAA KK\n##listWide##\nTT+ AKs AKo\n##listBad##\nAK\n##f$which##\n7\n##listZero##\nQQ:0\n", "x", false);
    fc->parseAll();
    fc->buildLowerIndex();
    oh::SymbolContext ctx(fc, cfg, 1, "x");
    ctx.bind(g, 0);
    auto ev = [&](const char* sym) { auto f = ctx.resolveNative(sym); return f ? f(ctx) : -12345.0; };

    // préflop : 2 adversaires encore en jeu
    CHECK(ev("vs$nhands") == 1225 && ev("vs$nhandshi") == 0 && ev("vs$nhandsti") == 1 && ev("vs$nhandslo") == 1224);
    VersusResult R; computeVersus(pe("As"), pe("Ah"), 0, 0, 1, R);
    CHECK(std::fabs(ev("vs$prwin") - std::pow(R.prwin, 2)) < 1e-12);                     // nopponentsplaying = 2 : prwin élevé au carré
    CHECK(std::fabs(ev("vs$prtie") - R.prtie) < 1e-15 && std::fabs(ev("vs$prlos") - R.prlos) < 1e-15);
    CHECK(ev("vs$prwinhi") == 0 && ev("vs$nhandshinow") == 0 && ev("vs$prwinhinow") == 0);   // pas de « now » préflop
    CHECK(std::fabs(ev("vs$prwinlo") + ev("vs$prtielo") + ev("vs$prloslo") - 1.0) < 1e-12);

    // listes : AsAh contre les 6 combinaisons de KK, calculé indépendamment avec la table
    double w = 0, t = 0, l = 0;
    for (int a = 0; a < 4; ++a) for (int b = a + 1; b < 4; ++b) {
        uint32_t ww, ll; versusPreflopLookup(pe("As"), pe("Ah"), 13 * a + 11, 13 * b + 11, ww, ll);
        w += ww; l += ll; t += kVersusBoards - ww - ll;
    }
    CHECK(std::fabs(ev("vs$listKK$prwin") - w / (w + t + l)) < 1e-12);
    CHECK(std::fabs(ev("vs$listKK$prtie") - t / (w + t + l)) < 1e-12 && std::fabs(ev("vs$listKK$prlos") - l / (w + t + l)) < 1e-12);
    CHECK(std::fabs(ev("vs$listKK$prwin") + ev("vs$listKK$prtie") + ev("vs$listKK$prlos") - 1.0) < 1e-12);
    CHECK(std::fabs(ev("vs$listKK$prwin") + ev("vs$listKK$prtie") / 2 - 0.8195) < 0.0005);   // équité d'AA contre KK : 81,95 % (valeur classique)
    CHECK(std::fabs(ev("vs$LISTKK$PRWIN") - ev("vs$listKK$prwin")) < 1e-15);              // insensible à la casse
    // multiplex : f$which = 7 -> list7 = AA KK
    const double m7 = ev("vs$multiplex$f$which$prwin");
    CHECK(m7 > 0.5 && std::fabs(m7 - ev("vs$list7$prwin")) < 1e-15);
    CHECK(ev("vs$list7$prwin") < ev("vs$listKK$prwin") + 0.2 && ev("vs$list7$prwin") > ev("vs$listKK$prwin") - 0.2);
    // cas dégénérés : liste inconnue, liste invalide, poids nul, symbole mal formé
    CHECK(ev("vs$listNope$prwin") == 0 && ev("vs$listBad$prwin") == 0);
    CHECK(ev("vs$listWide$prwin") > 0.7);
    CHECK(ev("vs$bogus") == 0 && ctx.unknownSymbols().count("vs$bogus") == 1);
    CHECK(ev("vs$123$win") == 0);
    int zeroHits = 0;
    for (int i = 0; i < 400; ++i) zeroHits += ev("vs$listZero$prwin") != 0;                // poids 0 : seulement ~1/101 des tirages
    CHECK(zeroHits > 3 && zeroHits < 60);                                                  // attendu ≈ 23 : 1 - (100/101)^6 de 400 évaluations

    // flop : doit égaler l'énumérateur exact existant (1 adversaire)
    g.t.seats[2].inHand = false;
    g.t.seats[0].cards = {cardFromString("Ks"), cardFromString("Qs")};
    g.t.board = {cardFromString("Js"), cardFromString("Ts"), cardFromString("2d")};
    ctx.bind(g, 0);
    Card h[2] = {cardFromString("Ks"), cardFromString("Qs")}, b[3] = {cardFromString("Js"), cardFromString("Ts"), cardFromString("2d")};
    Equity ex = equityExactHeadsUp(h, b, 3);
    CHECK(ev("vs$nhands") == 1081);
    CHECK(std::fabs(ev("vs$prwin") - ex.win) < 1e-12 && std::fabs(ev("vs$prtie") - ex.tie) < 1e-12 && std::fabs(ev("vs$prlos") - ex.lose) < 1e-12);
    CHECK(ev("vs$nhandshi") + ev("vs$nhandsti") + ev("vs$nhandslo") == 1081 && ev("vs$nhandshinow") + ev("vs$nhandstinow") + ev("vs$nhandslonow") == 1081);
    CHECK(ev("vs$nhandshinow") > ev("vs$nhandslonow"));                                    // KsQs n'est que « roi haut » pour l'instant (tirage) : toute paire ou as haut me bat maintenant
    CHECK(ev("vs$listKK$prwin") > 0.3 && ev("vs$listKK$prwin") < 0.9);
    // sans cartes : tout à 0
    g.t.seats[0].cards = {kNoCard, kNoCard};
    ctx.bind(g, 0);
    CHECK(ev("vs$nhands") == 0 && ev("vs$prwin") == 0 && ev("vs$listKK$prwin") == 0);
}

int main() {
    testSymbols();
    testTableAgainstBruteForce();
    testPreflopAggregates();
    testPostflopAgainstExact();
    if (failures) { std::printf("%d échec(s)\n", failures); return 1; }
    std::puts("versus : OK");
    return 0;
}
