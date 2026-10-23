#include "pokersim/engine.hpp"
#include <algorithm>
#include <numeric>
#include <map>
#include "pokersim/evaluator.hpp"
#include "pokersim/pt.hpp"

namespace ps {

double drawMultiplier(const std::vector<std::pair<double, int>>& table, Rng& rng) {
    long long total = 0;
    for (auto& e : table) total += e.second;
    long long x = static_cast<long long>(rng.below(static_cast<uint32_t>(total)));
    for (auto& e : table) { if (x < e.second) return e.first; x -= e.second; }
    return table.back().first;
}

Game::Game(const SimConfig& c, std::vector<IBrain*> brains, std::vector<std::string> names,
           std::vector<int> profileIds, int gameId, uint64_t seed, bool collectRecords)
    : rng(seed), cfg(c), collect_(collectRecords), levels_(c.levels) {
    const int n = static_cast<int>(brains.size());
    t.nchairs = n; t.gameId = gameId;
    t.startingStack = cfg.startingStack; t.buyin = cfg.buyin;
    t.seats.resize(n);
    for (int i = 0; i < n; ++i) {
        Seat& s = t.seats[i];
        s.chair = i; s.name = names[i]; s.brain = brains[i]; s.profileId = profileIds[i];
        s.stack = cfg.startingStack; s.startStack = s.stack;
    }
    if (!cash()) {
        if (cfg.format == GameFormat::SpinGo) {
            t.multiplier = drawMultiplier(cfg.multipliers, rng);
            t.prizepool = t.multiplier * cfg.buyin * (cfg.multiplierTimesTotal ? n : 1);
        } else {
            t.multiplier = 0;
            t.prizepool = cfg.buyin * n * (1.0 - cfg.feePct / 100.0);
        }
    }
    t.dealer = static_cast<int>(rng.below(n));
    prevDealer_ = (t.dealer - 1 + n) % n;
}

void Game::blindsForState(int handsDone, int& lvl, int& sb, int& bb, int& ante) const {
    if (cash()) { lvl = 0; sb = cfg.cashSb; bb = cfg.cashBb; ante = cfg.cashAnte; return; }
    int l = (cfg.levelBy == LevelBy::Time) ? static_cast<int>(t.clock / cfg.levelSeconds) : handsDone / cfg.levelHands;
    l = std::min<int>(l, static_cast<int>(levels_.size()) - 1);
    lvl = l; sb = levels_[l].sb; bb = levels_[l].bb; ante = levels_[l].ante;
}

int Game::aliveCount() const { int n = 0; for (auto& s : t.seats) n += s.stack > 0; return n; }

void Game::log(int rnd, int chair, ActKind k, int amt, int to, int code, bool allin) {
    Action a; a.round = rnd; a.chair = chair; a.kind = k; a.amount = amt; a.to = to; a.code = code; a.allin = allin;
    t.actions.push_back(a);
}

void Game::post(Seat& s, int amount, ActKind kind) {
    amount = std::min(amount, s.stack);
    s.stack -= amount; s.bet += amount; s.committed += amount;
    if (s.stack == 0) s.allin = true;
    log(PREFLOP, s.chair, kind, amount, s.bet, PA_UNDEFINED, s.allin);
}

GameResult Game::play() {
    if (cash()) return playCash();
    for (auto& s : t.seats) s.brain->newGame(*this);
    while (aliveCount() > 1 && handsPlayed_ < cfg.maxHandsPerGame) playHand();
    const int n = t.nchairs;
    std::vector<int> alive;
    for (auto& s : t.seats) if (s.stack > 0) alive.push_back(s.chair);
    std::vector<int> order = finishOrder_;
    if (alive.size() > 1) {   // limite de mains atteinte : classement au nombre de jetons
        std::stable_sort(alive.begin(), alive.end(), [&](int a, int b) { return t.seats[a].stack < t.seats[b].stack; });
        for (size_t i = 0; i + 1 < alive.size(); ++i) { t.seats[alive[i]].busted = true; order.push_back(alive[i]); }
    }
    std::vector<int> places(n, n);
    if (!alive.empty()) places[alive.back()] = 1;
    int pos = n;
    for (int ch : order) places[ch] = pos--;
    GameResult r;
    r.format = cfg.format; r.game = t.gameId; r.hands = handsPlayed_; r.seconds = t.clock;
    r.multiplier = t.multiplier; r.prizepool = t.prizepool;
    for (auto& s : t.seats) {
        PlayerResult p;
        p.chair = s.chair; p.name = s.name; p.profileId = s.profileId; p.place = places[s.chair];
        p.hands = s.handsPlayed; p.netChips = s.netChips;
        p.gain = (p.place - 1 < (int)cfg.payoutShares.size()) ? t.prizepool * cfg.payoutShares[p.place - 1] : 0.0;
        p.net = p.gain - cfg.buyin;
        r.players.push_back(p);
    }
    r.records = std::move(records_);
    return r;
}

GameResult Game::playCash() {
    for (auto& s : t.seats) s.brain->newGame(*this);
    for (int i = 0; i < cfg.cashHands; ++i) playHand();
    GameResult r;
    r.format = GameFormat::Cash; r.game = t.gameId; r.hands = handsPlayed_; r.seconds = t.clock; r.rake = totalRake_;
    std::vector<int> idx(t.nchairs);
    std::iota(idx.begin(), idx.end(), 0);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return t.seats[a].netChips > t.seats[b].netChips; });
    std::vector<int> place(t.nchairs);
    for (int i = 0; i < t.nchairs; ++i) place[idx[i]] = i + 1;
    for (auto& s : t.seats) {
        PlayerResult p;
        p.chair = s.chair; p.name = s.name; p.profileId = s.profileId; p.place = place[s.chair];
        p.netChips = s.netChips; p.gain = s.netChips * cfg.chipValue; p.net = p.gain;
        p.rebuys = s.rebuys; p.hands = s.handsPlayed;
        p.bb100 = 100.0 * s.netChips / cfg.cashBb / std::max(1, s.handsPlayed);
        r.players.push_back(p);
    }
    r.records = std::move(records_);
    return r;
}

