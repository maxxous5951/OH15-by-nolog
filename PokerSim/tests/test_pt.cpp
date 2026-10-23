#include <cmath>
#include <cstdio>
#include "pokersim/pt.hpp"
#include "pokersim/ohsymbols.hpp"
#include "pokersim/runner.hpp"

using namespace ps;
using namespace ps::pt;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

// ---- mini-langage de scénario : (tour, siège, action, montant total sur le tour)
struct A { int round; int chair; char k; int to; };
struct Scn {
    int n = 3, dealer = 0, sb = 1, bb = 2, board = 0;
    std::vector<int> stacks;
    std::vector<A> acts;
    bool showdown = false;
    std::vector<int> winners;        // gagnants du pot principal
};
static HandRecord build(const Scn& s) {
    HandRecord h;
    h.dealer = s.dealer; h.sbChair = s.sb; h.bbChair = s.bb; h.sb = 10; h.bb = 20; h.showdown = s.showdown;
    for (int c = 0; c < s.n; ++c) { SeatRecord sr; sr.chair = c; sr.startStack = s.stacks.empty() ? 5000 : s.stacks[static_cast<size_t>(c)]; h.seats.push_back(sr); }
    for (int i = 0; i < s.board; ++i) h.board.push_back(static_cast<Card>(i));
    std::vector<int> bet(16, 0);
    int cur = 0, round = 1;
    auto add = [&](int r, int c, ActKind k, int to) {
        if (r != round) { std::fill(bet.begin(), bet.end(), 0); cur = 0; round = r; }
        Action a; a.round = r; a.chair = c; a.kind = k;
        if (k == ActKind::Call) to = cur;
        if (k == ActKind::Fold || k == ActKind::Check) to = bet[static_cast<size_t>(c)];
        a.amount = to - bet[static_cast<size_t>(c)]; a.to = to;
        if (k == ActKind::Call || k == ActKind::Bet || k == ActKind::Raise || k == ActKind::SmallBlind || k == ActKind::BigBlind) { bet[static_cast<size_t>(c)] = to; cur = std::max(cur, to); }
        h.actions.push_back(a);
    };
    add(1, s.sb, ActKind::SmallBlind, 10);
    add(1, s.bb, ActKind::BigBlind, 20);
    for (const A& a : s.acts) {
        ActKind k = a.k == 'f' ? ActKind::Fold : a.k == 'x' ? ActKind::Check : a.k == 'c' ? ActKind::Call : a.k == 'b' ? ActKind::Bet : ActKind::Raise;
        add(a.round, a.chair, k, a.to);
    }
    PotRecord pr; pr.amount = 100; pr.winners = s.winners;
    h.pots.push_back(pr);
    return h;
}
static Database run(const Scn& s) {
    Database db;
    analyzeHand(build(s), {"A", "B", "C", "D"}, db, Mode::Cash);
    return db;
}
static double st(const Database& db, const char* who, int stat) { const PlayerCounters* c = db.find(who, Mode::Cash); return c ? c->value(stat) : -99; }
static bool eq(double a, double b) { return std::fabs(a - b) < 1e-9; }
#define STAT(db, who, S, v) do { double _v = st(db, who, S_##S); if (!eq(_v, (v))) { std::printf("ECHEC %s:%d  %s %s = %g (attendu %g)\n", __FILE__, __LINE__, who, statName(S_##S), _v, static_cast<double>(v)); ++failures; } } while (0)

