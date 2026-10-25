#include "pokersim/ohsymbols.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include "pokersim/handrank.hpp"
#include "pokersim/pathutil.hpp"

namespace ps::oh {
namespace {
std::string lowerS(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
constexpr int kOhSuit[4] = {2, 1, 0, 3};            // c,d,h,s -> clubs=2 diamonds=1 hearts=0 spades=3
const int kPrimes[15] = {0, 0, 2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41};
int rankChar(char u) { const char* r = "23456789TJQKA"; const char* p = std::strchr(r, std::toupper(static_cast<unsigned char>(u))); return (p && *p) ? static_cast<int>(p - r) + 2 : 0; }

int rankbitsOf(const std::vector<Card>& cards) {
    int b = 0;
    for (Card c : cards) { int r = rankOf(c); b |= 1 << (r + 2); if (r == 12) b |= 2; }
    return b;
}
std::pair<int, int> straightStats(int strbits) {
    int ns = 0, nf = 5;
    for (int i = 10; i >= 1; --i) {
        int w = strbits >> i;
        if ((w & 0x1f) == 0x1f) ns = std::max(ns, 5);
        else if ((w & 0x1e) == 0x1e || (w & 0x0f) == 0x0f) ns = std::max(ns, 4);
        else if ((w & 0x1c) == 0x1c || (w & 0x0e) == 0x0e || (w & 0x07) == 0x07) ns = std::max(ns, 3);
        else if ((w & 0x18) == 0x18 || (w & 0x0c) == 0x0c || (w & 0x06) == 0x06 || (w & 3) == 3) ns = std::max(ns, 2);
        else ns = std::max(ns, 1);
        nf = std::min(nf, 5 - std::popcount(static_cast<unsigned>(w & 0x1f)));
    }
    return {ns, nf};
}
// (valeur OH de la couleur dominante, nombre de cartes, indice c/d/h/s) ; ordre OpenHoldem : clubs, diamonds, hearts, spades (>=)
std::array<int, 3> tsuitInfo(const std::vector<Card>& cards) {
    int cnt[4] = {0, 0, 0, 0};
    for (Card c : cards) ++cnt[suitOf(c)];
    int nc = cnt[0], nd = cnt[1], nh = cnt[2], ns = cnt[3];
    if (nc >= nd && nc >= nh && nc >= ns) return {kOhSuit[0], nc, 0};
    if (nd >= nh && nd >= ns) return {kOhSuit[1], nd, 1};
    if (nh >= ns) return {kOhSuit[2], nh, 2};
    return {kOhSuit[3], ns, 3};
}
int srankbitsOf(const std::vector<Card>& cards, int suitIdx) {
    int b = 0;
    for (Card c : cards) if (suitOf(c) == suitIdx) { int r = rankOf(c); b |= 1 << (r + 2); if (r == 12) b |= 2; }
    return b;
}
uint64_t maskOf(const std::vector<Card>& a, const std::vector<Card>& b = {}) {
    uint64_t m = 0;
    for (Card c : a) m |= peBit(toPe(c));
    for (Card c : b) m |= peBit(toPe(c));
    return m;
}
}  // namespace

// ============================================================ construction / cycle de vie
SymbolContext::SymbolContext(std::shared_ptr<const FormulaCollection> f, const SimConfig& c, uint64_t seed, std::string n)
    : fc(std::move(f)), cfg(c), rng(seed), name(std::move(n)) {}

void SymbolContext::bind(Game& g, int ch) {
    game = &g; t = &g.t; chair = ch; me = &g.t.seats[ch];
    hole.clear(); board.clear();
    for (Card c : me->cards) if (c != kNoCard) hole.push_back(c);
    for (Card c : t->board) board.push_back(c);
    fnCache_.clear();
    pvOk_ = csOk_ = noutsOk_ = prwOk_ = nhOk_ = hrOk_ = vsOk_ = false;
}

void SymbolContext::newGame(Game& g) {
    game = &g;
    memory.clear(); uservars.clear(); hist.clear();
    bind(g, 0);
    evaluateNamed("f$ini_function_on_startup");
    evaluateNamed("f$ini_function_on_connection");
}

void SymbolContext::newHand(Game& g, int ch) {
    bind(g, ch);
    uservars.clear(); hist.clear();
    for (int i = 0; i < 6; ++i) { phandval[i] = chandval[i] = 0; roundRand[i] = 0; }
    handRand = rng.uniform();
    roundsSeen = 0;
    evaluateNamed("UpdateMemorySymbolsOnHandReset");
    evaluateNamed("f$ini_function_on_handreset");
}

void SymbolContext::prepare(Game& g, int ch) {
    bind(g, ch);
    int rnd = t->betround;
    if (!(roundsSeen & (1 << rnd))) {
        roundsSeen |= 1 << rnd;
        roundRand[rnd] = rng.uniform();
        evaluateNamed("UpdateMemorySymbolsOnNewRound");
        evaluateNamed("f$ini_function_on_new_round");
        fnCache_.clear();
        pvOk_ = csOk_ = noutsOk_ = prwOk_ = nhOk_ = hrOk_ = vsOk_ = false;
    }
    beatRand = rng.uniform();
    evaluateNamed("f$ini_function_on_my_turn");
    evaluateNamed("f$ini_function_on_heartbeat");
    fnCache_.clear();
    pvOk_ = csOk_ = noutsOk_ = prwOk_ = nhOk_ = hrOk_ = vsOk_ = false;
    snapshotHistory();
}

void SymbolContext::afterAction(Game& g, int ch) {
    bind(g, ch);
    evaluateNamed("UpdateMemorySymbolsAfterAutoplayerAction");
    fnCache_.clear();
}

bool SymbolContext::usesHiPrwin() {
    if (!hiPrwinKnown_) {
        hiPrwinKnown_ = true; hiPrwin_ = false;
        for (auto& kv : fc->functions) if (kv.second->text.find("hi_pr") != std::string::npos) { hiPrwin_ = true; break; }
    }
    return hiPrwin_;
}

void SymbolContext::snapshotHistory() {
    int r = t->betround;
    auto snap = [&](const std::string& nm) {
        Native f = resolveNative(nm);
        hist[{nm, r}] = f ? f(*this) : 0.0;
    };
    snap("balance");
    for (int i = 0; i < 10; ++i) snap("balance" + std::to_string(i));
    for (int i = 0; i < 10; ++i) snap("stack" + std::to_string(i));
    if (usesHiPrwin()) {
        PrwinResult p = prwinTriplet();
        hist[{"prwin", r}] = p.win; hist[{"prtie", r}] = p.tie; hist[{"prlos", r}] = p.lose;
    }
    const PokerVal& v = pokervalAll();
    phandval[r] = static_cast<uint32_t>(v.pv) & 0xff000000u;
    chandval[r] = static_cast<uint32_t>(v.pvCommon) & 0xff000000u;
}

// ============================================================ fonctions & symboles
bool SymbolContext::hasFunction(int id) {
    std::string nm = names().name(id);
    return fc->functions.count(nm) || fc->lowerIndex.count(lowerS(nm));
}
double SymbolContext::setVariable(int id) { uservars.insert(lowerS(names().name(id))); return 0.0; }

SymbolContext::Binding& SymbolContext::binding(int id) {
    if (id >= static_cast<int>(bindings_.size())) bindings_.resize(static_cast<size_t>(id) + 64);
    Binding& b = bindings_[id];
    if (b.kind != Binding::None) return b;
    std::string nm = names().name(id);
    auto it = fc->functions.find(nm);
    if (it != fc->functions.end()) { b.kind = Binding::Func; b.fn = it->second; return b; }
    // natif d'abord : « call » (montant à payer) ne doit pas être confondu avec la constante OpenPPL « Call »
    Native nat = resolveNative(nm);
    if (nat) { b.kind = Binding::Nat; b.native = std::move(nat); return b; }
    auto li = fc->lowerIndex.find(lowerS(nm));
    if (li != fc->lowerIndex.end()) { b.kind = Binding::Func; b.fn = fc->functions.at(li->second); return b; }
    b.kind = Binding::Unknown;
    return b;
}

double SymbolContext::symbol(int id) {
    Binding& b = binding(id);
    switch (b.kind) {
        case Binding::Func: return callFunction(*b.fn);
        case Binding::Nat: return b.native(*this);
        default: ++unknown_[names().name(id)]; return 0.0;
    }
}

double SymbolContext::callFunction(OHFunction& f) {
    int key = names().intern(f.name);
    auto it = fnCache_.find(key);
    if (it != fnCache_.end()) return it->second;
    if (f.isList) return lowerS(f.name).rfind("list", 0) == 0 ? isList(f.name.substr(4)) : 0.0;
    if (!f.tree) { fnCache_[key] = 0.0; return 0.0; }
    if (active_.count(key) || depth_ > 200) return 0.0;
    active_.insert(key); ++depth_;
    double v = f.tree->ev(*this);
    active_.erase(key); --depth_;
    fnCache_[key] = v;
    return v;
}

double SymbolContext::evaluateNamed(const std::string& nm, bool* found) {
    auto f = fc->get(nm);
    if (found) *found = (f != nullptr);
    if (!f) return 0.0;
    if (f->text.find_first_not_of(" \t\r\n") == std::string::npos) return 0.0;
    return callFunction(*f);
}

// ============================================================ état dérivé
double SymbolContext::callAmount() const {
    double call = largestBet() - me->bet;
    if (call > me->stack && me->stack > 0) call = me->stack;
    return std::max(call, 0.0);
}
std::vector<const Action*> SymbolContext::roundActions(int rnd) const {
    if (!rnd) rnd = t->betround;
    std::vector<const Action*> out;
    for (auto& a : t->actions)
        if (a.round == rnd && a.kind != ActKind::SmallBlind && a.kind != ActKind::BigBlind && a.kind != ActKind::Ante) out.push_back(&a);
    return out;
}
std::vector<const Action*> SymbolContext::aggr(int rnd) const {
    std::vector<const Action*> out;
    for (auto* a : roundActions(rnd)) if (a->kind == ActKind::Bet || a->kind == ActKind::Raise) out.push_back(a);
    return out;
}
int SymbolContext::dealpos(int c) const {
    int n = 0, d = t->dealer;
    for (int i = 1; i <= t->nchairs; ++i) {
        int cc = (d + i) % t->nchairs;
        if (t->seats[cc].dealt) ++n;
        if (cc == c) return n;
    }
    return 0;
}
int SymbolContext::betpos(int c) const {
    int n = 0, d = t->dealer;
    for (int i = 1; i <= t->nchairs; ++i) {
        int cc = (d + i) % t->nchairs;
        if (t->seats[cc].inHand) ++n;
        if (cc == c) return n;
    }
    return 0;
}
int SymbolContext::chairByDealpos(int p) const {
    for (int c = 0; c < t->nchairs; ++c) if (t->seats[c].dealt && dealpos(c) == p) return c;
    return -1;
}
int SymbolContext::logicalChair(int offset) const {
    int pos = static_cast<int>(dealt().size()) - offset;
    return pos <= 2 ? -1 : chairByDealpos(pos);
}
std::vector<int> SymbolContext::playing() const { std::vector<int> v; for (auto& s : t->seats) if (s.inHand) v.push_back(s.chair); return v; }
std::vector<int> SymbolContext::dealt() const { std::vector<int> v; for (auto& s : t->seats) if (s.dealt) v.push_back(s.chair); return v; }
int SymbolContext::raisbits(int rnd) const { int b = 0; for (auto* a : aggr(rnd)) b |= 1 << a->chair; return b; }
int SymbolContext::callbits(int rnd) const { int b = 0; for (auto* a : roundActions(rnd)) if (a->kind == ActKind::Call) b |= 1 << a->chair; return b; }
int SymbolContext::foldbits(int rnd) const { int b = 0; for (auto* a : roundActions(rnd)) if (a->kind == ActKind::Fold) b |= 1 << a->chair; return b; }
int SymbolContext::raischair() const {
    int r = -1;
    for (auto* a : aggr()) if (a->chair != chair) r = a->chair;
    return r;
}
int SymbolContext::nTrulyRaising() const {
    auto acts = roundActions();
    int lastMine = -1;
    for (size_t i = 0; i < acts.size(); ++i) if (acts[i]->chair == chair) lastMine = static_cast<int>(i);
    int n = 0;
    for (size_t i = lastMine + 1; i < acts.size(); ++i) if ((acts[i]->kind == ActKind::Bet || acts[i]->kind == ActKind::Raise) && acts[i]->chair != chair) ++n;
    return n;
}
int SymbolContext::nCalling() const {
    auto acts = roundActions();
    int lastAggr = -1;
    for (size_t i = 0; i < acts.size(); ++i) if (acts[i]->kind == ActKind::Bet || acts[i]->kind == ActKind::Raise) lastAggr = static_cast<int>(i);
    std::set<int> s;
    for (size_t i = lastAggr + 1; i < acts.size(); ++i) if (acts[i]->kind == ActKind::Call && acts[i]->chair != chair) s.insert(acts[i]->chair);
    return static_cast<int>(s.size());
}
double SymbolContext::did(const char* kind, int rnd) const {
    if (rnd < 1 || rnd > 4) return 0.0;
    std::string k = kind;
    int code = k == "chec" ? PA_CHECK : k == "call" ? PA_CALL : k == "rais" ? PA_RAISE : k == "swag" ? PA_BETSIZE : k == "fold" ? PA_FOLD : PA_ALLIN;
    for (int c : me->acts[rnd]) if (c == code) return 1.0;
    return 0.0;
}
double SymbolContext::nplayersround(int rnd) const {
    if (rnd > t->betround) return 0.0;
    int n = static_cast<int>(dealt().size());
    for (int r = 1; r < rnd; ++r) for (auto& a : t->actions) if (a.round == r && a.kind == ActKind::Fold) --n;
    return n;
}
double SymbolContext::nbetsround(int rnd) const {
    if (rnd > t->betround) return 0.0;
    int mx = 0;
    for (auto& a : t->actions) if (a.round == rnd) mx = std::max(mx, a.to);
    return mx / unit();
}
int SymbolContext::blindBits() const {
    int b = 0;
    if (t->sbChair >= 0) b |= 1 << t->sbChair;
    if (t->bbChair >= 0) b |= 1 << t->bbChair;
    return b;
}
double SymbolContext::balanceSorted(int rank) const {
    std::vector<int> v;
    for (auto& s : t->seats) v.push_back(s.bet + s.stack);
    std::sort(v.begin(), v.end(), std::greater<int>());
    v.resize(v.size() + 10, 0);
    return rank >= 0 && rank < static_cast<int>(v.size()) ? v[rank] : 0.0;
}
double SymbolContext::stackSorted(int rank) const {
    std::vector<int> v;
    for (auto& s : t->seats) v.push_back(s.inHand || s.dealt ? s.stack : 0);
    std::sort(v.begin(), v.end(), std::greater<int>());
    v.resize(v.size() + 10, 0);
    return rank >= 0 && rank < static_cast<int>(v.size()) ? v[rank] : 0.0;
}
std::vector<int> SymbolContext::aliveStacks() const {
    std::vector<int> v;
    for (auto& s : t->seats) if (s.stack + s.bet > 0) v.push_back(s.stack + s.bet);
    std::sort(v.begin(), v.end(), std::greater<int>());
    return v;
}

// ============================================================ cartes
const SymbolContext::PokerVal& SymbolContext::pokervalAll() {
    if (pvOk_) return pv_;
    pvOk_ = true;
    uint64_t mAll = maskOf(hole, board), mHole = maskOf(hole), mBoard = maskOf(board);
    int nAll = static_cast<int>(hole.size() + board.size());
    int c0 = hole.size() > 0 ? toPe(hole[0]) : kNoCardOH, c1 = hole.size() > 1 ? toPe(hole[1]) : kNoCardOH, dummy = 0;
    pv_.pv = calcPokerval(handValN(mAll, nAll), nAll, &pv_.bits, c0, c1, mAll);
    pv_.pvPlayer = calcPokerval(handValN(mHole, static_cast<int>(hole.size())), static_cast<int>(hole.size()), &dummy, kNoCardOH, kNoCardOH, mHole);
    pv_.pvCommon = calcPokerval(handValN(mBoard, static_cast<int>(board.size())), static_cast<int>(board.size()), &dummy, kNoCardOH, kNoCardOH, mBoard);
    return pv_;
}

std::pair<int, std::array<int, 5>> SymbolContext::handTypeInfo() {
    std::array<int, 5> r{0, 0, 0, 0, 0};
    int n = static_cast<int>(hole.size() + board.size());
    if (!n) return {0, r};
    uint32_t hv = handValN(maskOf(hole, board), n);
    for (int i = 0; i < 5; ++i) r[i] = hvCard(hv, i);
    return {hvType(hv), r};
}

const SymbolContext::CardStats& SymbolContext::cardStats() {
    if (csOk_) return cs_;
    csOk_ = true;
    std::vector<Card> pl = hole;
    pl.insert(pl.end(), board.begin(), board.end());
    const std::vector<Card>& cm = board;
    CardStats d;
    auto a = tsuitInfo(pl); d.tsuit = a[0]; d.nsuited = a[1];
    auto b = tsuitInfo(cm); d.tsuitcommon = b[0]; d.nsuitedcommon = b[1];
    for (int which = 0; which < 2; ++which) {
        const std::vector<Card>& cards = which == 0 ? pl : cm;
        int cnt[13] = {0}, mx = 0, tr = 0;
        for (Card c : cards) ++cnt[rankOf(c)];
        for (int i = 12; i >= 0; --i) if (cnt[i] > mx && cnt[i] > 0) { mx = cnt[i]; tr = i + 2; }
        (which == 0 ? d.nranked : d.nrankedcommon) = mx;
        (which == 0 ? d.trank : d.trankcommon) = tr;
    }
    auto s1 = straightStats(rankbitsOf(pl)); d.nstraight = s1.first; d.nstraightfill = s1.second;
    if (!board.empty()) { auto s2 = straightStats(rankbitsOf(cm)); d.nstraightcommon = s2.first; d.nstraightfillcommon = s2.second; }
    else { d.nstraightcommon = 0; d.nstraightfillcommon = 5; }
    int nsf = 0, nff = 5, nsfc = 0, nffc = 5;
    for (int s = 0; s < 4; ++s) {
        std::vector<Card> ps, pc;
        for (Card c : pl) if (suitOf(c) == s) ps.push_back(c);
        for (Card c : cm) if (suitOf(c) == s) pc.push_back(c);
        auto x = straightStats(rankbitsOf(ps)); nsf = std::max(nsf, x.first); nff = std::min(nff, x.second);
        auto y = straightStats(rankbitsOf(pc)); nsfc = std::max(nsfc, y.first); nffc = std::min(nffc, y.second);
    }
    d.nstraightflush = nsf; d.nstraightflushfill = nff; d.nstraightflushcommon = nsfc; d.nstraightflushfillcommon = nffc;
    cs_ = d;
    return cs_;
}

std::pair<int, int> SymbolContext::noutsBetter() {
    if (noutsOk_) return nouts_;
    noutsOk_ = true;
    uint64_t known = maskOf(hole, board);
    int basePv = pokervalAll().pv;
    int nAll = static_cast<int>(hole.size() + board.size());
    int baseT = nAll ? hvType(handValN(known, nAll)) : 0;
    int nouts = 0, better = 0;
    for (int c = 0; c < 52; ++c) {
        Card card = static_cast<Card>(c);
        uint64_t bit = peBit(toPe(card));
        if (known & bit) continue;
        uint64_t mAll = known | bit, mCm = maskOf(board) | bit;
        int n1 = nAll + 1, n2 = static_cast<int>(board.size()) + 1, dummy = 0;
        uint32_t hv1 = handValN(mAll, n1), hvc = handValN(mCm, n2);
        int pv1 = calcPokerval(hv1, n1, &dummy, kNoCardOH, kNoCardOH, mAll);
        int pvc1 = calcPokerval(hvc, n2, &dummy, kNoCardOH, kNoCardOH, mCm);
        if (t->betround < RIVER && hvType(hv1) > baseT && pv1 > basePv && hvType(hv1) > hvType(hvc)) ++nouts;
        if (pvc1 > basePv) ++better;
    }
    nouts_ = {nouts, better};
    return nouts_;
}

double SymbolContext::isHand(const std::string& name) {
    std::string s = name.substr(1);
    int ranks[2] = {0, 0}, nr = 0, suited = 0;
    for (char ch : s) {
        char u = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        if (rankChar(u)) { if (nr < 2) ranks[nr] = rankChar(u); ++nr; }
        else if (u == 'X') { if (nr < 2) ranks[nr] = -1; ++nr; }
        else if (u == 'S') suited = 1;
        else if (u == 'O') suited = 2;
        else return 0.0;
    }
    while (nr < 2) ranks[nr++] = 0;
    if (hole.size() < 2) return 0.0;
    if (ranks[1] > ranks[0]) std::swap(ranks[0], ranks[1]);
    int pr[2] = {rankOf(hole[0]) + 2, rankOf(hole[1]) + 2};
    if (pr[0] < pr[1]) std::swap(pr[0], pr[1]);
    bool plsuited = suitOf(hole[0]) == suitOf(hole[1]);
    if (suited == 1 && !plsuited) return 0.0;
    if (suited == 2 && plsuited) return 0.0;
    if (ranks[0] == -1 && ranks[1] == -1) return 1.0;
    if (ranks[1] == 0 || ranks[1] == -1) return (ranks[0] == pr[0] || ranks[0] == pr[1]) ? 1.0 : 0.0;
    if (ranks[0] != -1 && ranks[0] != pr[0]) return 0.0;
    if (ranks[1] != -1 && ranks[1] != pr[1]) return 0.0;
    return 1.0;
}

double SymbolContext::handBoardExpr(const std::string& nm) {
    bool isHandSym = nm.rfind("hand$", 0) == 0;
    std::string expr = isHandSym ? nm.substr(5) : nm.substr(6);
    const std::vector<Card>& cards = isHandSym ? hole : board;
    bool suited = false;
    if (lowerS(expr).size() >= 6 && lowerS(expr).compare(expr.size() - 6, 6, "suited") == 0) { suited = true; expr = expr.substr(0, expr.size() - 6); }
    long long avail = 1;
    for (Card c : cards) avail *= kPrimes[rankOf(c) + 2];
    long long want = 1;
    std::vector<int> wanted;
    for (char ch : expr) { int r = rankChar(ch); if (r) { want *= kPrimes[r]; wanted.push_back(r); } }
    if (want == 0 || avail % want != 0) return 0.0;
    if (suited) {
        if (isHandSym) { if (hole.size() < 2 || suitOf(hole[0]) != suitOf(hole[1])) return 0.0; }
        else {
            int tsci = tsuitInfo(board)[2];
            int have = srankbitsOf(board, tsci), need = 0;
            for (int r : wanted) { need |= 1 << r; if (r == 14) need |= 2; }
            if ((need & have) != need) return 0.0;
        }
    }
    for (size_t i = 0; i + 1 < expr.size(); ++i) {
        if (rankChar(expr[i]) && std::strchr("cdhs", std::tolower(static_cast<unsigned char>(expr[i + 1]))) && expr[i + 1]) {
            char buf[3] = {expr[i], expr[i + 1], 0};
            Card cc = cardFromString(buf);
            if (cc == kNoCard) continue;
            if (std::find(cards.begin(), cards.end(), cc) == cards.end()) return 0.0;
        }
    }
    return 1.0;
}

double SymbolContext::cardCode(const std::string& nm) {
    // $$pc0 $$cr3 $$ps1 ... : joueur/commun, carte/rang/couleur, indice
    bool player = nm[2] == 'p';
    char kind = nm[3];
    int i = nm[4] - '0';
    const std::vector<Card>& cards = player ? hole : board;
    if (i >= static_cast<int>(cards.size())) return kind != 'r' ? -1.0 : 0.0;
    Card c = cards[i];
    if (kind == 'r') return rankOf(c) + 2;
    if (kind == 's') return kOhSuit[suitOf(c)];
    return kOhSuit[suitOf(c)] * 13 + rankOf(c);
}

double SymbolContext::isList(const std::string& num) {
    auto it = fc->functions.find("list" + num);
    if (it == fc->functions.end()) return 0.0;
    const OHFunction& f = *it->second;
    if (hole.size() < 2 || !f.hands) return 0.0;
    return f.hands->isOnList(rankOf(hole[0]) + 2, rankOf(hole[1]) + 2, suitOf(hole[0]) == suitOf(hole[1]), rng) ? 1.0 : 0.0;
}

// ============================================================ prwin & co (original OpenHoldem)
int SymbolContext::nopponentsForPrwin() {
    int v = static_cast<int>(evaluateNamed("f$prwin_number_of_opponents"));
    return std::max(1, std::min(v, kMaxOpponents));
}

PrwinResult SymbolContext::prwinTriplet() {
    if (prwOk_) return prw_;
    prwOk_ = true;
    prw_ = PrwinResult{};
    if (hole.size() < 2) return prw_;
    PrwinInput in;
    in.plCards = maskOf(hole); in.comCards = maskOf(board); in.nCom = static_cast<int>(board.size());
    in.iterations = static_cast<int>(evaluateNamed("f$prwin_number_of_iterations"));
    if (cfg.prwinMaxIterations > 0) in.iterations = std::min(in.iterations, cfg.prwinMaxIterations);
    in.nOpponents = nopponentsForPrwin();
    in.willplay = static_cast<int>(evaluateNamed("f$prwin_willplay"));
    in.wontplay = static_cast<int>(evaluateNamed("f$prwin_wontplay"));
    in.topclip = static_cast<int>(evaluateNamed("f$prwin_topclip"));
    in.mustplay = static_cast<int>(evaluateNamed("f$prwin_mustplay"));
    in.userchair = chair; in.preflop = board.empty();
    prw_ = computePrwin(in, rng);
    return prw_;
}

NhandsResult SymbolContext::nhands() {
    if (nhOk_) return nh_;
    nhOk_ = true;
    nh_ = hole.size() >= 2 ? computeNhands(maskOf(hole), maskOf(board), static_cast<int>(hole.size()), static_cast<int>(board.size()), nopponentsForPrwin()) : NhandsResult{};
    return nh_;
}

HandrankValues SymbolContext::handrank() {
    if (hrOk_) return hr_;
    hrOk_ = true;
    hr_ = hole.size() >= 2 ? computeHandrank(toPe(hole[0]), toPe(hole[1]), nopponentsForPrwin()) : HandrankValues{};
    return hr_;
}

// ============================================================ vs$ (CSymbolEngineVersus)
const VersusResult& SymbolContext::versus() {
    if (!vs_) vs_ = std::make_unique<VersusResult>();
    if (vsOk_) return *vs_;
    vsOk_ = true;
    if (hole.size() >= 2) {
        const auto p = playing();
        const int nOpp = static_cast<int>(p.size()) - (std::find(p.begin(), p.end(), chair) != p.end() ? 1 : 0);   // nopponentsplaying
        computeVersus(toPe(hole[0]), toPe(hole[1]), maskOf(board), static_cast<int>(board.size()), nOpp, *vs_);
    } else *vs_ = VersusResult{};
    return *vs_;
}

double SymbolContext::versusList(const std::string& listName, int which) {
    std::shared_ptr<OHFunction> f;
    auto it = fc->functions.find(listName);
    if (it != fc->functions.end()) f = it->second;
    else { auto li = fc->lowerIndex.find(lowerS(listName)); if (li != fc->lowerIndex.end()) f = fc->functions.at(li->second); }
    if (!f || !f->hands) return 0.0;                         // liste inconnue ou invalide : OpenHoldem renvoie 0
    const VersusResult& R = versus();
    if (!R.valid) return 0.0;
    double nw = 0, nt = 0, nl = 0;
    for (int i = 0; i < 51; ++i) for (int j = i + 1; j < 52; ++j) {
        const uint32_t w = R.win[i][j], t = R.tie[i][j], l = R.loss[i][j];
        if (w + t + l == 0) continue;                        // main impossible (cartes connues)
        if (!f->hands->isOnList(i % 13 + 2, j % 13 + 2, i / 13 == j / 13, rng)) continue;
        nw += w; nt += t; nl += l;
    }
    double total = nw + nt + nl;
    if (total == 0) total = 1;
    return (which == 0 ? nw : which == 1 ? nt : nl) / total;
}

// ============================================================ mémoire me_*
double SymbolContext::memorySymbol(const std::string& nm) {
    std::vector<std::string> parts;
    { size_t s = 0; for (;;) { size_t p = nm.find('_', s); if (p == std::string::npos) { parts.push_back(nm.substr(s)); break; } parts.push_back(nm.substr(s, p - s)); s = p + 1; } }
    if (parts.size() < 3) return 0.0;
    const std::string& op = parts[1];
    std::string var = lowerS(parts[2]);
    auto rhs = [&]() -> double {
        size_t i1 = nm.find('_'), i2 = i1 == std::string::npos ? i1 : nm.find('_', i1 + 1), i3 = i2 == std::string::npos ? i2 : nm.find('_', i2 + 1);
        if (i3 == std::string::npos) return 0.0;
        std::string txt = nm.substr(i3 + 1);
        if (txt.empty()) return 0.0;
        if (std::isdigit(static_cast<unsigned char>(txt[0]))) {
            std::replace(txt.begin(), txt.end(), '_', '.');
            char* e = nullptr; double v = std::strtod(txt.c_str(), &e);
            return (e && *e == 0) ? v : 0.0;
        }
        return symbol(names().intern(txt));
    };
    if (op == "st") { memory[var] = rhs(); return 0.0; }
    if (op == "re") { auto it = memory.find(var); return it == memory.end() ? 0.0 : it->second; }
    if (op == "inc") { memory[var] += 1; return 0.0; }
    if (op == "dec") { memory[var] -= 1; return 0.0; }
    if (op == "add") { double v = rhs(); memory[var] += v; return 0.0; }
    if (op == "sub") { double v = rhs(); memory[var] -= v; return 0.0; }
    return 0.0;
}

// ============================================================ chargement des profils
std::shared_ptr<FormulaCollection> loadLibrary(const std::string& libraryDir, const std::string& defaultBotDir) {
    namespace fs = std::filesystem;
    auto fc = std::make_shared<FormulaCollection>();
    std::error_code ec;
    if (fs::is_directory(pathFromUtf8(libraryDir), ec)) {
        std::vector<std::string> files;
        for (auto& e : fs::directory_iterator(pathFromUtf8(libraryDir), ec)) {
            std::string p = pathToUtf8(e.path()), low = lowerS(pathToUtf8(e.path().extension()));
            if (low == ".ohf") files.push_back(p);
        }
        std::sort(files.begin(), files.end());
        for (auto& f : files) fc->addFile(f, true);
    }
    // ordre d'OpenHoldem (CFormulaParser::LoadDefaultBot + bibliothèque personnalisée) : les derniers fichiers redéfinissent les premiers
    for (const char* n : {"DefaultBot.ohf", "Gecko_NL_6Max_FR_BSS.ohf", "Termita_SNG.ohf", "Winngy_PT_Limit.ohf"}) {
        fs::path p = pathFromUtf8(defaultBotDir) / n;
        if (fs::exists(p, ec)) fc->addFile(pathToUtf8(p), true);
    }
    fs::path custom = pathFromUtf8(defaultBotDir).parent_path() / "custom_function_library.ohf";
    if (fs::exists(custom, ec)) fc->addFile(pathToUtf8(custom), true);
    fc->parseAll();
    fc->buildLowerIndex();
    return fc;
}

std::shared_ptr<FormulaCollection> buildProfile(const std::string& path, const FormulaCollection& lib) {
    namespace fs = std::filesystem;
    auto fc = std::make_shared<FormulaCollection>();
    fc->functions = lib.functions;     // fonctions de la bibliothèque : partagées (déjà analysées, immuables)
    FormulaCollection user;
    if (!path.empty() && path != "DefaultBot") user.addFile(path, false);
    std::vector<std::string> added;
    for (auto& kv : user.functions) {
        if (kv.first.rfind("f$debug", 0) == 0 || lowerS(kv.first) == "f$debug") continue;
        fc->functions[kv.first] = kv.second;
        added.push_back(kv.first);
    }
    for (auto& k : added) fc->parseOne(*fc->functions[k]);
    fc->profilePath = path;
    fc->profileName = path.empty() ? "DefaultBot" : pathToUtf8(pathFromUtf8(path).stem());
    fc->buildLowerIndex();
    return fc;
}

bool isOHScript(const FormulaCollection& fc) {
    auto f = fc.get("f$call");
    return f && f->text.find_first_not_of(" \t\r\n") != std::string::npos;
}

}  // namespace ps::oh