void Game::playHand() {
    const int n0 = t.nchairs;
    int lvl, sb, bb, ante;
    blindsForState(handsPlayed_, lvl, sb, bb, ante);
    handStartClock_ = t.clock;
    t.level = lvl; t.sblind = sb; t.bblind = bb; t.ante = ante;
    t.handNo = handsPlayed_ + 1;
    t.actions.clear(); t.board.clear();
    t.betround = PREFLOP; t.potPrev = 0;
    for (auto& s : t.seats) {
        s.bet = s.committed = 0; s.cards = {kNoCard, kNoCard};
        s.inHand = s.allin = s.dealt = false;
        for (auto& v : s.acts) v.clear();
        s.prevAction = PA_UNDEFINED;
    }
    if (cash()) {
        for (auto& s : t.seats) {
            if (cfg.cashStackMode == CashStackMode::Reset) s.stack = cfg.cashStack;
            else if (s.stack <= 0) { s.stack = cfg.cashStack; ++s.rebuys; }
        }
    }
    flopSeen_ = false;
    std::vector<int> alive;
    for (auto& s : t.seats) if (s.stack > 0) alive.push_back(s.chair);
    const int n = static_cast<int>(alive.size());
    // bouton
    int d = t.nextChair(prevDealer_);
    while (t.seats[d].stack <= 0) d = t.nextChair(d);
    t.dealer = d; prevDealer_ = d;
    std::vector<int> order;   // joueurs vivants à partir du bouton+1
    int c = d;
    for (int i = 0; i < n0; ++i) { c = t.nextChair(c); if (t.seats[c].stack > 0) order.push_back(c); }
    if (n == 2) { t.sbChair = d; t.bbChair = order[0]; }
    else { t.sbChair = order[0]; t.bbChair = order[1]; }
    std::vector<int> recordStart(n0, 0);
    for (int ch : alive) {
        Seat& s = t.seats[ch];
        s.dealt = s.inHand = true; s.startStack = s.stack; ++s.handsPlayed;
        recordStart[ch] = s.stack;
    }
    Deck deck;
    for (int ch : alive) { t.seats[ch].cards = {deck.draw(rng), deck.draw(rng)}; }
    std::vector<Card> boardCards;
    for (int i = 0; i < 5; ++i) boardCards.push_back(deck.draw(rng));
    for (int ch : alive) t.seats[ch].brain->newHand(*this, ch);
    // antes (argent mort dans le pot) puis blinds
    if (ante > 0) {
        for (int ch : alive) {
            Seat& s = t.seats[ch];
            int a = std::min(ante, s.stack);
            s.stack -= a; s.committed += a; t.potPrev += a;
            if (s.stack == 0) s.allin = true;
            log(PREFLOP, ch, ActKind::Ante, a, 0, PA_UNDEFINED, s.allin);
        }
    }
    post(t.seats[t.sbChair], sb, ActKind::SmallBlind);
    post(t.seats[t.bbChair], bb, ActKind::BigBlind);
    t.currentBet = 0;
    for (auto& s : t.seats) t.currentBet = std::max(t.currentBet, s.bet);
    t.lastRaiseSize = bb;
    double seconds = cfg.handBaseSeconds;
    for (int rnd = PREFLOP; rnd <= RIVER; ++rnd) {
        t.betround = rnd;
        if (rnd == FLOP) t.board.assign(boardCards.begin(), boardCards.begin() + 3);
        else if (rnd == TURN) t.board.assign(boardCards.begin(), boardCards.begin() + 4);
        else if (rnd == RIVER) t.board.assign(boardCards.begin(), boardCards.begin() + 5);
        if (rnd > PREFLOP) {
            for (auto& s : t.seats) { t.potPrev += s.bet; s.bet = 0; }
            t.currentBet = 0; t.lastRaiseSize = bb;
        }
        if (t.nInHand() <= 1) break;
        if (rnd == FLOP) flopSeen_ = true;
        std::vector<int> first;
        if (rnd == PREFLOP) {
            if (n >= 3) { first.assign(order.begin() + 2, order.end()); first.insert(first.end(), order.begin(), order.begin() + 2); }
            else first = {d, order[0]};
        } else first = order;
        size_t before = t.actions.size();
        bettingRound(rnd, first);
        seconds += cfg.actionSeconds * static_cast<double>(t.actions.size() - before);
        if (t.nInHand() <= 1) break;
    }
    Settle res = settle(boardCards);
    if (res.showdown) seconds += cfg.showdownSeconds;
    t.clock += seconds;
    std::vector<int> bustedNow;
    if (!cash()) for (int ch : alive) if (t.seats[ch].stack <= 0) bustedNow.push_back(ch);
    std::stable_sort(bustedNow.begin(), bustedNow.end(), [&](int a, int b) {
        return recordStart[a] != recordStart[b] ? recordStart[a] < recordStart[b] : a < b; });
    for (int ch : bustedNow) { t.seats[ch].busted = true; finishOrder_.push_back(ch); }
    // enregistrement
    HandRecord rec;
    rec.game = t.gameId; rec.hand = t.handNo; rec.level = lvl + 1; rec.sb = sb; rec.bb = bb; rec.ante = ante;
    rec.time = handStartClock_; rec.dealer = t.dealer; rec.sbChair = t.sbChair; rec.bbChair = t.bbChair;
    rec.rake = res.rake; rec.showdown = res.showdown; rec.flopSeen = flopSeen_;
    rec.board = res.showdown ? boardCards : t.board;
    rec.actions = t.actions; rec.pots = res.pots;
    for (auto& s : t.seats) {
        if (!s.dealt) continue;
        SeatRecord sr;
        sr.chair = s.chair; sr.startStack = recordStart[s.chair]; sr.endStack = s.stack; sr.committed = s.committed;
        sr.collected = res.collected[s.chair]; sr.cards = s.cards; sr.folded = !s.inHand;
        if (res.showdown && s.inHand) sr.handType = categoryName(categoryOf(res.values[s.chair]));
        s.netChips += sr.collected - sr.committed;
        rec.seats.push_back(sr);
    }
    ++handsPlayed_;
    if (pt) { std::vector<std::string> nm; for (auto& s : t.seats) nm.push_back(s.name); pt->update(rec, nm); }   // les statistiques ne voient que les mains passées
    for (int ch : alive) t.seats[ch].brain->endHand(*this, rec);
    if (collect_) records_.push_back(std::move(rec));
}

