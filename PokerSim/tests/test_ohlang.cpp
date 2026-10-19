#include <cmath>
#include <cstdio>
#include <map>
#include "pokersim/ohlang.hpp"

using namespace ps;
using namespace ps::oh;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

struct Mock : IEval {
    std::map<std::string, double> sym; std::map<std::string, bool> vars;
    double symbol(int id) override { auto it = sym.find(names().name(id)); return it == sym.end() ? 0.0 : it->second; }
    double setVariable(int id) override { vars[names().name(id)] = true; return 0.0; }
    bool hasFunction(int) override { return false; }
};

static double eval(const std::string& src, Mock& m, const std::string& fname = "f$test") {
    Arena a;
    const Node* n = parseFunctionBody(src, fname, defaultFunctionFor(fname), a);
    return n->ev(m);
}
static double E(const std::string& src) { Mock m; return eval(src, m); }
static bool parseFails(const std::string& src) { Arena a; try { parseFunctionBody(src, "f$t", "", a); } catch (const ParseError&) { return true; } return false; }
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static void testOperators() {
    CHECK(near(E("1+2*3"), 7));
    CHECK(near(E("(1+2)*3"), 9));
    CHECK(near(E("2**3**2"), 512));                 // ** associatif à droite
    CHECK(near(E("10 mod 3"), 1));
    CHECK(near(E("10 - 3 - 2"), 5));                // associatif à gauche
    CHECK(near(E("5 + -3"), 2));
    CHECK(near(E("-5 + 3"), -2));
    CHECK(near(E("1/0"), -1));                      // OpenHoldem : kUndefined
    CHECK(near(E("7 mod 0"), -1));
    CHECK(near(E("7 > 3 && 2 < 1"), 0));
    CHECK(near(E("7 > 3 || 2 < 1"), 1));
    CHECK(near(E("1 ? 10 : 20"), 10));
    CHECK(near(E("0 ? 10 : 0 ? 20 : 30"), 30));
    CHECK(near(E("0.1+0.2 == 0.3"), 1));            // tolérance epsilon
    CHECK(near(E("2.5 ~~ 3"), 1));                  // égalité après arrondi
    CHECK(near(E("6 & 3"), 2));
    CHECK(near(E("6 | 3"), 7));
    CHECK(near(E("6 ^ 3"), 5));
    CHECK(near(E("1 << 4"), 16));
    CHECK(near(E("256 >> 4"), 16));
    CHECK(near(E("~0"), 4294967295.0));             // unsigned long 32 bits
    CHECK(near(E("bitcount(7)"), 3));
    CHECK(near(E("0x1F"), 31));
    CHECK(near(E("0b101"), 5));
    CHECK(near(E("1 and 0 or 1"), 1));              // mots-clés
    CHECK(near(E("not 0"), 1));
    CHECK(near(E("1 xor 1"), 0));
    CHECK(near(E("/* commentaire */ 4 // fin"), 4));
    CHECK(near(E("1 < 2 == 1"), 1));                // < (9) plus prioritaire que == (8)
}

