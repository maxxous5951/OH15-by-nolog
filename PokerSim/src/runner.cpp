#include "pokersim/runner.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace ps {

SimSummary runSimulation(const SimConfig& cfg, const BrainFactory& factory, bool collectRecords,
                         const std::function<void(int, int)>& progress) {
    const int np = static_cast<int>(cfg.players.size());
    if (np != cfg.numPlayers) throw std::runtime_error("Le nombre de profils déclarés ne correspond pas à numPlayers");
    SimSummary out;
    out.games.resize(cfg.numGames);
    std::atomic<int> next{0}, done{0};
    std::mutex errMu;
    std::string error;
    auto t0 = std::chrono::steady_clock::now();
    auto worker = [&]() {
        for (;;) {
            int g = next.fetch_add(1);
            if (g >= cfg.numGames || !error.empty()) return;
            try {
                uint64_t x = cfg.seed ^ (0x9E3779B97F4A7C15ULL * (static_cast<uint64_t>(g) + 1));
                uint64_t gameSeed = splitmix64(x);
                Rng prng(splitmix64(x));
                std::vector<int> perm(np);
                for (int i = 0; i < np; ++i) perm[i] = i;
                if (cfg.rotateSeats)
                    for (int i = np - 1; i > 0; --i) std::swap(perm[i], perm[prng.below(static_cast<uint32_t>(i + 1))]);
                std::vector<std::unique_ptr<IBrain>> owned;
                std::vector<IBrain*> brains;
                std::vector<std::string> names;
                for (int seat = 0; seat < np; ++seat) {
                    int pid = perm[seat];
                    owned.push_back(factory(cfg.players[pid], pid, splitmix64(x), cfg));
                    brains.push_back(owned.back().get());
                    names.push_back(cfg.players[pid].name);
                }
                Game game(cfg, brains, names, perm, g, gameSeed, collectRecords);
                out.games[g] = game.play();
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lk(errMu);
                if (error.empty()) error = e.what();
                return;
            }
            int d = ++done;
            if (progress) progress(d, cfg.numGames);
        }
    };
    int nt = cfg.workers > 0 ? cfg.workers : static_cast<int>(std::thread::hardware_concurrency());
    nt = std::max(1, std::min(nt, cfg.numGames));
    std::vector<std::thread> th;
    for (int i = 1; i < nt; ++i) th.emplace_back(worker);
    worker();
    for (auto& t : th) t.join();
    if (!error.empty()) throw std::runtime_error(error);

    out.profiles.resize(np);
    for (int i = 0; i < np; ++i) out.profiles[i].name = cfg.players[i].name;
    for (auto& gr : out.games) {
        for (auto& p : gr.players) {
            ProfileStats& s = out.profiles[p.profileId];
            ++s.games; s.hands += p.hands; s.rebuys += p.rebuys; s.netChips += p.netChips;
            s.totalNet += p.net; s.placeSum += p.place;
            if (!cfg.isCash()) { s.totalBuyin += cfg.buyin; if (p.place == 1) ++s.wins; }
            else s.totalBuyin += cfg.buyin * (1 + p.rebuys);
        }
    }
    out.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return out;
}

static std::string cardsStr(const std::vector<Card>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += ' '; s += cardToString(v[i]); }
    return s;
}

std::string formatHandHistory(const HandRecord& h, const std::vector<std::string>& names, GameFormat f) {
    std::ostringstream o;
    o << "Hand #" << h.game << "-" << h.hand << " [" << formatName(f) << "] blinds " << h.sb << "/" << h.bb;
    if (h.ante) o << " ante " << h.ante;
    o << " (level " << h.level << ")\n";
    for (auto& s : h.seats)
        o << "  Seat " << s.chair << (s.chair == h.dealer ? " (BTN) " : " ") << names[s.chair] << " (" << s.startStack
          << ") [" << cardToString(s.cards[0]) << " " << cardToString(s.cards[1]) << "]\n";
    int rnd = 0;
    static const char* rn[] = {"", "PREFLOP", "FLOP", "TURN", "RIVER"};
    for (auto& a : h.actions) {
        if (a.kind != ActKind::Ante && a.round != rnd) {
            rnd = a.round;
            o << "*** " << rn[rnd] << " ***";
            if (rnd >= FLOP) { std::vector<Card> b(h.board.begin(), h.board.begin() + (rnd == FLOP ? 3 : rnd == TURN ? 4 : 5)); o << " [" << cardsStr(b) << "]"; }
            o << "\n";
        }
        static const char* kn[] = {"ante", "posts SB", "posts BB", "folds", "checks", "calls", "bets", "raises to"};
        o << "  " << names[a.chair] << " " << kn[static_cast<int>(a.kind)];
        if (a.kind == ActKind::Call || a.kind == ActKind::Ante || a.kind == ActKind::SmallBlind || a.kind == ActKind::BigBlind) o << " " << a.amount;
        else if (a.kind == ActKind::Bet || a.kind == ActKind::Raise) o << " " << a.to;
        if (a.allin) o << " (all-in)";
        o << "\n";
    }
    if (h.showdown) o << "*** SHOWDOWN *** board [" << cardsStr(h.board) << "]\n";
    for (auto& s : h.seats) if (s.collected > 0) o << "  " << names[s.chair] << " collects " << s.collected << (s.handType.empty() ? "" : " with " + s.handType) << "\n";
    if (h.rake) o << "  rake " << h.rake << "\n";
    return o.str();
}

std::string formatSummary(const SimConfig& cfg, const SimSummary& s) {
    char buf[256];
    std::ostringstream o;
    long long hands = 0;
    for (auto& g : s.games) hands += g.hands;
    std::snprintf(buf, sizeof buf, "Format %s, %d joueurs, %zu parties/sessions, %lld mains en %.2fs (%.0f mains/s)\n",
                  formatName(cfg.format), cfg.numPlayers, s.games.size(), hands, s.seconds, s.seconds > 0 ? hands / s.seconds : 0.0);
    o << buf;
    if (cfg.isCash()) {
        o << "Joueur                  mains   net(jetons)    bb/100   recaves\n";
        for (auto& p : s.profiles) {
            std::snprintf(buf, sizeof buf, "%-20s %8d %13lld %9.2f %9d\n", p.name.c_str(), p.hands, p.netChips, p.bb100(cfg.cashBb), p.rebuys);
            o << buf;
        }
    } else {
        o << "Joueur                  parties  victoires  place moy.    net        ROI%\n";
        for (auto& p : s.profiles) {
            std::snprintf(buf, sizeof buf, "%-20s %8d %10d %11.3f %9.2f %10.2f\n", p.name.c_str(), p.games, p.wins, p.avgPlace(), p.totalNet, p.roi());
            o << buf;
        }
    }
    return o.str();
}

}  // namespace ps
