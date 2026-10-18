#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "pokersim/runner.hpp"
#include "pokersim/evaluator.hpp"
#include "pokersim/equity.hpp"

using namespace ps;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static void testEvaluatorCounts() {
    // Dénombrement des catégories sur les C(52,5) = 2 598 960 mains
    long cnt[9] = {};
    Card h[5];
    for (int a = 0; a < 48; ++a) for (int b = a + 1; b < 49; ++b) for (int c = b + 1; c < 50; ++c)
        for (int d = c + 1; d < 51; ++d) for (int e = d + 1; e < 52; ++e) {
            h[0] = a; h[1] = b; h[2] = c; h[3] = d; h[4] = e;
            ++cnt[categoryOf(evaluate(h, 5))];
        }
    CHECK(cnt[HC_StraightFlush] == 40);
    CHECK(cnt[HC_Quads] == 624);
    CHECK(cnt[HC_FullHouse] == 3744);
    CHECK(cnt[HC_Flush] == 5108);
    CHECK(cnt[HC_Straight] == 10200);
    CHECK(cnt[HC_Trips] == 54912);
    CHECK(cnt[HC_TwoPair] == 123552);
    CHECK(cnt[HC_Pair] == 1098240);
    CHECK(cnt[HC_HighCard] == 1302540);
}

static Card C(const char* s) { return cardFromString(s); }

static void testEvaluatorOrder() {
    auto ev = [](std::initializer_list<const char*> l) { Card c[7]; int n = 0; for (auto s : l) c[n++] = cardFromString(s); return evaluate(c, n); };
    CHECK(ev({"As","Ks","Qs","Js","Ts"}) > ev({"9s","8s","7s","6s","5s"}));                 // quinte flush royale > 9-haute
    CHECK(ev({"As","2d","3h","4c","5s"}) < ev({"2d","3h","4c","5s","6s"}));                  // roue < quinte au 6
    CHECK(categoryOf(ev({"As","2d","3h","4c","5s","Kd","Kc"})) == HC_Straight);              // roue + paire au roi
    CHECK(ev({"Ah","Ad","Ac","Kh","Kd"}) > ev({"Kh","Kd","Kc","Ah","Ad"}));                 // brelan d'As plein > brelan de Rois plein
    CHECK(categoryOf(ev({"Ah","Ad","Ac","Kh","Kd","Kc","2s"})) == HC_FullHouse);             // deux brelans = full
    CHECK(ev({"Ah","Ad","Ks","Qh","Jd"}) > ev({"Ah","Ad","Ks","Qh","Td"}));                 // kicker
    CHECK(ev({"Ah","Ad","Kh","Kd","2c","3c","4c"}) == ev({"Ah","Ad","Kh","Kd","2d","3s","4h"})); // égalité exacte
    CHECK(ev({"Ah","Ad","Kh","Kd","Qc","Jc","2c"}) > ev({"Ah","Ad","Kh","Kd","Jc","Tc","2c"})); // kicker 2 paires
    CHECK(categoryOf(ev({"2h","3h","4h","5h","9h","6h","Ad"})) == HC_StraightFlush);          // quinte flush à 6 cartes de la couleur
}

static void testEquity() {
    Rng rng(42);
    Card aa[2] = {C("As"), C("Ah")};
    Equity e = equityVsRandom(aa, nullptr, 0, 1, 200000, rng);
    double v = e.win + 0.5 * e.tie;
    CHECK(std::fabs(v - 0.852) < 0.006);                      // AA vs main aléatoire ≈ 85,2 %
    Card h[2] = {C("Ks"), C("Qs")};
    Card b[3] = {C("Js"), C("Ts"), C("2d")};
    Equity ex = equityExactHeadsUp(h, b, 3);
    Equity mc = equityVsRandom(h, b, 3, 1, 200000, rng);
    CHECK(ex.exact && ex.samples == 1081L * 990L);
    CHECK(std::fabs((ex.win + 0.5 * ex.tie) - (mc.win + 0.5 * mc.tie)) < 0.01);
    CHECK(ex.win > 0.72 && ex.win < 0.80);                  // tirage quinte+couleur : ~76 % (exact = Monte-Carlo ci-dessus)
}

static void checkConservation(GameFormat f, int players, int games, int hands) {
    SimConfig c;
    c.format = f; c.numPlayers = players; c.numGames = games; c.cashHands = hands; c.workers = 1; c.seed = 7;
    c.cashStackMode = CashStackMode::Rebuy;
    for (int i = 0; i < players; ++i) c.players.push_back({"p" + std::to_string(i), i % 2 ? "builtin:random" : "builtin:call"});
    c.players[0].profile = "builtin:tag";
    if (f == GameFormat::SNG) c.cashAnte = 0;
    c.finalize();
    SimSummary s = runSimulation(c, defaultBrainFactory(), true);
    long handsSeen = 0;
    for (auto& g : s.games) {
        for (auto& h : g.records) {
            long start = 0, end = 0, net = 0;
            for (auto& st : h.seats) { start += st.startStack; end += st.endStack; net += st.collected - st.committed; }
            CHECK(end + h.rake == start);
            CHECK(net + h.rake == 0);
            ++handsSeen;
        }
        if (f != GameFormat::Cash) {
            int winners = 0; double paid = 0;
            for (auto& p : g.players) { winners += p.place == 1; paid += p.gain; }
            CHECK(winners == 1);
            CHECK(std::fabs(paid - g.prizepool) < 1e-6);
        }
    }
    std::printf("  %s %d joueurs : %ld mains vérifiées\n", formatName(f), players, handsSeen);
}

int main() {
    std::puts("évaluateur : dénombrement des 2 598 960 mains");
    testEvaluatorCounts();
    testEvaluatorOrder();
    std::puts("équité");
    testEquity();
    std::puts("conservation des jetons");
    checkConservation(GameFormat::Cash, 6, 3, 400);
    checkConservation(GameFormat::Cash, 2, 3, 400);
    checkConservation(GameFormat::SNG, 9, 20, 0);
    checkConservation(GameFormat::SpinGo, 3, 100, 0);
    checkConservation(GameFormat::SpinGo, 2, 50, 0);
    if (failures) { std::printf("%d échec(s)\n", failures); return 1; }
    std::puts("OK");
    return 0;
}