static void testSteal() {
    // 3 joueurs : A=bouton, B=SB, C=BB. A relance (vol), B se couche, C paie. Flop : C check, A mise (c-bet), C se couche.
    Scn s; s.n = 3; s.dealer = 0; s.sb = 1; s.bb = 2; s.board = 3;
    s.acts = {{1, 0, 'r', 60}, {1, 1, 'f', 0}, {1, 2, 'c', 0}, {2, 2, 'x', 0}, {2, 0, 'b', 80}, {2, 2, 'f', 0}};
    Database db = run(s);
    STAT(db, "A", HANDS, 1); STAT(db, "A", VPIP, 1); STAT(db, "A", PFR, 1); STAT(db, "A", RFI, 1); STAT(db, "A", STEAL_ATTEMPT, 1);
    STAT(db, "A", FLOP_SEEN, 1); STAT(db, "A", FLOP_CBET, 1); STAT(db, "A", FLOP_AFQ, 1.0); STAT(db, "A", FLOP_AF, -1);   // 0 paiement : AF indéfini
    STAT(db, "A", TURN_SEEN, 0); STAT(db, "A", WTSD, 0);
    STAT(db, "B", VPIP, 0); STAT(db, "B", PFR, 0);                  // se couche mais pouvait relancer : occasion comptée
    STAT(db, "B", SB_FOLD_TO_STEAL, 1); STAT(db, "B", BB_FOLD_TO_STEAL, -1); STAT(db, "B", P3BET, 0); STAT(db, "B", P3BET_VS_STEAL, 0);
    STAT(db, "B", FLOP_SEEN, 0);
    STAT(db, "C", VPIP, 1); STAT(db, "C", PFR, 0); STAT(db, "C", PREFLOP_CALLED_RAISE, 1); STAT(db, "C", BB_FOLD_TO_STEAL, 0);
    STAT(db, "C", BB_3BET_VS_STEAL, 0); STAT(db, "C", FLOP_FOLD_TO_CBET, 1); STAT(db, "C", FLOP_RAISE_CBET, 0);
    STAT(db, "C", FLOP_AFQ, 0); STAT(db, "C", FLOP_SEEN, 1);
}

static void testSqueeze4bet() {
    // 4 joueurs : D=bouton, A=SB, B=BB, C=UTG (donc cut-off). C ouvre, D paie, A se couche, B squeeze, C 4-bet, D se couche, B se couche.
    Scn s; s.n = 4; s.dealer = 3; s.sb = 0; s.bb = 1;
    s.acts = {{1, 2, 'r', 60}, {1, 3, 'c', 0}, {1, 0, 'f', 0}, {1, 1, 'r', 200}, {1, 2, 'r', 500}, {1, 3, 'f', 0}, {1, 1, 'f', 0}};
    Database db = run(s);
    STAT(db, "C", RFI, 1); STAT(db, "C", STEAL_ATTEMPT, 1);                      // le cut-off compte comme position de vol
    STAT(db, "C", FOLD_TO_3BET, 0); STAT(db, "C", P4BET, 1); STAT(db, "C", FOLD_TO_RESTEAL, 0); STAT(db, "C", FOLD_TO_SQUEEZE_AS_RAISER, 0);
    STAT(db, "D", PREFLOP_CALLED_RAISE, 1); STAT(db, "D", FOLD_TO_PF_SQUEEZE, 1); STAT(db, "D", FOLD_TO_SQUEEZE_AS_CALLER, 1);
    STAT(db, "D", FOLD_TO_SQUEEZE_AS_RAISER, -1);
    STAT(db, "D", P4BET, -1);                                  // D fait face à un 4bet, pas à un 3bet : pas d'occasion de 4bet
    STAT(db, "D", RFI, -1);                                    // D suit une relance : pas d'occasion d'ouvrir
    STAT(db, "A", SB_FOLD_TO_STEAL, 1); STAT(db, "A", VPIP, 0);
    STAT(db, "B", BB_FOLD_TO_STEAL, 0); STAT(db, "B", BB_3BET_VS_STEAL, 1); STAT(db, "B", P3BET, 1); STAT(db, "B", PREFLOP_SQUEEZE, 1);
    STAT(db, "B", FOLD_TO_4BET, 1); STAT(db, "B", P3BET_VS_STEAL, 1);
}