void Game::bettingRound(int rnd, const std::vector<int>& order) {
    std::vector<int> pending;
    for (int c : order) if (t.seats[c].inHand && !t.seats[c].allin) pending.push_back(c);
    if (pending.empty()) return;
    if (pending.size() == 1 && t.seats[pending[0]].bet >= t.largestBet()) return;
    unsigned acted = 0;
    int guard = 0;
    while (!pending.empty() && ++guard <= 500) {
        int c = pending.front(); pending.erase(pending.begin());
        Seat& s = t.seats[c];
        if (!s.inHand || s.allin) continue;
        if (t.nInHand() <= 1) return;
        int others = 0;
        for (auto& x : t.seats) if (x.inHand && !x.allin && x.chair != c) ++others;
        int toCall = t.currentBet - s.bet;
        if (toCall <= 0 && others == 0) break;
        bool canRaise = !(acted & (1u << c)) && (others > 0 || toCall < 0);
        Decision dec = s.brain->decide(*this, c);
        Legal L = legalize(s, dec, toCall, canRaise);
        apply(s, L, rnd, acted);
        s.brain->afterAction(*this, c);
        if (L.aggr) {
            pending.clear();
            int cc = c;
            for (int i = 0; i < t.nchairs - 1; ++i) {
                cc = t.nextChair(cc);
                const Seat& x = t.seats[cc];
                if (x.inHand && !x.allin && x.bet < t.currentBet) pending.push_back(cc);
            }
        }
        if (t.nInHand() <= 1) return;
    }
}

