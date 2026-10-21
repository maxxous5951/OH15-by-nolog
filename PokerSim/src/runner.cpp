#include "pokersim/runner.hpp"
#include "pokersim/pathutil.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace ps {

SimSummary runSimulation(const SimConfig& cfg, const BrainFactory& factory, bool collectRecords,
                         const std::function<void(int, int)>& progress, std::atomic<bool>* cancel) {
    const int np = static_cast<int>(cfg.players.size());
    if (np != cfg.numPlayers) throw std::runtime_error("Le nombre de profils déclarés ne correspond pas à numPlayers");
    SimSummary out;
    out.games.resize(cfg.numGames);
    // historique complet des mains : écrit au fil de l'eau, dans l'ordre des parties (même si les threads finissent en désordre)
    struct HandWriter {
        std::ofstream f;
        std::mutex mu;
        std::map<int, std::string> pending;
        int next = 0;
        long long bytes = 0, hands = 0;
        void write(const std::string& t) { f << t; bytes += static_cast<long long>(t.size()); }
        void submit(int g, std::string text, long long nHands) {
            std::lock_guard<std::mutex> lk(mu);
            hands += nHands;
            pending[g] = std::move(text);
            while (!pending.empty() && pending.begin()->first == next) { write(pending.begin()->second); pending.erase(pending.begin()); ++next; }
        }
        void finish() { std::lock_guard<std::mutex> lk(mu); for (auto& kv : pending) write(kv.second); pending.clear(); f.flush(); }
    } writer;
    const bool hhOn = !cfg.handHistoryPath.empty();
    const int hhLimit = cfg.handHistoryGames > 0 ? cfg.handHistoryGames : cfg.numGames;
    if (hhOn) {
        writer.f.open(pathFromUtf8(cfg.handHistoryPath), std::ios::binary | std::ios::trunc);
        if (!writer.f) throw std::runtime_error("Impossible d'écrire l'historique des mains : " + cfg.handHistoryPath);
        static const char bom[] = "\xEF\xBB\xBF";                 // BOM UTF-8 : accents corrects dans le Bloc-notes
        writer.f.write(bom, 3);
        writer.bytes += 3;
    }
    std::atomic<int> next{0}, done{0};
    std::atomic<bool> failed{false};
    std::mutex errMu;
    std::string error;
    auto t0 = std::chrono::steady_clock::now();
    auto worker = [&]() {
        for (;;) {
            int g = next.fetch_add(1);
            if (g >= cfg.numGames || failed.load() || (cancel && cancel->load())) return;
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
                const bool toFile = hhOn && g < hhLimit;
                const bool keep = collectRecords || g < cfg.recordGames;
                Game game(cfg, brains, names, perm, g, gameSeed, keep || toFile);
                out.games[g] = game.play();
                if (toFile) writer.submit(g, formatGameHistory(out.games[g], cfg), static_cast<long long>(out.games[g].records.size()));
                if (!keep) { out.games[g].records.clear(); out.games[g].records.shrink_to_fit(); }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lk(errMu);
                if (error.empty()) error = e.what();
                failed = true;
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
    if (hhOn) writer.finish();
    if (failed.load()) throw std::runtime_error(error);

    // parties non jouées (annulation) : retirées pour que les statistiques ne portent que sur les parties terminées
    out.cancelled = cancel && cancel->load();
    out.games.erase(std::remove_if(out.games.begin(), out.games.end(), [](const GameResult& g) { return g.players.empty(); }), out.games.end());
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
    if (hhOn) { out.handHistoryPath = cfg.handHistoryPath; out.handHistoryHands = writer.hands; out.handHistoryBytes = writer.bytes; }
    return out;
}

static std::string cardsStr(const std::vector<Card>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += ' '; s += cardToString(v[i]); }
    return s;
}

static std::string clock(double sec) {
    long long t = static_cast<long long>(sec);
    char b[32];
    std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld", t / 3600, (t / 60) % 60, t % 60);
    return b;
}

static const char* streetName(int r) {
    static const char* n[] = {"", "PRÉFLOP", "FLOP", "TURN", "RIVER"};
    return n[r];
}
static const char* streetShort(int r) {
    static const char* n[] = {"", "préflop", "flop", "turn", "river"};
    return n[r];
}
static std::string handTypeFr(const std::string& en) {
    static const std::pair<const char*, const char*> t[] = {{"high card", "carte haute"}, {"a pair", "une paire"}, {"two pair", "deux paires"},
        {"three of a kind", "un brelan"}, {"a straight", "une quinte"}, {"a flush", "une couleur"}, {"a full house", "un full"},
        {"four of a kind", "un carré"}, {"a straight flush", "une quinte flush"}};
    for (auto& p : t) if (en == p.first) return p.second;
    return en;
}
static int boardStreet(size_t nBoard) { return nBoard >= 5 ? RIVER : nBoard == 4 ? TURN : nBoard == 3 ? FLOP : PREFLOP; }

std::string formatHandHistory(const HandRecord& h, const std::vector<std::string>& names, GameFormat f) {
    std::ostringstream o;
    auto nm = [&](int c) { return c >= 0 && c < static_cast<int>(names.size()) ? names[static_cast<size_t>(c)] : "Siège " + std::to_string(c); };
    const char* fmt = f == GameFormat::Cash ? "Cash game" : f == GameFormat::SNG ? "Sit & Go" : "Spin & Go";
    o << "---- Main " << h.game + 1 << "-" << h.hand << " ----\n";
    o << fmt << " | blinds " << h.sb << "/" << h.bb;
    if (h.ante) o << " | ante " << h.ante;
    if (f != GameFormat::Cash) o << " | niveau " << h.level;
    o << " | temps simulé " << clock(h.time) << " | bouton : " << nm(h.dealer) << "\n";
    o << "Sièges :\n";
    for (const SeatRecord& s : h.seats) {
        o << "  Siège " << s.chair << " : " << nm(s.chair) << " (" << s.startStack << ") [" << cardToString(s.cards[0]) << " " << cardToString(s.cards[1]) << "]";
        if (s.chair == h.dealer) o << "  <bouton>";
        if (s.chair == h.sbChair) o << "  <petite blind>";
        if (s.chair == h.bbChair) o << "  <grosse blind>";
        o << "\n";
    }
    long long pot = 0;
    int shown = 0;                                   // dernière street dont l'en-tête a été écrit
    bool blindsHeader = false;
    std::vector<int> foldStreet(64, 0);
    auto header = [&](int r) {
        o << "*** " << streetName(r) << " ***";
        if (r >= FLOP) {
            std::vector<Card> b(h.board.begin(), h.board.begin() + static_cast<long>(std::min<size_t>(h.board.size(), r == FLOP ? 3u : r == TURN ? 4u : 5u)));
            o << " [" << cardsStr(b) << "]";
        }
        o << " (pot : " << pot << ")\n";
    };
    for (const Action& a : h.actions) {
        if (a.kind == ActKind::Ante || a.kind == ActKind::SmallBlind || a.kind == ActKind::BigBlind) {
            pot += a.amount;
            if (!blindsHeader) { o << "*** MISES FORCÉES ***\n"; blindsHeader = true; }
            o << "  " << nm(a.chair) << (a.kind == ActKind::Ante ? " paie l'ante " : a.kind == ActKind::SmallBlind ? " poste la petite blind " : " poste la grosse blind ") << a.amount;
            if (a.allin) o << " (all-in)";
            o << "\n";
            continue;
        }
        while (shown < a.round) { ++shown; header(shown); }
        pot += a.amount;
        o << "  " << nm(a.chair);
        switch (a.kind) {
            case ActKind::Fold: o << " se couche"; if (a.chair >= 0 && a.chair < 64) foldStreet[static_cast<size_t>(a.chair)] = a.round; break;
            case ActKind::Check: o << " checke"; break;
            case ActKind::Call: o << " paie " << a.amount; break;
            case ActKind::Bet: o << " mise " << a.to; break;
            default: o << " relance à " << a.to; break;
        }
        if (a.allin && a.kind != ActKind::Fold) o << " (all-in)";
        o << "\n";
    }
    if (shown == 0) { shown = PREFLOP; header(PREFLOP); }
    if (h.showdown) {                                // all-in : les streets restantes sont distribuées sans action
        for (int r = shown + 1; r <= boardStreet(h.board.size()); ++r) { header(r); o << "  (aucune action : tapis)\n"; shown = r; }
    }
    if (h.showdown) {
        o << "*** SHOWDOWN *** board [" << cardsStr(h.board) << "]\n";
        for (const SeatRecord& s : h.seats)
            if (!s.folded) o << "  " << nm(s.chair) << " montre [" << cardToString(s.cards[0]) << " " << cardToString(s.cards[1]) << "] : " << handTypeFr(s.handType) << "\n";
    }
    o << "*** POTS ***\n";
    int potNo = 0;
    for (const PotRecord& p : h.pots) {
        if (p.uncalled) { o << "  Mise non payée " << p.amount << " rendue à " << (p.winners.empty() ? "?" : nm(p.winners[0])) << "\n"; continue; }
        o << "  " << (potNo == 0 ? "Pot principal " : "Pot annexe " + std::to_string(potNo) + " ") << p.amount << " : ";
        if (p.winners.size() > 1) { o << "partagé entre "; for (size_t i = 0; i < p.winners.size(); ++i) o << (i ? ", " : "") << nm(p.winners[i]); }
        else if (!p.winners.empty()) o << "gagné par " << nm(p.winners[0]);
        if (!h.showdown) o << " (tous les autres se sont couchés)";
        o << "\n";
        ++potNo;
    }
    if (h.rake) o << "  Rake : " << h.rake << "\n";
    o << "*** RÉSUMÉ ***\n";
    for (const SeatRecord& s : h.seats) {
        const int net = s.collected - s.committed;
        o << "  " << nm(s.chair) << " : misé " << s.committed << ", ramassé " << s.collected << ", net " << (net > 0 ? "+" : "") << net << ", stack final " << s.endStack;
        if (s.folded) { int st = (s.chair >= 0 && s.chair < 64) ? foldStreet[static_cast<size_t>(s.chair)] : 0; o << " - couché" << (st ? std::string(" au ") + streetShort(st) : ""); }
        else if (h.showdown) o << " - au showdown" << (s.collected > 0 ? " (gagnant)" : "");
        else o << " - gagne sans showdown";
        if (s.endStack == 0 && s.startStack > 0) o << " - tapis à zéro";
        o << "\n";
    }
    return o.str();
}

std::string formatGameHistory(const GameResult& g, const SimConfig& cfg) {
    std::ostringstream o;
    std::vector<std::string> names;
    for (const PlayerResult& p : g.players) names.push_back(p.name);
    const bool cash = g.format == GameFormat::Cash;
    o << "################ " << (cash ? "SESSION " : "PARTIE ") << g.game + 1 << " ################\n";
    o << "Joueurs : ";
    for (size_t i = 0; i < names.size(); ++i) o << (i ? ", " : "") << "siège " << i << " " << names[i];
    o << "\n";
    if (!cash) {
        o << "Buy-in " << cfg.buyin << " | cagnotte " << g.prizepool;
        if (g.format == GameFormat::SpinGo) o << " (multiplicateur x" << g.multiplier << ")";
        o << "\n";
    }
    o << "\n";
    for (const HandRecord& h : g.records) o << formatHandHistory(h, names, g.format) << "\n";
    o << "=== " << (cash ? "Résultat de la session " : "Résultat de la partie ") << g.game + 1 << " (" << g.hands << (g.hands > 1 ? " mains" : " main") << ") ===\n";
    std::vector<const PlayerResult*> order;
    for (const PlayerResult& p : g.players) order.push_back(&p);
    std::sort(order.begin(), order.end(), [](const PlayerResult* a, const PlayerResult* b) { return a->place < b->place; });
    char buf[200];
    for (const PlayerResult* p : order) {
        if (cash) std::snprintf(buf, sizeof buf, "  %d. %s : net %lld jetons (%.2f)%s\n", p->place, p->name.c_str(), p->netChips, p->gain, p->rebuys ? (" - " + std::to_string(p->rebuys) + " recave(s)").c_str() : "");
        else std::snprintf(buf, sizeof buf, "  %d. %s : gain %.2f (net %.2f)\n", p->place, p->name.c_str(), p->gain, p->net);
        o << buf;
    }
    o << "\n\n";
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