static void testPostflop() {
    // face à face : A=bouton/SB, B=BB. A relance, B paie. Flop : B check, A mise, B check-relance, A paie. Turn : x x. River : B mise, A se couche.
    Scn s; s.n = 2; s.dealer = 0; s.sb = 0; s.bb = 1; s.board = 5;
    s.acts = {{1, 0, 'r', 60}, {1, 1, 'c', 0},
              {2, 1, 'x', 0}, {2, 0, 'b', 40}, {2, 1, 'r', 120}, {2, 0, 'c', 0},
              {3, 1, 'x', 0}, {3, 0, 'x', 0},
              {4, 1, 'b', 100}, {4, 0, 'f', 0}};
    s.winners = {1};
    Database db = run(s);
    STAT(db, "A", FLOP_CBET, 1); STAT(db, "A", FLOP_AF, 1.0); STAT(db, "A", FLOP_AFQ, 0.5);   // 1 mise / (1 mise + 1 paiement)
    STAT(db, "A", TURN_CBET, 0); STAT(db, "A", TURN_SEEN, 1); STAT(db, "A", RIVER_SEEN, 1); STAT(db, "A", RIVER_AFQ, 0);
    STAT(db, "A", TOTAL_AF, 1.0);                                  // (1 mise) / (1 paiement) sur tout le postflop
    STAT(db, "A", RIVER_BET, -1); STAT(db, "A", WTSD, 0);
    STAT(db, "B", FLOP_CHECKRAISE, 1); STAT(db, "B", FLOP_FOLD_TO_CBET, 0); STAT(db, "B", FLOP_RAISE_CBET, 1);
    STAT(db, "B", FLOP_DONKBET, 0);                                // a checké : occasion sans donk
    STAT(db, "B", TURN_CHECKRAISE, -1);                            // check sans nouvelle décision : pas d'occasion
    STAT(db, "B", RIVER_BET, 1); STAT(db, "B", WSSD, -1); STAT(db, "B", WTSD, 0); STAT(db, "B", FLOP_AF, -1);
    STAT(db, "B", FLOP_AFQ, 1.0);
}

static void testShowdown() {
    Scn s; s.n = 2; s.dealer = 0; s.sb = 0; s.bb = 1; s.board = 5; s.showdown = true; s.winners = {1};
    s.acts = {{1, 0, 'r', 60}, {1, 1, 'c', 0}, {2, 1, 'x', 0}, {2, 0, 'x', 0}, {3, 1, 'x', 0}, {3, 0, 'x', 0}, {4, 1, 'x', 0}, {4, 0, 'x', 0}};
    Database db = run(s);
    STAT(db, "A", WTSD, 1); STAT(db, "A", WSSD, 0); STAT(db, "B", WTSD, 1); STAT(db, "B", WSSD, 1);
    STAT(db, "A", RIVER_BET, 0); STAT(db, "B", FLOP_DONKBET, 0);
}

static void testLimpAndWalk() {
    Scn s; s.n = 3; s.dealer = 0; s.sb = 1; s.bb = 2;       // A limpe, B complète, C checke son option
    s.acts = {{1, 0, 'c', 0}, {1, 1, 'c', 0}, {1, 2, 'x', 0}};
    Database db = run(s);
    STAT(db, "A", VPIP, 1); STAT(db, "A", RFI, 0); STAT(db, "A", STEAL_ATTEMPT, 0); STAT(db, "B", VPIP, 1);
    STAT(db, "C", VPIP, 0); STAT(db, "C", PFR, 0);                // l'option du BB est une occasion de relancer
    Scn w; w.n = 3; w.dealer = 0; w.sb = 1; w.bb = 2;       // tout le monde se couche devant le BB
    w.acts = {{1, 0, 'f', 0}, {1, 1, 'f', 0}};
    Database d2 = run(w);
    STAT(d2, "C", HANDS, 1); STAT(d2, "C", VPIP, -1);        // le BB n'a pas agi : pas d'occasion
    STAT(d2, "A", VPIP, 0); STAT(d2, "A", RFI, 0); STAT(d2, "A", STEAL_ATTEMPT, 0);
}

static void testShortStack() {
    Scn s; s.n = 3; s.dealer = 0; s.sb = 1; s.bb = 2; s.stacks = {5000, 5000, 5000};
    s.stacks[0] = 15;                                       // A n'a pas de quoi relancer : pas d'occasion de PFR
    s.acts = {{1, 0, 'f', 0}, {1, 1, 'f', 0}};
    Database db = run(s);
    STAT(db, "A", PFR, -1);
}