static void testSymbolsAndPpl() {
    Mock m; m.sym["a"] = 3; m.sym["b"] = 4; m.sym["Call"] = 99; m.sym["Fold"] = 77;
    CHECK(near(eval("a*a + b*b", m), 25));
    // WHEN simple : première condition vraie, sinon DefaultPreflop (absent -> 0)
    const std::string ppl = "WHEN a = 1 RETURN 5 FORCE\nWHEN b = 4 Call FORCE\nWHEN a = 3 Fold FORCE";
    CHECK(near(eval(ppl, m, "f$preflop"), 99));
    m.sym["b"] = 0;
    CHECK(near(eval(ppl, m, "f$preflop"), 77));
    m.sym["a"] = 1;
    CHECK(near(eval(ppl, m, "f$preflop"), 5));
    m.sym["a"] = 0;
    CHECK(near(eval(ppl, m, "f$preflop"), 0));
    // RaiseBy / RaiseTo
    Mock r; r.sym["ncallbets"] = 2; r.sym["bet"] = 10; r.sym["pot"] = 50; r.sym["nbetstocall"] = 1;
    CHECK(near(eval("WHEN 1 RaiseTo 7 FORCE", r, "f$flop"), 7));
    CHECK(near(eval("WHEN 1 RaiseBy 3 FORCE", r, "f$flop"), 5));
    CHECK(near(eval("WHEN 1 RaiseBy 50% FORCE", r, "f$flop"), 2 + 0.01 * 50 * (50.0 / 10 + 1)));
    // Set user_xxx puis on continue avec la condition suivante
    Mock u; u.sym["Call"] = 99;
    CHECK(near(eval("WHEN 1 Set user_x\nWHEN 1 Call FORCE", u, "f$flop"), 99));
    CHECK(u.vars.count("user_x") == 1);
    // erreurs
    CHECK(parseFails("WHEN 1 Call"));              // FORCE manquant
    CHECK(parseFails("1 + "));
    CHECK(parseFails("(1 + 2"));
    CHECK(parseFails("1 2"));
    CHECK(parseFails("WHEN"));
    CHECK(!parseFails("WHEN 1 Call FORCE"));
    // chaîne très longue : pas de débordement de pile à l'évaluation
    std::string longChain;
    for (int i = 0; i < 3000; ++i) longChain += "WHEN a = " + std::to_string(i + 100) + " Call FORCE\n";
    Mock lc; lc.sym["a"] = 2999 + 100; lc.sym["Call"] = 42;
    CHECK(near(eval(longChain, lc, "f$flop"), 42));
}

static void testHandLists() {
    auto parse = [](const std::string& body, HandList& h, std::string& err) { return h.parse(body, err); };
    std::string err;
    HandList h;
    CHECK(parse("AA KK AKs, QJo", h, err));
    CHECK(h.isOnListNoRandom(14, 14, false));
    CHECK(h.isOnListNoRandom(14, 13, true));        // AKs
    CHECK(!h.isOnListNoRandom(14, 13, false));      // AKo absent
    CHECK(h.isOnListNoRandom(11, 12, false));       // QJo, ordre des rangs indifférent
    CHECK(h.nHands() == 4);
    HandList g;
    CHECK(parse("QQ-99", g, err));
    CHECK(g.isOnListNoRandom(10, 10, false) && g.isOnListNoRandom(12, 12, false) && !g.isOnListNoRandom(8, 8, false) && !g.isOnListNoRandom(13, 13, false));
    HandList p;
    CHECK(parse("TT+", p, err));
    CHECK(p.isOnListNoRandom(14, 14, false) && p.isOnListNoRandom(10, 10, false) && !p.isOnListNoRandom(9, 9, false));
    HandList s;
    CHECK(parse("A2s+", s, err));
    CHECK(s.isOnListNoRandom(14, 5, true) && s.isOnListNoRandom(14, 13, true) && !s.isOnListNoRandom(14, 5, false) && s.nHands() == 12);
    HandList t;
    CHECK(parse("T7s-T3s", t, err));
    CHECK(t.isOnListNoRandom(10, 7, true) && t.isOnListNoRandom(10, 3, true) && t.isOnListNoRandom(10, 5, true) && !t.isOnListNoRandom(10, 2, true) && t.nHands() == 5);
    HandList c;
    CHECK(parse("98s-54s", c, err));                // connecteurs
    CHECK(c.isOnListNoRandom(9, 8, true) && c.isOnListNoRandom(5, 4, true) && c.isOnListNoRandom(7, 6, true) && c.nHands() == 5);
    HandList w;
    CHECK(parse("AA:100 KK:0 QQ:50", w, err));
    CHECK(w.weightOf(14, 14, false) == 100 && w.weightOf(13, 13, false) == 0 && w.weightOf(12, 12, false) == 50);
    Rng rng(1);
    int aa = 0, kk = 0, qq = 0;
    for (int i = 0; i < 20000; ++i) { aa += w.isOnList(14, 14, false, rng); kk += w.isOnList(13, 13, false, rng); qq += w.isOnList(12, 12, false, rng); }
    CHECK(aa == 20000);
    CHECK(kk > 100 && kk < 300);                    // poids 0 : rand()%101 == 0 une fois sur 101, comme OpenHoldem
    CHECK(qq > 9500 && qq < 10500);                 // poids 50 : ~51/101
    // erreurs (comme OpenHoldem)
    HandList e;
    CHECK(!parse("AK", e, err));                    // ancien format
    CHECK(!parse("AXs", e, err));
    CHECK(!parse("AA:101", e, err));
    CHECK(!parse("AKs+-", e, err));
}

