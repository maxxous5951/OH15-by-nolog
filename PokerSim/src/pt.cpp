#include "pokersim/pt.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "nlohmann/json.hpp"
#include "pokersim/pathutil.hpp"

namespace ps::pt {

static const char* const kStatNames[S_COUNT] = {
#define X(id, name) name,
    PT_STATS(X)
#undef X
};
const char* statName(int s) { return s >= 0 && s < S_COUNT ? kStatNames[s] : ""; }
int statIndex(const std::string& n) {
    for (int i = 0; i < S_COUNT; ++i) if (n == kStatNames[i]) return i;
    return -1;
}

double PlayerCounters::value(int s) const {
    if (s == S_HANDS) return num[S_HANDS];
    return den[s] > 0 ? num[s] / den[s] : -1.0;
}

// ============================================================ base de données
const PlayerCounters* Database::find(const std::string& name, Mode m) const {
    auto it = players.find(name);
    return it == players.end() ? nullptr : &it->second.mode[static_cast<int>(m)];
}
void Database::merge(const Database& o) {
    for (auto& kv : o.players) for (int m = 0; m < 2; ++m) players[kv.first].mode[m].merge(kv.second.mode[m]);
}
long long Database::totalHands(Mode m) const {
    double t = 0;
    for (auto& kv : players) t += kv.second.mode[static_cast<int>(m)].num[S_HANDS];
    return static_cast<long long>(t);
}

using json = nlohmann::json;
std::string Database::toJson() const {
    json j;
    j["format"] = "pokersim-pokertracker";
    j["version"] = 1;
    j["players"] = json::object();
    for (auto& kv : players) {
        json pj = json::object();
        for (int m = 0; m < 2; ++m) {
            json mj = json::object();
            for (int s = 0; s < S_COUNT; ++s) {
                const PlayerCounters& c = kv.second.mode[m];
                if (c.num[s] != 0 || c.den[s] != 0) mj[kStatNames[s]] = {c.num[s], c.den[s]};
            }
            if (!mj.empty()) pj[m == 0 ? "cash" : "tourney"] = mj;
        }
        if (!pj.empty()) j["players"][kv.first] = pj;
    }
    return j.dump(1);
}
Database Database::fromJson(const std::string& text) {
    Database db;
    try {
        json j = json::parse(text);
        if (j.value("format", "") != "pokersim-pokertracker") throw std::runtime_error("ce n'est pas une base PokerTracker simulée de PokerSim");
        for (auto it = j.at("players").begin(); it != j.at("players").end(); ++it) {
            for (int m = 0; m < 2; ++m) {
                const char* key = m == 0 ? "cash" : "tourney";
                if (!it.value().contains(key)) continue;
                PlayerCounters& c = db.counters(it.key(), static_cast<Mode>(m));
                for (auto s = it.value()[key].begin(); s != it.value()[key].end(); ++s) {
                    int idx = statIndex(s.key());
                    if (idx < 0) continue;                       // statistique inconnue de cette version : ignorée
                    c.num[idx] = s.value().at(0).get<double>();
                    c.den[idx] = s.value().at(1).get<double>();
                }
            }
        }
    } catch (const json::exception& e) { throw std::runtime_error(std::string("base PokerTracker illisible : ") + e.what()); }
    return db;
}
bool Database::save(const std::string& path) const {
    std::ofstream f(pathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << toJson();
    return static_cast<bool>(f);
}
Database Database::load(const std::string& path) {
    std::ifstream f(pathFromUtf8(path), std::ios::binary);
    if (!f) throw std::runtime_error("Impossible d'ouvrir la base PokerTracker : " + path);
    std::stringstream ss; ss << f.rdbuf();
    return fromJson(ss.str());
}

double Tracker::stat(const std::string& player, int s) const {
    PlayerCounters c;
    bool any = false;
    if (base_) if (const PlayerCounters* b = base_->find(player, mode_)) { c.merge(*b); any = true; }
    if (const PlayerCounters* d = delta_.find(player, mode_)) { c.merge(*d); any = true; }
    if (!any) return s == S_HANDS ? 0.0 : -1.0;
    return c.value(s);
}

// ============================================================ analyse d'une main
namespace {

struct P {
    bool dealt = false;
    int stack = 0, bet = 0, committed = 0;
    // préflop
    int nActs = 0;
    bool anyAct = false, vpip = false, pfr = false, firstSeen = false, firstOppHere = false, limped = false;
    bool openOpp = false, firstRaise = false, stealOpp = false, stealAtt = false;
    bool p3Opp = false, p3 = false, p3DefOpp = false; char p3Act = 0;
    bool p4Opp = false, p4 = false, p4DefOpp = false; char p4Act = 0;
    bool sqOpp = false, sq = false, sqDefOpp = false, sqAsRaiser = false; char sqAct = 0;
    bool blindDefOpp = false, bbFold = false, sbFold = false;
    bool facedRaiseSeen = false, calledRaiseOpp = false, calledRaiseNum = false;
    // postflop
    bool saw[5] = {false, false, false, false, false};
    int agg[5] = {0}, calls[5] = {0}, folds[5] = {0};
    bool foldedPre = false;
};

char actCode(ActKind k) { return k == ActKind::Fold ? 'F' : k == ActKind::Call ? 'C' : 'R'; }
bool isAgg(ActKind k) { return k == ActKind::Bet || k == ActKind::Raise; }

}  // namespace

void analyzeHand(const HandRecord& h, const std::vector<std::string>& names, Database& into, Mode mode) {
    constexpr int kMax = 16;
    std::array<P, kMax> pl;
    std::vector<int> dealt;
    for (const SeatRecord& s : h.seats) {
        if (s.chair < 0 || s.chair >= kMax) continue;
        pl[s.chair].dealt = true; pl[s.chair].stack = s.startStack;
        dealt.push_back(s.chair);
    }
    if (dealt.size() < 2) return;
    std::sort(dealt.begin(), dealt.end());
    auto nameOf = [&](int c) { return c >= 0 && c < static_cast<int>(names.size()) ? names[static_cast<size_t>(c)] : "Siège " + std::to_string(c); };
    const int n = static_cast<int>(dealt.size());

    // positions de vol : bouton, petite blind, et cut-off (joueur juste avant le bouton) à partir de 4 joueurs
    std::array<bool, kMax> steal{};
    steal[static_cast<size_t>(h.dealer)] = true;
    if (h.sbChair >= 0) steal[static_cast<size_t>(h.sbChair)] = true;
    if (n >= 4) {
        int c = h.dealer;
        for (int i = 0; i < kMax; ++i) { c = (c + kMax - 1) % kMax; if (c < kMax && pl[c].dealt) break; }
        if (pl[static_cast<size_t>(c)].dealt) steal[static_cast<size_t>(c)] = true;
    }

    // ---------------- préflop
    int curBet = 0, raises = 0, lastRaiser = -1, opener = -1, threeBettor = -1;
    bool anyVoluntary = false, openIsSteal = false, squeezeEvent = false;
    std::vector<int> callersSinceOpen;
    for (const Action& a : h.actions) {
        if (a.round != PREFLOP) continue;
        P& p = pl[static_cast<size_t>(a.chair)];
        if (a.kind == ActKind::Ante) { p.committed += a.amount; continue; }
        if (a.kind == ActKind::SmallBlind || a.kind == ActKind::BigBlind) { p.committed += a.amount; p.bet = a.to; curBet = std::max(curBet, a.to); continue; }
        const int toCall = curBet - p.bet;
        const bool canRaise = (p.stack - p.committed) > toCall;
        const bool isRaise = isAgg(a.kind), isCall = a.kind == ActKind::Call, isFold = a.kind == ActKind::Fold;
        ++p.nActs; p.anyAct = true;
        const bool openOpp = raises == 0 && !anyVoluntary;
        const bool faceLimpers = raises == 0 && anyVoluntary;
        const bool p3Opp = raises == 1 && a.chair != lastRaiser && canRaise;
        const bool p4Opp = raises == 2 && canRaise;
        if (!p.firstSeen) { p.firstSeen = true; p.firstOppHere = canRaise && (openOpp || faceLimpers || p3Opp || p4Opp); }
        if (isCall || isRaise) p.vpip = true;
        if (isRaise) p.pfr = true;
        if (openOpp) {
            p.openOpp = true;
            if (isRaise) p.firstRaise = true;
            if (steal[static_cast<size_t>(a.chair)]) { p.stealOpp = true; if (isRaise) p.stealAtt = true; }
        }
        if (p3Opp) { p.p3Opp = true; if (isRaise) p.p3 = true; }
        if (raises >= 2 && opener == a.chair && !p.p3DefOpp) { p.p3DefOpp = true; p.p3Act = actCode(a.kind); }     // 1re décision face au 3bet
        if (p4Opp) { p.p4Opp = true; if (isRaise) p.p4 = true; }
        if (raises >= 3 && threeBettor == a.chair && !p.p4DefOpp) { p.p4DefOpp = true; p.p4Act = actCode(a.kind); }
        const bool inCallers = std::find(callersSinceOpen.begin(), callersSinceOpen.end(), a.chair) != callersSinceOpen.end();
        if (raises == 1 && !callersSinceOpen.empty() && a.chair != opener && !inCallers && canRaise) { p.sqOpp = true; if (isRaise) p.sq = true; }
        if (raises >= 2 && squeezeEvent && (a.chair == opener || inCallers) && !p.sqDefOpp) { p.sqDefOpp = true; p.sqAct = actCode(a.kind); p.sqAsRaiser = a.chair == opener; }
        if (raises == 1 && openIsSteal && a.chair != opener && (a.chair == h.sbChair || a.chair == h.bbChair) && !p.blindDefOpp) {
            p.blindDefOpp = true;
            if (isFold) { if (a.chair == h.bbChair) p.bbFold = true; else p.sbFold = true; }
        }
        if (raises >= 1 && !p.facedRaiseSeen) { p.facedRaiseSeen = true; p.calledRaiseOpp = true; if (isCall && !p.limped) p.calledRaiseNum = true; }
        // état de la main
        if (isRaise || isCall) { p.committed += a.amount; p.bet = a.to; }
        if (isRaise) {
            ++raises;
            if (raises == 1) { opener = a.chair; callersSinceOpen.clear(); openIsSteal = p.stealAtt; }
            else if (raises == 2) { threeBettor = a.chair; squeezeEvent = p.sq; }
            lastRaiser = a.chair; curBet = a.to; anyVoluntary = true;
        } else if (isCall) {
            if (raises == 0) p.limped = true;
            if (raises == 1) callersSinceOpen.push_back(a.chair);
            anyVoluntary = true;
        }
    }
    const int pfAgg = raises >= 1 ? lastRaiser : -1;

    // ---------------- postflop
    std::array<std::array<bool, 5>, kMax> foldedBefore{};
    for (const Action& a : h.actions) if (a.kind == ActKind::Fold) for (int r = a.round + 1; r <= 4; ++r) foldedBefore[static_cast<size_t>(a.chair)][static_cast<size_t>(r)] = true;
    const size_t boardNeed[5] = {0, 0, 3, 4, 5};
    for (int c : dealt) for (int r = FLOP; r <= RIVER; ++r) pl[static_cast<size_t>(c)].saw[r] = !foldedBefore[static_cast<size_t>(c)][static_cast<size_t>(r)] && h.board.size() >= boardNeed[r];

    // compteurs par rue
    struct Cnt { double num[S_COUNT] = {0}, den[S_COUNT] = {0}; };
    std::array<Cnt, kMax> extra{};                    // statistiques postflop spécifiques (c-bet, check-raise...)
    auto addx = [&](int c, int stat, double nn, double dd) { extra[static_cast<size_t>(c)].num[stat] += nn; extra[static_cast<size_t>(c)].den[stat] += dd; };
    int cbetBy[5] = {-1, -1, -1, -1, -1};
    for (int r = FLOP; r <= RIVER; ++r) {
        std::vector<const Action*> acts;
        for (const Action& a : h.actions) if (a.round == r) acts.push_back(&a);
        for (const Action* a : acts) {
            P& p = pl[static_cast<size_t>(a->chair)];
            if (isAgg(a->kind)) ++p.agg[r]; else if (a->kind == ActKind::Call) ++p.calls[r]; else if (a->kind == ActKind::Fold) ++p.folds[r];
        }
        auto firstIdx = [&](int c) { for (size_t i = 0; i < acts.size(); ++i) if (acts[i]->chair == c) return static_cast<int>(i); return -1; };
        auto betBefore = [&](int idx) { for (int i = 0; i < idx; ++i) if (isAgg(acts[static_cast<size_t>(i)]->kind)) return true; return false; };
        // c-bet : le dernier relanceur préflop (flop), puis la même personne à la turn / river
        const int X = r == FLOP ? pfAgg : cbetBy[r - 1];
        if (X >= 0 && pl[static_cast<size_t>(X)].saw[r]) {
            int ix = firstIdx(X);
            if (ix >= 0 && !betBefore(ix)) {
                const bool cb = acts[static_cast<size_t>(ix)]->kind == ActKind::Bet;
                if (r == FLOP) addx(X, S_FLOP_CBET, cb, 1); else if (r == TURN) addx(X, S_TURN_CBET, cb, 1);
                if (cb) {
                    cbetBy[r] = X;
                    std::array<bool, kMax> done{};
                    for (size_t i = static_cast<size_t>(ix) + 1; i < acts.size(); ++i) {
                        const int q = acts[i]->chair;
                        if (q == X || done[static_cast<size_t>(q)]) continue;
                        done[static_cast<size_t>(q)] = true;
                        const char code = actCode(acts[i]->kind);
                        if (r == FLOP) { addx(q, S_FLOP_FOLD_TO_CBET, code == 'F', 1); addx(q, S_FLOP_RAISE_CBET, code == 'R', 1); }
                        else if (r == TURN) addx(q, S_TURN_FOLD_TO_CBET, code == 'F', 1);
                        else addx(q, S_RIVER_FOLD_TO_CBET, code == 'F', 1);
                    }
                }
            }
        }
        // check-raise / check-call
        for (int c : dealt) {
            int ix = firstIdx(c);
            if (ix < 0 || acts[static_cast<size_t>(ix)]->kind != ActKind::Check) continue;
            bool later = false, raised = false, called = false;
            for (size_t i = static_cast<size_t>(ix) + 1; i < acts.size(); ++i) {
                if (acts[i]->chair != c) continue;
                later = true;
                if (isAgg(acts[i]->kind)) raised = true;
                if (acts[i]->kind == ActKind::Call) called = true;
            }
            if (!later) continue;
            if (r == FLOP) addx(c, S_FLOP_CHECKRAISE, raised, 1);
            else if (r == TURN) { addx(c, S_TURN_CHECKRAISE, raised, 1); addx(c, S_TURN_CHECKCALL, called, 1); }
        }
        // donk bet (flop) : face à un relanceur préflop, hors 3bet/4bet, premier à miser avant le relanceur
        if (r == FLOP && pfAgg >= 0) {
            const int ia = firstIdx(pfAgg);
            for (int c : dealt) {
                P& p = pl[static_cast<size_t>(c)];
                if (c == pfAgg || !p.saw[FLOP] || !p.facedRaiseSeen || p.p3 || p.p4) continue;
                int ix = firstIdx(c);
                if (ix < 0 || betBefore(ix)) continue;
                if (ia >= 0 && ix > ia) continue;               // doit parler avant le relanceur
                addx(c, S_FLOP_DONKBET, acts[static_cast<size_t>(ix)]->kind == ActKind::Bet, 1);
            }
        }
        // river bet : mise ou check à la river
        if (r == RIVER) {
            for (int c : dealt) {
                bool bet = false, check = false;
                for (const Action* a : acts) if (a->chair == c) { if (a->kind == ActKind::Bet) bet = true; if (a->kind == ActKind::Check) check = true; }
                if (bet || check) addx(c, S_RIVER_BET, bet, 1);
            }
        }
    }

    // showdown / gains
    std::array<bool, kMax> won{};
    for (const PotRecord& pr : h.pots) if (!pr.uncalled) for (int w : pr.winners) if (w >= 0 && w < kMax) won[static_cast<size_t>(w)] = true;
    std::array<bool, kMax> folded{};
    for (const Action& a : h.actions) if (a.kind == ActKind::Fold) folded[static_cast<size_t>(a.chair)] = true;

    // ---------------- ajout aux compteurs
    for (int c : dealt) {
        const P& p = pl[static_cast<size_t>(c)];
        PlayerCounters& k = into.counters(nameOf(c), mode);
        const bool isBB = c == h.bbChair, isSB = c == h.sbChair;
        k.add(S_HANDS, 1, 1);
        k.add(S_VPIP, p.vpip, p.anyAct);
        k.add(S_PFR, p.pfr, p.pfr || p.nActs >= 2 || p.firstOppHere);
        k.add(S_RFI, p.firstRaise, p.openOpp);
        k.add(S_STEAL_ATTEMPT, p.stealAtt, p.stealOpp);
        k.add(S_BB_FOLD_TO_STEAL, p.bbFold, p.blindDefOpp && isBB);
        k.add(S_SB_FOLD_TO_STEAL, p.sbFold, p.blindDefOpp && isSB && !isBB);
        k.add(S_P3BET_VS_STEAL, p.blindDefOpp && p.p3, p.blindDefOpp && p.p3Opp);
        k.add(S_BB_3BET_VS_STEAL, isBB && p.blindDefOpp && p.p3, isBB && p.blindDefOpp && p.p3Opp);
        k.add(S_SB_3BET_VS_STEAL, isSB && !isBB && p.blindDefOpp && p.p3, isSB && !isBB && p.blindDefOpp && p.p3Opp);
        k.add(S_PREFLOP_CALLED_RAISE, p.calledRaiseNum, p.calledRaiseOpp);
        k.add(S_P3BET, p.p3, p.p3Opp);
        k.add(S_FOLD_TO_3BET, p.p3Act == 'F', p.p3DefOpp);
        k.add(S_FOLD_TO_RESTEAL, p.stealAtt && p.p3DefOpp && p.p3Act == 'F', p.stealAtt && p.p3DefOpp);
        k.add(S_P4BET, p.p4, p.p4Opp);
        k.add(S_FOLD_TO_4BET, p.p4Act == 'F', p.p4DefOpp);
        k.add(S_PREFLOP_SQUEEZE, p.sq, p.sqOpp);
        k.add(S_FOLD_TO_PF_SQUEEZE, p.sqAct == 'F', p.sqDefOpp);
        k.add(S_FOLD_TO_SQUEEZE_AS_RAISER, p.sqAct == 'F' && p.sqAsRaiser, p.sqDefOpp && p.sqAsRaiser);
        k.add(S_FOLD_TO_SQUEEZE_AS_CALLER, p.sqAct == 'F' && !p.sqAsRaiser, p.sqDefOpp && !p.sqAsRaiser);
        // agressivité postflop
        const int afIdx[3] = {S_FLOP_AF, S_TURN_AF, S_RIVER_AF}, afqIdx[3] = {S_FLOP_AFQ, S_TURN_AFQ, S_RIVER_AFQ};
        double tAgg = 0, tCalls = 0, tFolds = 0;
        for (int r = FLOP; r <= RIVER; ++r) {
            k.add(afIdx[r - FLOP], p.agg[r], p.calls[r]);
            k.add(afqIdx[r - FLOP], p.agg[r], p.agg[r] + p.calls[r] + p.folds[r]);
            tAgg += p.agg[r]; tCalls += p.calls[r]; tFolds += p.folds[r];
        }
        k.add(S_TOTAL_AF, tAgg, tCalls);
        k.add(S_TOTAL_AFQ, tAgg, tAgg + tCalls + tFolds);
        // street vues, showdown
        k.add(S_FLOP_SEEN, p.saw[FLOP], 1);
        k.add(S_TURN_SEEN, p.saw[TURN], p.saw[FLOP]);
        k.add(S_RIVER_SEEN, p.saw[RIVER], p.saw[TURN]);
        const bool sd = h.showdown && !folded[static_cast<size_t>(c)];
        k.add(S_WTSD, sd, p.saw[FLOP]);
        k.add(S_WSSD, sd && won[static_cast<size_t>(c)], sd);
        const Cnt& e = extra[static_cast<size_t>(c)];
        for (int s = 0; s < S_COUNT; ++s) if (e.den[s] != 0 || e.num[s] != 0) k.add(s, e.num[s], e.den[s]);
    }
}

}  // namespace ps::pt
