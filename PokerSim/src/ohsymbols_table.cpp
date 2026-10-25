// Table des symboles natifs : portage de SpinGoSimulator/symbols.py (_NATIVE_TABLE, _NATIVE_DIGIT, _NATIVE_REGEX).
#include <algorithm>
#include <bit>
#include <cmath>
#include <regex>
#include "pokersim/evaluator.hpp"
#include "pokersim/handrank.hpp"
#include "pokersim/ohsymbols.hpp"
#include "pokersim/pt.hpp"

namespace ps::oh {
namespace {

using Ctx = SymbolContext;
using Fn = std::function<double(Ctx&)>;
using FnI = std::function<double(Ctx&, int)>;

std::string lowerS(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
double fl(double x) { return x; }
double fb(bool b) { return b ? 1.0 : 0.0; }
int floorMod(int a, int b) { int r = a % b; return r < 0 ? r + b : r; }

constexpr double PV_ONEPAIR = 0x01000000, PV_TWOPAIR = 0x02000000, PV_TRIPS = 0x04000000, PV_STRAIGHT = 0x08000000,
                 PV_FLUSH = 0x10000000, PV_FULLHOUSE = 0x20000000, PV_QUADS = 0x40000000, PV_STFLUSH = 2147483648.0, PV_ROYAL = 2148457658.0;

int rankbitsOf(const std::vector<Card>& cards) {
    int b = 0;
    for (Card c : cards) { int r = rankOf(c); b |= 1 << (r + 2); if (r == 12) b |= 2; }
    return b;
}
int srankbitsOf(const std::vector<Card>& cards, int suitIdx) {
    int b = 0;
    for (Card c : cards) if (suitOf(c) == suitIdx) { int r = rankOf(c); b |= 1 << (r + 2); if (r == 12) b |= 2; }
    return b;
}
int tsuitIdx(const std::vector<Card>& cards) {
    int cnt[4] = {0, 0, 0, 0};
    for (Card c : cards) ++cnt[suitOf(c)];
    if (cnt[0] >= cnt[1] && cnt[0] >= cnt[2] && cnt[0] >= cnt[3]) return 0;
    if (cnt[1] >= cnt[2] && cnt[1] >= cnt[3]) return 1;
    if (cnt[2] >= cnt[3]) return 2;
    return 3;
}
std::vector<Card> concat(const std::vector<Card>& a, const std::vector<Card>& b) { auto v = a; v.insert(v.end(), b.begin(), b.end()); return v; }

int bitsPlayer(Ctx& c) { return rankbitsOf(c.hole); }
int bitsCommon(Ctx& c) { return rankbitsOf(c.board); }
int bitsAll(Ctx& c) { return bitsPlayer(c) | bitsCommon(c); }
int pokerbits(Ctx& c) {
    unsigned pv = static_cast<unsigned>(c.pokervalAll().pv);
    int b = 0;
    for (int sh : {16, 12, 8, 4, 0}) b |= 1 << ((pv >> sh) & 0xf);
    if (b & 0x4000) b |= 2;
    return b;
}
int spokerbits(Ctx& c) {
    unsigned pv = static_cast<unsigned>(c.pokervalAll().pv);
    int tsi = tsuitIdx(concat(c.hole, c.board));
    uint64_t all = 0;
    for (Card x : concat(c.hole, c.board)) all |= 1ULL << x;
    int b = 0;
    for (int sh : {16, 12, 8, 4, 0}) {
        int r = static_cast<int>((pv >> sh) & 0xf);
        if (r >= 2 && ((all >> ((r - 2) * 4 + tsi)) & 1)) b |= 1 << r;
    }
    if (b & 0x4000) b |= 2;
    return b;
}
int sPlayer(Ctx& c) { return srankbitsOf(c.hole, tsuitIdx(concat(c.hole, c.board))); }
int sCommon(Ctx& c) { return srankbitsOf(c.board, tsuitIdx(c.board)); }
int sAll(Ctx& c) { int tsi = tsuitIdx(concat(c.hole, c.board)); return srankbitsOf(c.hole, tsi) | srankbitsOf(c.board, tsi); }
double rankHi(int b) { for (int i = 14; i > 1; --i) if (b >> i & 1) return i; return 0.0; }
double rankLo(int b) { for (int i = 2; i < 15; ++i) if (b >> i & 1) return i; return 0.0; }

bool higherStraightPossible(Ctx& c, int top) {
    if (top == 12) return false;
    int rb = rankbitsOf(c.board), shift = 10;
    for (int k = 14; k > top + 2; --k) {
        if (std::popcount(static_cast<unsigned>((rb >> shift) & 0x1f)) >= 3) return true;
        --shift;
    }
    return false;
}
double pairClass(Ctx& c, int which) {   // 0 hi, 1 mid, 2 lo
    auto [type, R] = c.handTypeInfo();
    int n = static_cast<int>(c.hole.size() + c.board.size());
    if (type != HC_Pair || n < 2) return 0.0;
    int rbc = rankbitsOf(c.board);
    double hiC = rankHi(rbc), loC = rankLo(rbc);
    int top = R[0];
    if (n == 2) return which == 0 ? 1.0 : 0.0;
    if (n >= 5) {
        if (top >= hiC - 2) return which == 0 ? 1.0 : 0.0;
        if (loC - 2 < top && top < hiC - 2) return which == 1 ? 1.0 : 0.0;
        return which == 2 ? 1.0 : 0.0;
    }
    return 0.0;
}
double typeIs(Ctx& c, int code) { return fb(c.handTypeInfo().first == code && !(c.hole.empty() && c.board.empty())); }

const std::unordered_map<std::string, int> kRoundNames = {{"preflop", 1}, {"flop", 2}, {"turn", 3}, {"river", 4}};
const int AC_PRE[11][10] = {{0}, {0}, {2, 6}, {1, 2, 6}, {1, 2, 5, 6}, {1, 2, 5, 5, 6}, {1, 2, 4, 5, 5, 6}, {1, 2, 4, 4, 5, 5, 6}, {1, 2, 3, 4, 4, 5, 5, 6}, {1, 2, 3, 3, 4, 4, 5, 5, 6}, {1, 2, 3, 3, 3, 4, 4, 5, 5, 6}};
const int AC_POST[11][10] = {{0}, {0}, {1, 5}, {1, 4, 5}, {1, 3, 4, 5}, {1, 2, 3, 4, 5}, {1, 2, 3, 4, 4, 5}, {1, 2, 3, 3, 4, 4, 5}, {1, 2, 2, 3, 3, 4, 4, 5}, {1, 2, 2, 3, 3, 3, 4, 4, 5}, {1, 2, 2, 2, 3, 3, 3, 4, 4, 5}};
double acLookup(const int (*tab)[10], int n, int pos) {
    if (n < 2 || n > 10 || pos < 1 || pos > n) return 0.0;
    return tab[n][pos - 1];
}

double headsupChair(Ctx& c) { std::vector<int> o; for (int x : c.playing()) if (x != c.chair) o.push_back(x); return o.size() == 1 ? o[0] : -1.0; }
double firstRaiser(Ctx& c) { auto a = c.aggr(); return a.empty() ? -1.0 : a.front()->chair; }
double firstCaller(Ctx& c) { for (auto* a : c.roundActions()) if (a->kind == ActKind::Call && a->chair != c.chair) return a->chair; return -1.0; }
double lastCaller(Ctx& c) { double r = -1; for (auto* a : c.roundActions()) if (a->kind == ActKind::Call && a->chair != c.chair) r = a->chair; return r; }
double smallstackChair(Ctx& c) { long long best = 1000000000000LL; double res = -1; for (auto& s : c.t->seats) { if (s.chair == c.chair || !s.inHand) continue; if (s.stack < best) { best = s.stack; res = s.chair; } } return res; }
double bigstackChair(Ctx& c) { long long best = -1; double res = -1; for (auto& s : c.t->seats) { if (s.chair == c.chair || !s.inHand) continue; if (s.stack > best) { best = s.stack; res = s.chair; } } return res; }
double myturnbits(Ctx& c) {
    double call = c.largestBet() - c.me->bet;
    int b = 0;
    if (call > 0) b |= 1 | 2; else b |= 4;
    if (c.me->stack + c.me->bet > c.t->currentBet) b |= 8 | 16; else b |= 16;
    return b;
}

using Table = std::unordered_map<std::string, Fn>;
const Table& nativeTable() {
    static const Table tab = [] {
        Table t;
        auto K = [&](const char* k, Fn f) { t[k] = std::move(f); };
        auto cst = [&](const char* k, double v) { t[k] = [v](Ctx&) { return v; }; };
        cst("ace", 14); cst("king", 13); cst("queen", 12); cst("jack", 11); cst("ten", 10);
        cst("preflop", 1); cst("flop", 2); cst("turn", 3); cst("river", 4);
        cst("clubs", 2); cst("diamonds", 1); cst("hearts", 0); cst("spades", 3); cst("true", 1); cst("false", 0);
        // table / blinds
        K("betround", [](Ctx& c) { return fl(c.t->betround); }); K("nchairs", [](Ctx& c) { return fl(c.t->nchairs); });
        K("bblind", [](Ctx& c) { return fl(c.t->bblind); }); K("sblind", [](Ctx& c) { return fl(c.t->sblind); });
        K("ante", [](Ctx& c) { return fl(c.t->ante); }); K("bet", [](Ctx& c) { return c.unit(); });
        K("buyin", [](Ctx& c) { return fl(c.t->buyin); }); K("prizepool", [](Ctx& c) { return fl(c.t->prizepool); });
        K("prizepoolmultiplier", [](Ctx& c) { return fl(c.t->multiplier); });
        cst("isnl", 1); cst("ispl", 0); cst("isfl", 0); cst("lim", 0); cst("isomaha", 0); cst("isrush", 0);
        K("istournament", [](Ctx& c) { return fb(!c.cfg.isCash()); }); K("issng", [](Ctx& c) { return fb(!c.cfg.isCash()); });
        K("isspin", [](Ctx& c) { return fb(!c.cfg.isCash()); });
        for (const char* k : {"ismtt", "isdon", "istripleup", "isshootout", "isfreeroll", "isknockout", "isrebuy", "issatelitte", "isturbo", "issemiturbo",
                              "issuperturbo", "isultraturbo", "issittingout", "isautopost"}) cst(k, 0);
        K("ishyperturbo", [](Ctx& c) { return fb(c.cfg.levelSeconds <= 60 && !c.cfg.isCash()); });
        K("isfinaltable", [](Ctx& c) { return fb(!c.cfg.isCash()); });
        cst("ismyturn", 1); cst("isfinalanswer", 1); cst("issittingin", 1);
        K("myturnbits", myturnbits);
        K("userchair", [](Ctx& c) { return fl(c.chair); }); K("dealerchair", [](Ctx& c) { return fl(c.t->dealer); });
        K("elapsed", [](Ctx& c) { return fl(c.t->clock); });
        K("elapsedhand", [](Ctx& c) { int n = 0; for (auto& a : c.t->actions) if (a.kind != ActKind::SmallBlind && a.kind != ActKind::BigBlind && a.kind != ActKind::Ante) ++n; return c.cfg.handBaseSeconds + c.cfg.actionSeconds * n; });
        for (const char* k : {"elapsedauto", "elapsedstart", "elapsedtoday"}) K(k, [](Ctx& c) { return fl(c.t->clock); });
        K("random", [](Ctx& c) { return c.rng.uniform(); }); K("randomheartbeat", [](Ctx& c) { return c.beatRand; });
        K("randomhand", [](Ctx& c) { return c.handRand; }); K("randomround", [](Ctx& c) { return c.roundRand[c.t->betround]; });
        K("handsplayed", [](Ctx& c) { return fl(c.me->handsPlayed); }); K("nhandsplayed", [](Ctx& c) { return fl(c.me->handsPlayed); });
        // mtt
        K("mtt_number_entrants", [](Ctx& c) { return fl(c.t->nchairs); });
        K("mtt_players_remaining", [](Ctx& c) { return fl(c.aliveStacks().size()); });
        K("mtt_paid_places", [](Ctx& c) { int n = 0; for (double x : c.cfg.payoutShares) n += x > 0; return fl(n); });
        K("mtt_my_rank", [](Ctx& c) { int mine = c.me->stack + c.me->bet, n = 1; for (int x : c.aliveStacks()) n += x > mine; return fl(n); });
        K("mtt_largest_stack", [](Ctx& c) { auto s = c.aliveStacks(); return s.empty() ? 0.0 : fl(s.front()); });
        K("mtt_smallest_stack", [](Ctx& c) { auto s = c.aliveStacks(); return s.empty() ? 0.0 : fl(s.back()); });
        K("mtt_average_stack", [](Ctx& c) { auto s = c.aliveStacks(); double sum = 0; for (int x : s) sum += x; return s.empty() ? 0.0 : sum / s.size(); });
        // chips
        K("pot", [](Ctx& c) { return fl(c.t->pot()); });
        K("potplayer", [](Ctx& c) { int s = 0; for (auto& x : c.t->seats) s += x.bet; return fl(s); });
        K("potcommon", [](Ctx& c) { return fl(c.t->potPrev); });
        K("balance", [](Ctx& c) { return fl(c.me->stack); });
        K("maxbalance", [](Ctx& c) { return fl(c.cfg.startingStack * c.t->nchairs); });
        K("balanceatstartofsession", [](Ctx& c) { return fl(c.cfg.startingStack); });
        K("currentbet", [](Ctx& c) { return fl(c.me->bet); }); K("call", [](Ctx& c) { return c.callAmount(); });
        K("nbetstocall", [](Ctx& c) { return c.callAmount() / c.unit(); });
        K("nbetstorais", [](Ctx& c) { return c.callAmount() / c.unit() + 1.0; });
        K("ncurrentbets", [](Ctx& c) { return c.me->bet / c.unit(); });
        K("ncallbets", [](Ctx& c) { return c.largestBet() / c.unit(); });
        K("nraisbets", [](Ctx& c) { return c.largestBet() / c.unit() + 1.0; });
        // joueurs
        K("nplayersplaying", [](Ctx& c) { return fl(c.playing().size()); });
        for (const char* k : {"nplayersdealt", "nplayersseated", "nplayersactive"}) K(k, [](Ctx& c) { return fl(c.dealt().size()); });
        K("nplayersallin", [](Ctx& c) { int n = 0; for (auto& s : c.t->seats) n += s.inHand && s.allin; return fl(n); });
        K("nopponentsplaying", [](Ctx& c) { auto p = c.playing(); bool in = std::find(p.begin(), p.end(), c.chair) != p.end(); return fl(static_cast<int>(p.size()) - in); });
        for (const char* k : {"nopponentsdealt", "nopponentsseated", "nopponentsactive"})
            K(k, [](Ctx& c) { auto p = c.dealt(); bool in = std::find(p.begin(), p.end(), c.chair) != p.end(); return fl(static_cast<int>(p.size()) - in); });
        K("nopponentsallin", [](Ctx& c) { int n = 0; for (auto& s : c.t->seats) n += s.inHand && s.allin && s.chair != c.chair; return fl(n); });
        K("playersplayingbits", [](Ctx& c) { return fl(Ctx::bitsOf(c.playing())); });
        for (const char* k : {"playersdealtbits", "playersseatedbits", "playersactivebits"}) K(k, [](Ctx& c) { return fl(Ctx::bitsOf(c.dealt())); });
        K("playersallinbits", [](Ctx& c) { std::vector<int> v; for (auto& s : c.t->seats) if (s.inHand && s.allin) v.push_back(s.chair); return fl(Ctx::bitsOf(v)); });
        K("opponentsplayingbits", [](Ctx& c) { return fl(Ctx::bitsOf(c.playing()) & ~(1 << c.chair)); });
        for (const char* k : {"opponentsdealtbits", "opponentsseatedbits", "opponentsactivebits"}) K(k, [](Ctx& c) { return fl(Ctx::bitsOf(c.dealt()) & ~(1 << c.chair)); });
        K("opponentsallinbits", [](Ctx& c) { std::vector<int> v; for (auto& s : c.t->seats) if (s.inHand && s.allin) v.push_back(s.chair); return fl(Ctx::bitsOf(v) & ~(1 << c.chair)); });
        K("maxnplayersdealt", [](Ctx& c) { return fl(c.t->nchairs); });
        K("nopponentsbetting", [](Ctx& c) { int n = 0; for (auto& s : c.t->seats) n += s.chair != c.chair && s.bet > 0; return fl(n); });
        K("nopponentschecking", [](Ctx& c) { int n = 0; for (auto& s : c.t->seats) n += s.chair != c.chair && s.inHand && s.bet == 0; return fl(n); });
        K("nopponentsfolded", [](Ctx& c) { int n = 0; for (auto& s : c.t->seats) n += s.chair != c.chair && s.dealt && !s.inHand; return fl(n); });
        K("nplayerscallshort", [](Ctx& c) { int n = 0; for (auto& s : c.t->seats) n += s.inHand && s.bet < c.largestBet(); return fl(n); });
        K("nopponentstruelyraising", [](Ctx& c) { return fl(c.nTrulyRaising()); });
        K("nopponentscalling", [](Ctx& c) { return fl(c.nCalling()); });
        K("raischair", [](Ctx& c) { return fl(c.raischair()); }); K("lastraiserchair", [](Ctx& c) { return fl(c.raischair()); });
        K("firstraiserchair", firstRaiser); K("firstcallerchair", firstCaller); K("lastcallerchair", lastCaller);
        K("nopponentsblind", [](Ctx& c) { int n = 0; if (c.t->sbChair >= 0 && c.t->sbChair != c.chair) ++n; if (c.t->bbChair >= 0 && c.t->bbChair != c.chair) ++n; return fl(n); });
        K("nplayersblind", [](Ctx& c) { return fl((c.t->sbChair >= 0) + (c.t->bbChair >= 0)); });
        K("playersblindbits", [](Ctx& c) { return fl(c.blindBits()); });
        K("opponentsblindbits", [](Ctx& c) { return fl(c.blindBits() & ~(1 << c.chair)); });
        K("bblindbits", [](Ctx& c) { return c.t->bbChair >= 0 ? fl(1 << c.t->bbChair) : 0.0; });
        // positions
        K("dealposition", [](Ctx& c) { return fl(c.dealpos(c.chair)); }); K("betposition", [](Ctx& c) { return fl(c.betpos(c.chair)); });
        K("callposition", [](Ctx& c) { int n = std::max<int>(1, c.playing().size()); int rc = c.raischair(); return fl(floorMod(c.betpos(c.chair) + n - c.betpos(rc >= 0 ? rc : c.t->dealer), n)); });
        K("dealpositionrais", [](Ctx& c) { int rc = c.raischair(); return rc >= 0 ? fl(c.dealpos(rc)) : 0.0; });
        K("betpositionrais", [](Ctx& c) { int rc = c.raischair(); return rc >= 0 ? fl(c.betpos(rc)) : 0.0; });
        K("nchairsdealtright", [](Ctx& c) { int n = 0; for (int i = 1; i < c.t->nchairs; ++i) { int s = (c.t->dealer + i) % c.t->nchairs; if (c.t->seats[s].dealt && s != c.chair && c.dealpos(s) < c.dealpos(c.chair)) ++n; } return fl(n); });
        K("nchairsdealtleft", [](Ctx& c) { int n = 0; for (auto& s : c.t->seats) n += s.dealt && s.chair != c.chair && c.dealpos(s.chair) > c.dealpos(c.chair); return fl(n); });
        K("buttonchair", [](Ctx& c) { return fl(c.t->dealer); }); K("_dealerchair", [](Ctx& c) { return fl(c.t->dealer); });
        K("br", [](Ctx& c) { return fl(c.t->betround); });
        K("nopponentsraising", [](Ctx& c) { return fl(c.nTrulyRaising()); });
        K("ac_preflop_pos", [](Ctx& c) { return acLookup(AC_PRE, static_cast<int>(c.dealt().size()), c.dealpos(c.chair)); });
        K("ac_prefloprais_pos", [](Ctx& c) { int rc = c.raischair(); return rc >= 0 ? acLookup(AC_PRE, static_cast<int>(c.dealt().size()), c.dealpos(rc)) : 0.0; });
        K("ac_postflop_pos", [](Ctx& c) { return acLookup(AC_POST, static_cast<int>(c.playing().size()), c.betpos(c.chair)); });
        K("ac_first_into_pot", [](Ctx& c) { int pp = 0; for (auto& s : c.t->seats) pp += s.bet; return c.t->betround == 1 ? fb(pp <= c.t->sblind + c.t->bblind) : fb(pp == 0); });
        K("ac_agchair_after", [](Ctx& c) { int rc = c.raischair(); return fb(rc >= 0 && c.betpos(rc) > c.betpos(c.chair)); });
        K("ac_aggressor", [](Ctx& c) { return fl(c.raischair()); });
        K("nplayersround_currentround", [](Ctx& c) { return c.nplayersround(c.t->betround); });
        K("smallblindchair", [](Ctx& c) { return fl(c.t->sbChair); }); K("bigblindchair", [](Ctx& c) { return fl(c.t->bbChair); });
        K("headsupchair", headsupChair);
        const char* logical[] = {"cutoffchair", "mp3chair", "mp2chair", "mp1chair", "ep3chair", "ep2chair", "ep1chair"};
        for (int i = 0; i < 7; ++i) { int off = i + 1; K(logical[i], [off](Ctx& c) { return fl(c.logicalChair(off)); }); }
        K("utgchair", [](Ctx& c) { return c.dealt().size() > 2 ? fl(c.chairByDealpos(3)) : -1.0; });
        cst("missingsmallblind", 0);
        K("smallstackchair", smallstackChair); K("bigstackchair", bigstackChair);
        // historique
        K("prevaction", [](Ctx& c) { return fl(c.me->prevAction); });
        K("didchec", [](Ctx& c) { return c.did("chec", c.t->betround); }); K("didcall", [](Ctx& c) { return c.did("call", c.t->betround); });
        K("didrais", [](Ctx& c) { return c.did("rais", c.t->betround); }); K("didbetsize", [](Ctx& c) { return c.did("swag", c.t->betround); });
        K("didswag", [](Ctx& c) { return c.did("swag", c.t->betround); }); K("didfold", [](Ctx& c) { return c.did("fold", c.t->betround); });
        K("didalli", [](Ctx& c) { return c.did("alli", c.t->betround); });
        K("nplayersround", [](Ctx& c) { return c.nplayersround(c.t->betround); }); K("nbetsround", [](Ctx& c) { return c.nbetsround(c.t->betround); });
        // cartes
        K("ncardsknown", [](Ctx& c) { return fl(c.hole.size() + c.board.size()); });
        K("ncardsunknown", [](Ctx& c) { return 52.0 - (c.hole.size() + c.board.size()); });
        K("ncommoncardsknown", [](Ctx& c) { return fl(c.board.size()); });
        K("ncardsbetter", [](Ctx& c) { return fl(c.noutsBetter().second); }); K("nouts", [](Ctx& c) { return fl(c.noutsBetter().first); });
        K("ispair", [](Ctx& c) { return fb(c.hole.size() == 2 && rankOf(c.hole[0]) == rankOf(c.hole[1])); });
        K("issuited", [](Ctx& c) { return fb(c.hole.size() == 2 && suitOf(c.hole[0]) == suitOf(c.hole[1])); });
        K("isconnector", [](Ctx& c) { return fb(c.hole.size() == 2 && std::abs(rankOf(c.hole[0]) - rankOf(c.hole[1])) == 1); });
        K("pokerval", [](Ctx& c) { return fl(c.pokervalAll().pv); }); K("pcbits", [](Ctx& c) { return fl(c.pokervalAll().bits); });
        K("pokervalplayer", [](Ctx& c) { return fl(c.pokervalAll().pvPlayer); }); K("pokervalcommon", [](Ctx& c) { return fl(c.pokervalAll().pvCommon); });
        K("npcbits", [](Ctx& c) { return fl(std::popcount(static_cast<unsigned>(c.pokervalAll().bits))); });
        cst("hicard", 1); cst("onepair", PV_ONEPAIR); cst("twopair", PV_TWOPAIR); cst("threeofakind", PV_TRIPS); cst("straight", PV_STRAIGHT);
        cst("flush", PV_FLUSH); cst("fullhouse", PV_FULLHOUSE); cst("fourofakind", PV_QUADS); cst("straightflush", PV_STFLUSH); cst("royalflush", PV_ROYAL);
        K("ishicard", [](Ctx& c) { return typeIs(c, HC_HighCard); }); K("isonepair", [](Ctx& c) { return typeIs(c, HC_Pair); });
        K("istwopair", [](Ctx& c) { return typeIs(c, HC_TwoPair); }); K("isthreeofakind", [](Ctx& c) { return typeIs(c, HC_Trips); });
        K("isstraight", [](Ctx& c) { return typeIs(c, HC_Straight); }); K("isflush", [](Ctx& c) { return typeIs(c, HC_Flush); });
        K("isfullhouse", [](Ctx& c) { return typeIs(c, HC_FullHouse); }); K("isfourofakind", [](Ctx& c) { return typeIs(c, HC_Quads); });
        K("isstraightflush", [](Ctx& c) { return typeIs(c, HC_StraightFlush); }); cst("isfiveofakind", 0);
        K("isroyalflush", [](Ctx& c) { auto [ty, R] = c.handTypeInfo(); return fb(ty == HC_StraightFlush && R[0] == 12); });
        K("ishandup", [](Ctx& c) { int r = c.t->betround; if (r <= 1) return 0.0; return fb((static_cast<uint32_t>(c.pokervalAll().pv) & 0xff000000u) > c.phandval[r - 1]); });
        K("ishandupcommon", [](Ctx& c) { int r = c.t->betround; if (r <= 1) return 0.0; return fb((static_cast<uint32_t>(c.pokervalAll().pvCommon) & 0xff000000u) > c.chandval[r - 1]); });
        K("ishipair", [](Ctx& c) { return pairClass(c, 0); }); K("ismidpair", [](Ctx& c) { return pairClass(c, 1); }); K("islopair", [](Ctx& c) { return pairClass(c, 2); });
        K("ishistraight", [](Ctx& c) { auto [ty, R] = c.handTypeInfo(); return fb(ty == HC_Straight && !higherStraightPossible(c, R[0])); });
        K("ishiflush", [](Ctx& c) {
            auto [ty, R] = c.handTypeInfo();
            if (ty != HC_Flush) return 0.0;
            int bits = c.pokervalAll().bits, need[4] = {12, 11, 10, 9};
            for (int i = 0; i < 4; ++i) if (R[i] == need[i] && (bits & (1 << (4 - i)))) return 1.0;
            return 0.0;
        });
        K("rankbits", [](Ctx& c) { return fl(bitsAll(c)); }); K("rankbitsplayer", [](Ctx& c) { return fl(bitsPlayer(c)); });
        K("rankbitscommon", [](Ctx& c) { return fl(bitsCommon(c)); }); K("rankbitspoker", [](Ctx& c) { return fl(pokerbits(c)); });
        K("srankbits", [](Ctx& c) { return fl(sAll(c)); }); K("srankbitsplayer", [](Ctx& c) { return fl(sPlayer(c)); });
        K("srankbitscommon", [](Ctx& c) { return fl(sCommon(c)); }); K("srankbitspoker", [](Ctx& c) { return fl(spokerbits(c)); });
        K("rankhi", [](Ctx& c) { return rankHi(bitsAll(c)); }); K("rankhiplayer", [](Ctx& c) { return rankHi(bitsPlayer(c)); });
        K("rankhicommon", [](Ctx& c) { return rankHi(bitsCommon(c)); }); K("rankhipoker", [](Ctx& c) { return rankHi(pokerbits(c)); });
        K("srankhi", [](Ctx& c) { return rankHi(sAll(c)); }); K("srankhiplayer", [](Ctx& c) { return rankHi(sPlayer(c)); });
        K("srankhicommon", [](Ctx& c) { return rankHi(sCommon(c)); }); K("srankhipoker", [](Ctx& c) { return rankHi(spokerbits(c)); });
        K("ranklo", [](Ctx& c) { return rankLo(bitsAll(c)); }); K("rankloplayer", [](Ctx& c) { return rankLo(bitsPlayer(c)); });
        K("ranklocommon", [](Ctx& c) { return rankLo(bitsCommon(c)); }); K("ranklopoker", [](Ctx& c) { return rankLo(pokerbits(c)); });
        K("sranklo", [](Ctx& c) { return rankLo(sAll(c)); }); K("srankloplayer", [](Ctx& c) { return rankLo(sPlayer(c)); });
        K("sranklocommon", [](Ctx& c) { return rankLo(sCommon(c)); }); K("sranklopoker", [](Ctx& c) { return rankLo(spokerbits(c)); });
        // handrank / prwin / nhands : moteurs fidèles à OpenHoldem
        K("handrank169", [](Ctx& c) { return fl(c.handrank().r169); }); K("handrank2652", [](Ctx& c) { return fl(c.handrank().r2652); });
        K("handrank1326", [](Ctx& c) { return fl(c.handrank().r1326); }); K("handrank1000", [](Ctx& c) { return fl(c.handrank().r1000); });
        K("handrankp", [](Ctx& c) { return c.handrank().rp; });
        K("prwin", [](Ctx& c) { return c.prwinTriplet().win; }); K("prtie", [](Ctx& c) { return c.prwinTriplet().tie; }); K("prlos", [](Ctx& c) { return c.prwinTriplet().lose; });
        K("prwinnow", [](Ctx& c) { return c.nhands().prwinnow; }); K("prlosnow", [](Ctx& c) { return c.nhands().prlosnow; });
        K("nhands", [](Ctx& c) { return fl(c.nhands().total()); }); K("nhandshi", [](Ctx& c) { return fl(c.nhands().hi); });
        K("nhandsti", [](Ctx& c) { return fl(c.nhands().ti); }); K("nhandslo", [](Ctx& c) { return fl(c.nhands().lo); });
        auto cs = [&](const char* k, int Ctx::CardStats::*m) { t[k] = [m](Ctx& c) { return fl(c.cardStats().*m); }; };
        cs("nsuited", &Ctx::CardStats::nsuited); cs("nsuitedcommon", &Ctx::CardStats::nsuitedcommon); cs("tsuit", &Ctx::CardStats::tsuit);
        cs("tsuitcommon", &Ctx::CardStats::tsuitcommon); cs("nranked", &Ctx::CardStats::nranked); cs("nrankedcommon", &Ctx::CardStats::nrankedcommon);
        cs("trank", &Ctx::CardStats::trank); cs("trankcommon", &Ctx::CardStats::trankcommon); cs("nstraight", &Ctx::CardStats::nstraight);
        cs("nstraightcommon", &Ctx::CardStats::nstraightcommon); cs("nstraightfill", &Ctx::CardStats::nstraightfill);
        cs("nstraightfillcommon", &Ctx::CardStats::nstraightfillcommon); cs("nstraightflush", &Ctx::CardStats::nstraightflush);
        cs("nstraightflushcommon", &Ctx::CardStats::nstraightflushcommon); cs("nstraightflushfill", &Ctx::CardStats::nstraightflushfill);
        cs("nstraightflushfillcommon", &Ctx::CardStats::nstraightflushfillcommon);
        return t;
    }();
    return tab;
}

const std::unordered_map<std::string, FnI>& digitTable() {
    static const std::unordered_map<std::string, FnI> d = [] {
        std::unordered_map<std::string, FnI> t;
        t["balance"] = [](Ctx& c, int i) { return i < c.t->nchairs ? fl(c.t->seats[i].stack) : 0.0; };
        t["stack"] = [](Ctx& c, int i) { return c.stackSorted(i); };
        t["currentbet"] = [](Ctx& c, int i) { return i < c.t->nchairs ? fl(c.t->seats[i].bet) : 0.0; };
        t["balance_rank"] = [](Ctx& c, int i) { return c.balanceSorted(i); };
        t["bet"] = [](Ctx& c, int) { return c.unit(); };
        t["raisbits"] = [](Ctx& c, int i) { return fl(c.raisbits(i)); }; t["callbits"] = [](Ctx& c, int i) { return fl(c.callbits(i)); };
        t["foldbits"] = [](Ctx& c, int i) { return fl(c.foldbits(i)); };
        t["lastraised"] = [](Ctx& c, int i) { auto a = c.aggr(i); return a.empty() ? -1.0 : fl(a.back()->chair); };
        t["didchecround"] = [](Ctx& c, int i) { return c.did("chec", i); }; t["didcallround"] = [](Ctx& c, int i) { return c.did("call", i); };
        t["didraisround"] = [](Ctx& c, int i) { return c.did("rais", i); }; t["didbetsizeround"] = [](Ctx& c, int i) { return c.did("swag", i); };
        t["didswaground"] = [](Ctx& c, int i) { return c.did("swag", i); }; t["didfoldround"] = [](Ctx& c, int i) { return c.did("fold", i); };
        t["didalliround"] = [](Ctx& c, int i) { return c.did("alli", i); };
        t["nplayersround"] = [](Ctx& c, int i) { return c.nplayersround(i); }; t["nbetsround"] = [](Ctx& c, int i) { return c.nbetsround(i); };
        static const int inv[4] = {2, 1, 0, 3};
        t["suitbitsplayer"] = [](Ctx& c, int i) { return i < 4 ? fl(srankbitsOf(c.hole, inv[i])) : 0.0; };
        t["suitbitscommon"] = [](Ctx& c, int i) { return i < 4 ? fl(srankbitsOf(c.board, inv[i])) : 0.0; };
        t["randomround"] = [](Ctx& c, int i) { return i >= 0 && i < 6 ? c.roundRand[i] : 0.0; };
        t["ac_betpos"] = [](Ctx& c, int i) { return i < c.t->nchairs && c.t->seats[i].inHand ? fl(c.betpos(i)) : 0.0; };
        t["ac_dealpos"] = [](Ctx& c, int i) { return i < c.t->nchairs && c.t->seats[i].dealt ? fl(c.dealpos(i)) : 0.0; };
        return t;
    }();
    return d;
}

int roleChair(Ctx& c, const std::string& role) {
    if (role == "dealer" || role == "button") return c.t->dealer;
    if (role == "user") return c.chair;
    auto it = nativeTable().find(role + "chair");
    return it == nativeTable().end() ? -1 : static_cast<int>(it->second(c));
}

}  // namespace

SymbolContext::Native SymbolContext::resolveNative(const std::string& name) {
    const std::string low = lowerS(name);
    auto startsLow = [&](const char* p) { return low.rfind(p, 0) == 0; };
    if (startsLow("user") && !startsLow("userchair")) return [low](Ctx& c) { return c.uservars.count(low) ? 1.0 : 0.0; };
    if (startsLow("me_")) {
        size_t p1 = name.find('_');
        size_t p2 = name.find('_', p1 + 1);
        std::string op = name.substr(p1 + 1, p2 == std::string::npos ? std::string::npos : p2 - p1 - 1);
        if (p2 != std::string::npos && op != "st" && op != "re" && op != "inc" && op != "dec" && op != "add" && op != "sub") return nullptr;
        return [name](Ctx& c) { return c.memorySymbol(name); };
    }
    if (name.rfind("hand$", 0) == 0 || name.rfind("board$", 0) == 0) return [name](Ctx& c) { return c.handBoardExpr(name); };
    if (name.rfind("$$", 0) == 0) {
        static const std::regex re("^\\$\\$(p|c)(c|r|s)(\\d)$");
        if (!std::regex_match(name, re)) return nullptr;
        return [name](Ctx& c) { return c.cardCode(name); };
    }
    if (name.rfind("$", 0) == 0) return [name](Ctx& c) { return c.isHand(name); };
    if (startsLow("islist")) { std::string num = name.substr(6); return [num](Ctx& c) { return c.isList(num); }; }
    if (startsLow("vs$")) {
        const std::string rest = low.substr(3);
        auto post = [&](const char* p) { return rest.size() > 6 && rest.compare(rest.size() - 6, 6, p) == 0; };
        const int which = post("$prwin") ? 0 : post("$prtie") ? 1 : post("$prlos") ? 2 : -1;
        if (rest.rfind("multiplex$", 0) == 0 && which >= 0) {
            const std::string infix = name.substr(13, name.size() - 13 - 6);
            return [infix, which](Ctx& c) { const double d = c.symbol(names().intern(infix)); return c.versusList("list" + std::to_string(static_cast<int>(d + 0.5)), which); };
        }
        if (rest.rfind("list", 0) == 0 && which >= 0) {
            const std::string listName = name.substr(3, name.size() - 9);
            return [listName, which](Ctx& c) { return c.versusList(listName, which); };
        }
        // vs$nhands[hi|ti|lo][now], vs$prwin|prtie|prlos[hi|ti|lo][now]
        static const std::regex re("^(nhands|prwin|prtie|prlos)(hi|ti|lo)?(now)?$");
        std::smatch m;
        if (std::regex_match(rest, m, re)) {
            const std::string kind = m[1], grp = m[2], now = m[3];
            return [kind, grp, now](Ctx& c) -> double {
                const VersusResult& R = c.versus();
                if (!R.valid) return 0.0;
                const bool n = !now.empty();
                const VersusGroup& g = grp.empty() ? R.all : grp == "hi" ? (n ? R.hiNow : R.hi) : grp == "ti" ? (n ? R.tiNow : R.ti) : (n ? R.loNow : R.lo);
                if (kind == "nhands") return static_cast<double>(g.nhands);
                if (grp.empty() && !n) return kind == "prwin" ? R.prwin : kind == "prtie" ? R.prtie : R.prlos;   // prwin élevé à la puissance du nombre d'adversaires
                return kind == "prwin" ? g.prwin() : kind == "prtie" ? g.prtie() : g.prlos();
            };
        }
        return [name](Ctx& c) { c.noteUnknown(name); return 0.0; };                 // vs$ inconnu ou ancien format : 0
    }
    if (startsLow("hi_")) {
        static const std::regex re("^hi_(.*?)(\\d)$");
        std::smatch m;
        if (std::regex_match(low, m, re)) { std::string s = m[1]; int r = m[2].str()[0] - '0'; return [s, r](Ctx& c) { auto it = c.hist.find({s, r}); return it == c.hist.end() ? 0.0 : it->second; }; }
        return [](Ctx&) { return 0.0; };
    }
    if (startsLow("pt_")) {
        // pt_<stat><siège 0-9>  ou  pt_<stat>_<rôle>chair (raischair, headsupchair, bigblindchair, userchair...)
        std::string rest = low.substr(3), role;
        int fixedChair = -1;
        static const std::regex roleRe("^(.+)_([a-z0-9]+)chair$");
        std::smatch m;
        bool ok = false;
        if (std::regex_match(rest, m, roleRe)) {
            std::string r = m[2];
            if (r == "dealer" || r == "button" || r == "user" || nativeTable().count(r + "chair")) { role = r; rest = m[1]; ok = true; }
        }
        if (!ok && !rest.empty() && std::isdigit(static_cast<unsigned char>(rest.back()))) { fixedChair = rest.back() - '0'; rest.pop_back(); ok = true; }
        const int stat = ok ? pt::statIndex(rest) : -1;
        // symbole PokerTracker invalide ou statistique non simulée : -1 comme « pas de données » chez OpenHoldem, et signalé
        if (stat < 0) return [name](Ctx& c) { c.noteUnknown(name); return -1.0; };
        return [stat, fixedChair, role](Ctx& c) -> double {
            if (!c.game || !c.game->pt) return -1.0;
            const int ch = role.empty() ? fixedChair : roleChair(c, role);
            if (ch < 0 || ch >= c.t->nchairs) return -1.0;
            return c.game->pt->stat(c.t->seats[ch].name, stat);
        };
    }
    if (startsLow("ptt_")) return [](Ctx&) { return -1.0; };      // préfixe inexistant dans OpenHoldem 15
    for (const char* p : {"dll$", "sitename$", "log$", "msgbox$", "mh_"}) if (startsLow(p)) return [](Ctx&) { return 0.0; };
    auto it = nativeTable().find(low);
    if (it != nativeTable().end()) return it->second;
    if (low.size() >= 2 && std::isdigit(static_cast<unsigned char>(low.back()))) {
        std::string base = low.substr(0, low.size() - 1);
        bool ok = !base.empty() && std::all_of(base.begin(), base.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || ch == '_'; });
        if (ok) { auto d = digitTable().find(base); if (d != digitTable().end()) { FnI f = d->second; int n = low.back() - '0'; return [f, n](Ctx& c) { return f(c, n); }; } }
    }
    static const std::regex chairStat("^(balance|currentbet|stacksize|startingstacksize)_(bigblind|smallblind|dealer|button|cutoff|ep1|ep2|ep3|mp1|mp2|mp3|utg|lastraiser|firstraiser|bigstack|smallstack|headsup|user|rais|firstcaller|lastcaller)chair$");
    static const std::regex roundName("^([a-z]+)_(preflop|flop|turn|river)$");
    static const std::regex suitBits("^suitbits(player|common)_(clubs|diamonds|hearts|spades)$");
    static const std::regex prevRound("^(didcallround|didchecround|didraisround|didbetsizeround|didswaground|didfoldround|didalliround|nplayersround|nbetsround|raisbits|callbits|foldbits)_?previousround$");
    static const std::regex raisRound("^raisbits_?(flop|turn|preflop|river)$");
    std::smatch m;
    if (std::regex_match(low, m, chairStat)) {
        std::string base = m[1], role = m[2];
        return [base, role](Ctx& c) -> double {
            int ch = roleChair(c, role);
            if (ch < 0 || ch >= c.t->nchairs) return 0.0;
            const Seat& s = c.t->seats[ch];
            if (base == "balance") return fl(s.stack);
            if (base == "currentbet") return fl(s.bet);
            return (base == "stacksize" ? s.stack : s.startStack) / c.unit();
        };
    }
    if (std::regex_match(low, m, roundName)) {
        auto d = digitTable().find(m[1]);
        if (d != digitTable().end()) { FnI f = d->second; int rn = kRoundNames.at(m[2]); return [f, rn](Ctx& c) { return f(c, rn); }; }
    }
    if (std::regex_match(low, m, suitBits)) {
        bool player = m[1] == "player"; std::string s = m[2];
        int idx = s == "clubs" ? 0 : s == "diamonds" ? 1 : s == "hearts" ? 2 : 3;
        return [player, idx](Ctx& c) { return fl(srankbitsOf(player ? c.hole : c.board, idx)); };
    }
    if (std::regex_match(low, m, prevRound)) {
        auto d = digitTable().find(m[1]);
        if (d != digitTable().end()) { FnI f = d->second; return [f](Ctx& c) { return c.t->betround > 1 ? f(c, std::max(1, c.t->betround - 1)) : 0.0; }; }
    }
    if (std::regex_match(low, m, raisRound)) { int rn = kRoundNames.at(m[1]); return [rn](Ctx& c) { return fl(c.raisbits(rn)); }; }
    return nullptr;
}

}  // namespace ps::oh