static void testFormulaCollection() {
    FormulaCollection fc;
    fc.addText("##notes##\nhello\n##list01##\nAA KK\n##f$test##\n1+1\n##list02##\nAK\n", "x", false);
    fc.parseAll();
    CHECK(fc.has("f$test") && fc.get("f$test")->error.empty());
    CHECK(fc.get("list01")->error.empty() && fc.get("list01")->hands->nHands() == 2);
    CHECK(!fc.get("list02")->error.empty());
    CHECK(fc.get("notes")->tree == nullptr);
}

static void testShanky() {
    const std::string src =
        "MaxSessionHands = 10000\ncustom\nlists\nNEW LIST 001\n  AA KK\n  AK\nEND LIST\n\n"
        "preflop\nWhen hand = AKs Raise 90% force\nWhen islist001 and BetSize = 3 Call\nWhen hand = A suited Bet force\n"
        "flop\nWhen BotsLastAction = Raise User_Aggr\nWhen User_Aggr Call force\n";
    CHECK(isShanky(src));
    CHECK(!isShanky("##f$preflop##\nWHEN 1 Call FORCE"));
    std::string ohf = shankyToOhf(src);
    CHECK(ohf.find("##list001##") != std::string::npos);
    CHECK(ohf.find("hand$AKSuited") != std::string::npos);
    CHECK(ohf.find("RaiseBy 90% FORCE") != std::string::npos);
    CHECK(ohf.find("Set User_Aggr") != std::string::npos);
    FormulaCollection fc;
    fc.addText(ohf, "x", false);
    fc.parseAll();
    for (auto& e : fc.errors(true)) std::printf("  erreur inattendue %s : %s\n", e.first.c_str(), e.second.c_str());
    CHECK(fc.errors(true).empty());
    CHECK(fc.get("list001")->hands->nHands() == 4);   // AA KK + AKs AKo
    CHECK(fc.isOpenPPLProfile());
    // symboles utilisateur Shanky : « NEW SYMBOL x ... END SYMBOL » devient une fonction, même au milieu d'une section
    const std::string sym =
        "NEW SYMBOL IP\n  WHEN (Position = Last) RETURN 1 FORCE\n\tWHEN Others RETURN 0 FORCE\nEND SYMBOL\n"
        "preflop\nWhen islist001 Raise force\nNEW SYMBOL Mid\n  WHEN Others RETURN 7 FORCE\nEND SYMBOL\nWhen IP Call force\n"
        "flop\nWhen Mid = 7 Fold force\n";
    std::string ohf2 = shankyToOhf(sym);
    FormulaCollection f2;
    f2.addText(ohf2, "x", false);
    f2.parseAll();
    CHECK(f2.has("IP") && f2.has("Mid") && f2.has("f$preflop") && f2.has("f$flop"));
    CHECK(f2.errors(true).empty());
    CHECK(f2.get("f$preflop")->text.find("Call") != std::string::npos);     // la suite de la section n'est pas happée par le symbole
    CHECK(f2.get("f$preflop")->text.find("RETURN 7") == std::string::npos);
}

int main() {
    testOperators();
    testSymbolsAndPpl();
    testHandLists();
    testFormulaCollection();
    testShanky();
    if (failures) { std::printf("%d échec(s)\n", failures); return 1; }
    std::puts("ohlang : OK");
    return 0;
}