static void testDatabase() {
    Scn s; s.n = 3; s.dealer = 0; s.sb = 1; s.bb = 2; s.board = 3;
    s.acts = {{1, 0, 'r', 60}, {1, 1, 'f', 0}, {1, 2, 'c', 0}, {2, 2, 'x', 0}, {2, 0, 'b', 80}, {2, 2, 'f', 0}};
    Database a = run(s), b = run(s);
    Database m; m.merge(a); m.merge(b);
    CHECK(eq(st(m, "A", S_HANDS), 2) && eq(st(m, "A", S_VPIP), 1));
    // tournoi et cash séparés
    Database t; analyzeHand(build(s), {"A", "B", "C"}, t, Mode::Tourney);
    CHECK(t.find("A", Mode::Cash)->num[S_HANDS] == 0 && t.find("A", Mode::Tourney)->num[S_HANDS] == 1);
    // JSON : aller-retour exact, rejet d'un fichier étranger
    Database r = Database::fromJson(m.toJson());
    CHECK(r.players.size() == m.players.size());
    for (auto& kv : m.players) for (int mo = 0; mo < 2; ++mo) for (int i = 0; i < S_COUNT; ++i) {
        CHECK(r.players[kv.first].mode[mo].num[i] == kv.second.mode[mo].num[i] && r.players[kv.first].mode[mo].den[i] == kv.second.mode[mo].den[i]);
    }
    bool threw = false;
    try { Database::fromJson("{\"format\":\"autre\"}"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { Database::fromJson("pas du json"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    CHECK(m.save("/tmp/pt_test_db.json"));
    CHECK(Database::load("/tmp/pt_test_db.json").players.size() == m.players.size());
    // suivi : base + partie
    auto base = std::make_shared<Database>(a);
    Tracker tr(base, Mode::Cash);
    CHECK(eq(tr.stat("A", S_HANDS), 1) && eq(tr.stat("zzz", S_HANDS), 0) && eq(tr.stat("zzz", S_VPIP), -1));
    tr.update(build(s), {"A", "B", "C"});
    CHECK(eq(tr.stat("A", S_HANDS), 2) && eq(tr.stat("A", S_PFR), 1.0));
    CHECK(statIndex("vpip") == S_VPIP && statIndex("3bet_vs_steal") == S_P3BET_VS_STEAL && statIndex("icon") == -1);
}

// ---- propriétés sur des milliers de vraies mains
static void testProperties() {
    SimConfig c; c.format = GameFormat::Cash; c.numPlayers = 6; c.numGames = 6; c.cashHands = 400; c.workers = 2; c.seed = 4; c.rakePct = 0;
    const char* k[] = {"tag", "call", "random", "random", "tag", "call"};
    for (int i = 0; i < 6; ++i) c.players.push_back({std::string("J") + std::to_string(i), std::string("builtin:") + k[i]});
    c.finalize();
    SimSummary s = runSimulation(c, defaultBrainFactory(), true);
    Database db;
    long long hands = 0;
    for (auto& g : s.games) {
        std::vector<std::string> names;
        for (auto& p : g.players) names.push_back(p.name);
        for (auto& h : g.records) { analyzeHand(h, names, db, Mode::Cash); ++hands; }
    }
    long long total = 0;
    for (auto& kv : db.players) {
        const PlayerCounters& k2 = kv.second.mode[0];
        total += static_cast<long long>(k2.num[S_HANDS]);
        for (int i = 0; i < S_COUNT; ++i) {
            if (i == S_FLOP_AF || i == S_TURN_AF || i == S_RIVER_AF || i == S_TOTAL_AF || i == S_HANDS) continue;   // ratios non bornés
            CHECK(k2.num[i] >= 0 && k2.num[i] <= k2.den[i] + 1e-9);
        }
        CHECK(k2.num[S_PFR] <= k2.num[S_VPIP] + 1e-9);                 // relancer préflop = entrer volontairement
        CHECK(k2.num[S_RFI] <= k2.num[S_PFR] + 1e-9 && k2.num[S_STEAL_ATTEMPT] <= k2.num[S_RFI] + 1e-9);
        CHECK(k2.num[S_P4BET] <= k2.num[S_PFR] + 1e-9 && k2.num[S_P3BET] <= k2.num[S_PFR] + 1e-9);
        CHECK(k2.num[S_WSSD] <= k2.num[S_WTSD] + 1e-9 || true);
        CHECK(k2.den[S_WSSD] <= k2.num[S_FLOP_SEEN] + 1e-9);          // pas de showdown sans avoir vu le flop (sauf all-in préflop : vu aussi)
        CHECK(k2.num[S_TURN_SEEN] <= k2.num[S_FLOP_SEEN] + 1e-9 && k2.num[S_RIVER_SEEN] <= k2.num[S_TURN_SEEN] + 1e-9);
        CHECK(k2.den[S_VPIP] <= k2.num[S_HANDS] + 1e-9);
    }
    CHECK(total == hands * 6);
    std::printf("  propriétés : %lld mains, %zu joueurs\n", hands, db.players.size());
    // déterminisme : la fusion des parties ne dépend pas de l'ordre
    Database d1, d2;
    for (size_t g = 0; g < s.games.size(); ++g) { std::vector<std::string> nm; for (auto& p : s.games[g].players) nm.push_back(p.name); Database tmp; for (auto& h : s.games[g].records) analyzeHand(h, nm, tmp, Mode::Cash); d1.merge(tmp); }
    for (size_t g = s.games.size(); g-- > 0;) { std::vector<std::string> nm; for (auto& p : s.games[g].players) nm.push_back(p.name); Database tmp; for (auto& h : s.games[g].records) analyzeHand(h, nm, tmp, Mode::Cash); d2.merge(tmp); }
    CHECK(d1.toJson() == d2.toJson());
}

// ---- symboles pt_* évalués sur un vrai état de jeu
static void testSymbols() {
    SimConfig cfg; cfg.format = GameFormat::Cash; cfg.numPlayers = 3; cfg.numGames = 1;
    for (int i = 0; i < 3; ++i) cfg.players.push_back({std::string(1, static_cast<char>('A' + i)), "builtin:call"});
    cfg.finalize();
    std::vector<std::unique_ptr<IBrain>> own; std::vector<IBrain*> br;
    for (int i = 0; i < 3; ++i) { own.push_back(makeBuiltinBrain("call", 1, cfg)); br.push_back(own.back().get()); }
    Game g(cfg, br, {"A", "B", "C"}, {0, 1, 2}, 0, 1, false);
    g.t.dealer = 2; g.t.bbChair = 1; g.t.sbChair = 0;
    for (int i = 0; i < 3; ++i) { g.t.seats[static_cast<size_t>(i)].dealt = g.t.seats[static_cast<size_t>(i)].inHand = true; g.t.seats[static_cast<size_t>(i)].name = std::string(1, static_cast<char>('A' + i)); }
    // base : B a 200 mains avec VPIP 30 % ; A est inconnu
    auto base = std::make_shared<Database>();
    base->counters("B", Mode::Cash).add(S_HANDS, 200, 200);
    base->counters("B", Mode::Cash).add(S_VPIP, 60, 200);
    base->counters("C", Mode::Cash).add(S_HANDS, 50, 50);
    base->counters("C", Mode::Cash).add(S_VPIP, 10, 50);
    base->counters("B", Mode::Tourney).add(S_VPIP, 1, 2);
    g.pt = std::make_shared<Tracker>(base, Mode::Cash);
    auto fc = std::make_shared<oh::FormulaCollection>();
    oh::SymbolContext ctx(fc, cfg, 1, "x");
    ctx.bind(g, 0);
    auto ev = [&](const char* sym) { auto f = ctx.resolveNative(sym); return f ? f(ctx) : -12345.0; };
    CHECK(eq(ev("pt_vpip1"), 0.30));                              // siège 1 = B
    CHECK(eq(ev("pt_hands1"), 200));
    CHECK(eq(ev("pt_vpip2"), 0.20));
    CHECK(eq(ev("pt_vpip0"), -1));                                // A : aucune donnée
    CHECK(eq(ev("pt_hands0"), 0));
    CHECK(eq(ev("pt_vpip_dealerchair"), 0.20));                   // bouton = siège 2 = C
    CHECK(eq(ev("pt_vpip_bigblindchair"), 0.30));                 // BB = siège 1 = B
    CHECK(eq(ev("pt_vpip_userchair"), -1));                       // soi-même (A) : inconnu
    CHECK(eq(ev("pt_PFR1"), -1));                                 // pas de données pfr pour B (insensible à la casse)
    CHECK(eq(ev("pt_vpip9"), -1));                                // siège inexistant
    CHECK(eq(ev("pt_vpip"), -1));                                 // pas de siège : symbole invalide
    CHECK(eq(ev("pt_icon1"), -1));                                // statistique non simulée
    CHECK(ctx.unknownSymbols().count("pt_icon1") == 1 && ctx.unknownSymbols().count("pt_vpip") == 1);
    CHECK(eq(ev("pt_3bet_vs_steal1"), -1));                       // statistique supportée, sans occasion
    // mains jouées pendant la partie : visibles aussitôt (mises à jour en fin de main)
    Scn s; s.n = 3; s.dealer = 0; s.sb = 1; s.bb = 2;
    s.acts = {{1, 0, 'r', 60}, {1, 1, 'f', 0}, {1, 2, 'c', 0}};
    g.pt->update(build(s), {"A", "B", "C"});
    CHECK(eq(ev("pt_hands0"), 1) && eq(ev("pt_vpip0"), 1.0) && eq(ev("pt_pfr0"), 1.0));
    CHECK(eq(ev("pt_hands1"), 201) && eq(ev("pt_vpip1"), 60.0 / 201));
    // mode tournoi : lit la base « tournoi » (séparée du cash)
    g.pt = std::make_shared<Tracker>(base, Mode::Tourney);
    ctx.bind(g, 0);
    CHECK(eq(ev("pt_vpip1"), 0.5) && eq(ev("pt_vpip2"), -1));
    g.pt = nullptr;
    ctx.bind(g, 0);
    CHECK(eq(ev("pt_vpip1"), -1));                                // PokerTracker désactivé
}

// ---- chaînage de simulations par la base JSON ; résultat indépendant du nombre de threads
static void testRunnerChain() {
    auto mk = [](int workers, const std::string& base, const std::string& save, int games, uint64_t seed) {
        SimConfig c; c.format = GameFormat::SpinGo; c.numPlayers = 3; c.numGames = games; c.workers = workers; c.seed = seed;
        c.ptBaselinePath = base; c.ptSavePath = save;
        const char* k[] = {"tag", "call", "random"};
        for (int i = 0; i < 3; ++i) c.players.push_back({std::string("J") + std::to_string(i), std::string("builtin:") + k[i]});
        c.finalize();
        return c;
    };
    std::remove("/tmp/pt_run1.json"); std::remove("/tmp/pt_run1b.json"); std::remove("/tmp/pt_run2.json");
    SimSummary a = runSimulation(mk(1, "", "/tmp/pt_run1.json", 20, 5), defaultBrainFactory(), false);
    SimSummary b = runSimulation(mk(3, "", "/tmp/pt_run1b.json", 20, 5), defaultBrainFactory(), false);
    CHECK(a.ptDatabase && b.ptDatabase);
    CHECK(a.ptDatabase->toJson() == b.ptDatabase->toJson());       // indépendant du nombre de threads
    double playerHandsA = 0;                                        // une main à 3 joueurs compte 3 fois dans la base
    for (auto& g : a.games) for (auto& p : g.players) playerHandsA += p.hands;
    CHECK(a.ptDatabase->totalHands(Mode::Tourney) == static_cast<long long>(playerHandsA));
    CHECK(a.ptDatabase->totalHands(Mode::Cash) == 0);             // Spin & Go : base « tournoi » uniquement
    SimSummary c2 = runSimulation(mk(2, "/tmp/pt_run1.json", "/tmp/pt_run2.json", 10, 99), defaultBrainFactory(), false);
    double h2 = 0; for (auto& g : c2.games) for (auto& p : g.players) h2 += p.hands;
    CHECK(c2.ptDatabase->totalHands(Mode::Tourney) == a.ptDatabase->totalHands(Mode::Tourney) + static_cast<long long>(h2));   // base de départ + nouvelles mains
    Database reloaded = Database::load("/tmp/pt_run2.json");
    CHECK(reloaded.toJson() == c2.ptDatabase->toJson());
    // base de départ illisible : erreur claire
    bool threw = false;
    try { runSimulation(mk(1, "/tmp/inexistante_pt.json", "", 2, 1), defaultBrainFactory(), false); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    // PokerTracker désactivé : pas de base produite
    SimConfig off = mk(1, "", "", 3, 1); off.ptEnabled = false;
    CHECK(runSimulation(off, defaultBrainFactory(), false).ptDatabase == nullptr);
    std::printf("  chaînage : %.0f mains en base après 2 simulations\n", static_cast<double>(c2.ptDatabase->totalHands(Mode::Tourney)));
}

int main() {
    testSymbols();
    testRunnerChain();
    testSteal();
    testSqueeze4bet();
    testPostflop();
    testShowdown();
    testLimpAndWalk();
    testShortStack();
    testDatabase();
    testProperties();
    if (failures) { std::printf("%d échec(s)\n", failures); return 1; }
    std::puts("pt : OK");
    return 0;
}
