#include <cmath>
#include <fstream>
#include <sstream>
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

// ---- historique complet des mains : invariants vérifiés sur le texte écrit
static long long intAfter(const std::string& s, size_t from, size_t* end = nullptr) {
    size_t i = from;
    while (i < s.size() && !(s[i] >= '0' && s[i] <= '9')) ++i;
    long long v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') { v = v * 10 + (s[i] - '0'); ++i; }
    if (end) *end = i;
    return v;
}
static long long sumAll(const std::string& blk, const std::string& marker) {
    long long t = 0;
    for (size_t p = blk.find(marker); p != std::string::npos; p = blk.find(marker, p + 1)) t += intAfter(blk, p + marker.size());
    return t;
}
static void checkHandFile(const std::string& path, int expectGames, const char* label) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    const std::string all = ss.str();
    CHECK(all.size() > 3 && static_cast<unsigned char>(all[0]) == 0xEF && static_cast<unsigned char>(all[1]) == 0xBB && static_cast<unsigned char>(all[2]) == 0xBF);   // BOM UTF-8
    // parties dans l'ordre 1..N
    int last = 0, games = 0;
    for (size_t p = all.find("################ "); p != std::string::npos; p = all.find("################ ", p + 1)) {
        size_t e = all.find(' ', p + 17);
        if (all.compare(p + 17, 6, "PARTIE") == 0 || all.compare(p + 17, 7, "SESSION") == 0) { int n = static_cast<int>(intAfter(all, p + 17)); if (n != last + 1) { std::printf("  ordre : %d après %d\n", n, last); ++failures; } last = n; ++games; }
        (void)e;
    }
    CHECK(games == expectGames);
    int hands = 0, bad = 0;
    for (size_t p = all.find("---- Main "); p != std::string::npos;) {
        size_t nx = all.find("---- Main ", p + 1);
        const std::string blk = all.substr(p, nx == std::string::npos ? std::string::npos : nx - p);
        ++hands;
        size_t sum = blk.find("*** RÉSUMÉ ***");
        if (sum == std::string::npos) { ++bad; }
        else {
            const long long staked = sumAll(blk.substr(sum), "misé ");
            long long annex = 0;                                   // « Pot annexe N montant » : le montant est le 2e nombre
            for (size_t q = blk.find("Pot annexe "); q != std::string::npos; q = blk.find("Pot annexe ", q + 1)) { size_t e = 0; intAfter(blk, q + 11, &e); annex += intAfter(blk, e); }
            const long long out = sumAll(blk, "Pot principal ") + annex + sumAll(blk, "Mise non payée ") + sumAll(blk, "Rake : ");
            const long long potNo = 0;
            if (staked != out - potNo) { if (++bad < 4) std::printf("  main incohérente (%s) : misé %lld, pots+rendu+rake %lld\n%s\n", label, staked, out - potNo, blk.c_str()); }
        }
        // pot affiché au début du préflop = mises forcées
        size_t pf = blk.find("*** PRÉFLOP *** (pot : ");
        if (pf != std::string::npos) {
            long long shown = intAfter(blk, pf + 22), forced = sumAll(blk, "paie l'ante ") + sumAll(blk, "poste la petite blind ") + sumAll(blk, "poste la grosse blind ");
            if (shown != forced) { if (++bad < 4) std::printf("  pot préflop %lld != mises forcées %lld\n", shown, forced); }
        } else ++bad;
        p = nx;
    }
    std::printf("  %s : %d parties, %d mains, %d main(s) incohérente(s)\n", label, games, hands, bad);
    CHECK(hands > 0 && bad == 0);
}

static void testHandHistoryFile() {
    {   // cash avec rake, 4 threads : parties écrites dans l'ordre
        SimConfig c; c.format = GameFormat::Cash; c.numPlayers = 5; c.numGames = 8; c.cashHands = 150; c.workers = 4; c.seed = 3; c.rakePct = 5; c.rakeCap = 30;
        c.cashStackMode = CashStackMode::Rebuy; c.handHistoryPath = "/tmp/ps_hh_cash.txt";
        const char* k[] = {"tag", "call", "random", "random", "tag"};
        for (int i = 0; i < 5; ++i) c.players.push_back({std::string("J") + std::to_string(i), std::string("builtin:") + k[i]});
        c.finalize();
        SimSummary s = runSimulation(c, defaultBrainFactory(), false);
        CHECK(s.handHistoryHands == 8 * 150 && s.handHistoryBytes > 10000);
        for (auto& g : s.games) CHECK(g.records.empty());          // pas de mains gardées en mémoire
        checkHandFile(c.handHistoryPath, 8, "cash");
    }
    {   // SNG avec antes (montée rapide aux mains) : le pot préflop inclut les antes
        SimConfig c; c.format = GameFormat::SNG; c.numPlayers = 6; c.numGames = 6; c.workers = 3; c.seed = 9; c.levelBy = LevelBy::Hands; c.levelHands = 2;
        c.handHistoryPath = "/tmp/ps_hh_sng.txt";
        const char* k[] = {"tag", "call", "random", "call", "random", "tag"};
        for (int i = 0; i < 6; ++i) c.players.push_back({std::string("J") + std::to_string(i), std::string("builtin:") + k[i]});
        c.finalize();
        SimSummary s = runSimulation(c, defaultBrainFactory(), false);
        CHECK(s.handHistoryHands > 6);
        checkHandFile(c.handHistoryPath, 6, "sng (antes)");
        std::ifstream f(c.handHistoryPath, std::ios::binary); std::stringstream ss; ss << f.rdbuf();
        CHECK(ss.str().find("paie l'ante") != std::string::npos);
    }
    {   // limite de parties et chemin invalide
        SimConfig c; c.format = GameFormat::SpinGo; c.numPlayers = 3; c.numGames = 10; c.workers = 2; c.handHistoryPath = "/tmp/ps_hh_lim.txt"; c.handHistoryGames = 3;
        for (int i = 0; i < 3; ++i) c.players.push_back({"J" + std::to_string(i), "builtin:call"});
        c.finalize();
        runSimulation(c, defaultBrainFactory(), false);
        checkHandFile(c.handHistoryPath, 3, "limite à 3 parties");
        c.handHistoryPath = "/nulle/part/mains.txt";
        bool threw = false;
        try { runSimulation(c, defaultBrainFactory(), false); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
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
    std::puts("historique complet des mains");
    testHandHistoryFile();
    if (failures) { std::printf("%d échec(s)\n", failures); return 1; }
    std::puts("OK");
    return 0;
}