Game::Legal Game::legalize(const Seat& s, const Decision& d, int toCall, bool canRaise) const {
    const int total = s.bet + s.stack;
    Legal out{d.kind, d.to, d.code, false};
    switch (d.kind) {
        case Decision::Fold:
            if (toCall <= 0) { out.kind = Decision::Check; out.code = PA_CHECK; }
            return out;
        case Decision::Check:
            if (toCall > 0) { out.kind = Decision::Fold; out.code = PA_FOLD; }
            return out;
        case Decision::Call:
            if (toCall <= 0) { out.kind = Decision::Check; out.code = PA_CHECK; }
            return out;
        default: break;
    }
    int target = (d.kind == Decision::AllIn) ? total : d.to;
    if (target >= total) target = total;
    auto passive = [&]() { out.kind = toCall > 0 ? Decision::Call : Decision::Check; out.code = toCall > 0 ? PA_CALL : PA_CHECK; return out; };
    if (!canRaise || total <= t.currentBet) return passive();
    int minTo = t.currentBet + t.lastRaiseSize;
    if (target < minTo && total > minTo) target = minTo;
    if (target <= t.currentBet) return passive();
    out.kind = Decision::Raise; out.to = target; out.aggr = true;
    return out;
}

void Game::apply(Seat& s, const Legal& d, int rnd, unsigned& acted) {
    const int toCall = t.currentBet - s.bet;
    acted |= 1u << s.chair;
    if (d.kind == Decision::Fold) {
        s.inHand = false; s.acts[rnd].push_back(PA_FOLD); s.prevAction = PA_FOLD;
        log(rnd, s.chair, ActKind::Fold, 0, s.bet, PA_FOLD, false);
        return;
    }
    if (d.kind == Decision::Check) {
        s.acts[rnd].push_back(PA_CHECK); s.prevAction = PA_CHECK;
        log(rnd, s.chair, ActKind::Check, 0, s.bet, PA_CHECK, false);
        return;
    }
    if (d.kind == Decision::Call) {
        int amt = std::min(toCall, s.stack);
        s.stack -= amt; s.bet += amt; s.committed += amt;
        if (s.stack == 0) s.allin = true;
        s.acts[rnd].push_back(PA_CALL); s.prevAction = PA_CALL;
        log(rnd, s.chair, ActKind::Call, amt, s.bet, PA_CALL, s.allin);
        return;
    }
    int amt = std::min(d.to - s.bet, s.stack);
    s.stack -= amt; s.bet += amt; s.committed += amt;
    if (s.stack == 0) s.allin = true;
    int increment = s.bet - t.currentBet;
    ActKind kind = (t.currentBet == 0) ? ActKind::Bet : ActKind::Raise;
    if (increment >= t.lastRaiseSize) { t.lastRaiseSize = increment; acted = 0; acted |= 1u << s.chair; }
    t.currentBet = s.bet;
    int code = (d.code == PA_RAISE || d.code == PA_BETSIZE || d.code == PA_ALLIN) ? d.code : PA_BETSIZE;
    if (s.allin) code = (d.code == PA_UNDEFINED) ? PA_ALLIN : d.code;
    s.acts[rnd].push_back(code); s.prevAction = code;
    log(rnd, s.chair, kind, amt, s.bet, code, s.allin);
}

int Game::rakeFor(int base) const {
    if (!cash() || cfg.rakePct <= 0 || base <= 0) return 0;
    if (cfg.noFlopNoDrop && !flopSeen_) return 0;
    int rake = static_cast<int>(base * cfg.rakePct / 100.0);
    if (cfg.rakeCap > 0) rake = std::min(rake, cfg.rakeCap);
    return rake;
}

Game::Settle Game::settle(const std::vector<Card>& boardCards) {
    Settle R;
    R.showdown = false; R.rake = 0;
    R.collected.assign(t.nchairs, 0);
    R.values.assign(t.nchairs, 0);
    std::vector<int> contenders;
    for (auto& s : t.seats) if (s.inHand) contenders.push_back(s.chair);
    for (auto& s : t.seats) { t.potPrev += s.bet; s.bet = 0; }
    std::vector<int> committed(t.nchairs, 0);
    int total = 0;
    for (auto& s : t.seats) if (s.dealt) { committed[s.chair] = s.committed; total += s.committed; }
    if (contenders.size() == 1) {
        Seat& w = t.seats[contenders[0]];
        int other = 0;
        for (int i = 0; i < t.nchairs; ++i) if (i != w.chair) other = std::max(other, committed[i]);
        int uncalled = std::max(0, committed[w.chair] - other);
        int rake = rakeFor(total - uncalled);
        w.stack += total - rake; R.collected[w.chair] = total - rake; totalRake_ += rake; R.rake = rake;
        R.pots.push_back({total - rake, {w.chair}, {w.chair}, false});
        return R;
    }
    R.showdown = true;
    t.board = boardCards;
    for (int ch : contenders) {
        Card seven[7] = {t.seats[ch].cards[0], t.seats[ch].cards[1], boardCards[0], boardCards[1], boardCards[2], boardCards[3], boardCards[4]};
        R.values[ch] = evaluate(seven, 7);
    }
    std::vector<int> lv;
    for (int v : committed) if (v > 0) lv.push_back(v);
    std::sort(lv.begin(), lv.end()); lv.erase(std::unique(lv.begin(), lv.end()), lv.end());
    struct Layer { int amount; std::vector<int> eligible; };
    std::vector<Layer> layers;
    int prev = 0;
    for (int level : lv) {
        int amount = 0;
        for (int v : committed) amount += std::min(v, level) - std::min(v, prev);
        std::vector<int> el;
        for (int ch : contenders) if (committed[ch] >= level) el.push_back(ch);
        prev = level;
        if (amount <= 0) continue;
        if (el.empty()) {
            int best = contenders[0];
            for (int ch : contenders) if (committed[ch] > committed[best]) best = ch;
            el.push_back(best);
        }
        layers.push_back({amount, el});
    }
    int contested = 0;
    for (auto& l : layers) if (l.eligible.size() > 1) contested += l.amount;
    int rake = rakeFor(contested), left = rake;
    for (auto& l : layers) if (l.eligible.size() > 1 && left > 0) { int take = std::min(left, l.amount); l.amount -= take; left -= take; }
    totalRake_ += rake; R.rake = rake;
    for (auto& l : layers) {
        if (l.eligible.size() == 1) {
            int ch = l.eligible[0];
            t.seats[ch].stack += l.amount; R.collected[ch] += l.amount;
            R.pots.push_back({l.amount, {ch}, l.eligible, true});
            continue;
        }
        if (l.amount <= 0) continue;
        uint32_t best = 0;
        for (int ch : l.eligible) best = std::max(best, R.values[ch]);
        std::vector<int> winners;
        for (int ch : l.eligible) if (R.values[ch] == best) winners.push_back(ch);
        int share = l.amount / static_cast<int>(winners.size());
        int rem = l.amount - share * static_cast<int>(winners.size());
        std::vector<int> ordered;
        int c = t.dealer;
        for (int i = 0; i < t.nchairs; ++i) { c = t.nextChair(c); if (std::find(winners.begin(), winners.end(), c) != winners.end()) ordered.push_back(c); }
        for (size_t i = 0; i < ordered.size(); ++i) {
            int got = share + (static_cast<int>(i) < rem ? 1 : 0);
            t.seats[ordered[i]].stack += got; R.collected[ordered[i]] += got;
        }
        R.pots.push_back({l.amount, winners, l.eligible, false});
    }
    return R;
}

}  // namespace ps
