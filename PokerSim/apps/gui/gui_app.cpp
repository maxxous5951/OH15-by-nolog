#include "gui_app.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>
#include "imgui.h"
#include "misc/imgui_stdlib.h"
#include "nlohmann/json.hpp"
#include "pokersim/pathutil.hpp"

namespace psgui {
using json = nlohmann::json;

namespace {
double sampleSd(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    double m = 0;
    for (double x : v) m += x;
    m /= static_cast<double>(v.size());
    double s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return std::sqrt(s / static_cast<double>(v.size() - 1));
}
const char* kindProfile(PlayerKind k) {
    switch (k) { case PlayerKind::BuiltinTag: return "builtin:tag"; case PlayerKind::BuiltinCall: return "builtin:call"; case PlayerKind::BuiltinRandom: return "builtin:random"; default: return ""; }
}
std::string csvEscape(const std::string& s) {
    if (s.find_first_of(",\"\n") == std::string::npos) return s;
    std::string o = "\"";
    for (char c : s) { if (c == '"') o += '"'; o += c; }
    return o + "\"";
}
}  // namespace

// ============================================================ construction, persistance
App::App(Platform& platform, std::string settingsPath) : platform_(platform), settingsPath_(std::move(settingsPath)) {
    resetPlayers();
    cfg.numGames = 200;
    setFormat(ps::GameFormat::SpinGo);
    ohRoot = ps::oh::findOhRoot(platform_.exeDir());
    if (ohRoot.empty()) ohRoot = ps::oh::findOhRoot(".");
    if (!settingsPath_.empty()) loadSettings();
}

App::~App() {
    cancel_ = true;
    if (worker_.joinable()) worker_.join();
    if (!settingsPath_.empty()) saveSettings();
}

void App::resetPlayers() {
    static const PlayerKind cycle[3] = {PlayerKind::BuiltinTag, PlayerKind::BuiltinCall, PlayerKind::BuiltinRandom};
    players.assign(10, PlayerRow{});
    for (int i = 0; i < 10; ++i) { players[i].name = "Joueur " + std::to_string(i + 1); players[i].kind = cycle[i % 3]; }
}

void App::setFormat(ps::GameFormat f) {
    cfg.format = f;
    cfg.levels.clear(); cfg.multipliers.clear(); cfg.payoutShares.clear();
    if (f == ps::GameFormat::Cash) {
        cfg.numPlayers = 6; cfg.numGames = std::min(cfg.numGames, 200);
    } else if (f == ps::GameFormat::SNG) {
        cfg.numPlayers = 9; cfg.startingStack = 1500; cfg.buyin = 10.0; cfg.feePct = 10.0;
        cfg.levels = ps::defaultSngLevels(); cfg.payoutShares = ps::defaultPayoutShares(9);
    } else {
        cfg.numPlayers = 3; cfg.startingStack = 500; cfg.buyin = 1.0; cfg.feePct = 0.0;
        cfg.levels = ps::defaultSpinLevels(); cfg.multipliers = ps::defaultSpinMultipliers(); cfg.payoutShares = ps::defaultPayoutShares(3);
    }
}

bool App::saveConfigFile(const std::string& path) const {
    try {
        json j;
        j["config"] = json::parse(cfg.toJsonText());
        j["ohRoot"] = ohRoot;
        j["players"] = json::array();
        for (auto& p : players) j["players"].push_back({{"name", p.name}, {"kind", static_cast<int>(p.kind)}, {"path", p.path}});
        std::ofstream f(ps::pathFromUtf8(path));
        if (!f) return false;
        f << j.dump(2);
        return static_cast<bool>(f);
    } catch (...) { return false; }
}

bool App::loadConfigFile(const std::string& path) {
    try {
        std::ifstream f(ps::pathFromUtf8(path));
        if (!f) return false;
        std::stringstream ss; ss << f.rdbuf();
        json j = json::parse(ss.str());
        ps::SimConfig c = ps::SimConfig::fromJsonText(j.at("config").dump());
        if (c.format != ps::GameFormat::Cash) {
            if (c.levels.empty()) c.levels = c.format == ps::GameFormat::SpinGo ? ps::defaultSpinLevels() : ps::defaultSngLevels();
            if (c.format == ps::GameFormat::SpinGo && c.multipliers.empty()) c.multipliers = ps::defaultSpinMultipliers();
            if (c.payoutShares.empty()) c.payoutShares = ps::defaultPayoutShares(c.numPlayers);
        }
        cfg = c;
        if (j.contains("ohRoot") && j["ohRoot"].is_string() && !j["ohRoot"].get<std::string>().empty()) ohRoot = j["ohRoot"].get<std::string>();
        if (j.contains("players")) {
            resetPlayers();
            size_t i = 0;
            for (auto& jp : j["players"]) {
                if (i >= players.size()) break;
                players[i].name = jp.value("name", players[i].name);
                players[i].kind = static_cast<PlayerKind>(std::clamp(jp.value("kind", 0), 0, 3));
                players[i].path = jp.value("path", "");
                ++i;
            }
        }
        return true;
    } catch (...) { return false; }
}

bool App::saveSettings() const { return !settingsPath_.empty() && saveConfigFile(settingsPath_); }
bool App::loadSettings() { return !settingsPath_.empty() && loadConfigFile(settingsPath_); }

// ============================================================ exécution
ps::SimConfig App::buildRunConfig(std::string& error) const {
    ps::SimConfig c = cfg;
    error.clear();
    c.players.clear();
    for (int i = 0; i < c.numPlayers && i < static_cast<int>(players.size()); ++i) {
        const PlayerRow& r = players[i];
        ps::PlayerSpec sp;
        sp.name = r.name.empty() ? "Joueur " + std::to_string(i + 1) : r.name;
        if (r.kind == PlayerKind::ProfileFile) {
            if (r.path.empty()) { error = "Joueur " + std::to_string(i + 1) + " (" + sp.name + ") : aucun fichier de profil choisi."; return c; }
            sp.profile = r.path;
        } else sp.profile = kindProfile(r.kind);
        c.players.push_back(sp);
    }
    try { c.finalize(); } catch (const std::exception& e) { error = e.what(); }
    return c;
}

bool App::startRun() {
    poll();
    if (running_) return false;
    std::string err;
    ps::SimConfig c = buildRunConfig(err);
    if (!err.empty()) { error_ = err; return false; }
    error_.clear();
    cancel_ = false; finished_ = false; done_ = 0; total_ = c.numGames; running_ = true;
    t0_ = std::chrono::steady_clock::now();
    saveSettings();
    const std::string root = ohRoot;
    worker_ = std::thread([this, c, root]() {
        std::unique_ptr<ResultsView> res;
        std::string error;
        try {
            ps::oh::collectUnknownSymbols(true);
            ps::SimSummary s = ps::runSimulation(c, ps::oh::profileBrainFactory(root), false, [this](int d, int) { done_.store(d); }, &cancel_);
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
            res = makeResults(std::move(s), c, secs);
            for (auto& kv : ps::oh::collectUnknownSymbols(true)) res->unknownSymbols.push_back({kv.second, kv.first});
            std::sort(res->unknownSymbols.rbegin(), res->unknownSymbols.rend());
        } catch (const std::exception& e) { error = e.what(); }
        { std::lock_guard<std::mutex> lk(mu_); pending_ = std::move(res); pendingError_ = error; }
        finished_ = true;
    });
    return true;
}

void App::stopRun() { cancel_ = true; }

void App::poll() {
    if (!running_ || !finished_) return;
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lk(mu_);
    if (pending_) { results_ = std::move(pending_); handGame = handIndex = 0; }
    error_ = pendingError_;
    running_ = false;
}

std::unique_ptr<ResultsView> App::makeResults(ps::SimSummary&& s, const ps::SimConfig& cfg, double seconds) {
    auto r = std::make_unique<ResultsView>();
    r->summary = std::move(s);
    r->cfg = cfg;
    r->seconds = seconds;
    r->cancelled = r->summary.cancelled;
    const int np = static_cast<int>(cfg.players.size());
    r->players.resize(np);
    r->cumulative.assign(np, {});
    std::vector<std::vector<double>> nets(np), bb100s(np);
    std::vector<double> cum(np, 0.0);
    for (int i = 0; i < np; ++i) { r->players[i].name = cfg.players[i].name; r->players[i].placeCount.assign(10, 0); }
    for (const ps::GameResult& g : r->summary.games) {
        r->totalHands += g.hands;
        for (const ps::PlayerResult& p : g.players) {
            if (p.profileId < 0 || p.profileId >= np) continue;
            PlayerView& v = r->players[p.profileId];
            ++v.games; v.hands += p.hands; v.rebuys += p.rebuys; v.netChips += p.netChips; v.net += p.net; v.avgPlace += p.place;
            if (p.place >= 1 && p.place <= 10) ++v.placeCount[p.place - 1];
            nets[p.profileId].push_back(p.net);
            bb100s[p.profileId].push_back(p.bb100);
            cum[p.profileId] += p.net;
            r->cumulative[p.profileId].push_back(static_cast<float>(cum[p.profileId]));
        }
    }
    for (int i = 0; i < np; ++i) {
        PlayerView& v = r->players[i];
        if (!v.games) continue;
        v.avgPlace /= v.games;
        v.wins = v.placeCount[0];
        const double n = v.games;
        if (!cfg.isCash() && cfg.buyin > 0) {
            v.roi = 100.0 * v.net / (n * cfg.buyin);
            v.roiCi = 1.96 * sampleSd(nets[i]) / std::sqrt(n) / cfg.buyin * 100.0;
        } else if (cfg.isCash()) {
            v.bb100 = v.hands ? 100.0 * static_cast<double>(v.netChips) / cfg.cashBb / static_cast<double>(v.hands) : 0.0;
            v.bb100Ci = 1.96 * sampleSd(bb100s[i]) / std::sqrt(n);
        }
    }
    return r;
}

// ============================================================ export
std::string App::summaryText() const {
    if (!results_) return "";
    const ResultsView& r = *results_;
    char buf[512];
    std::ostringstream o;
    std::snprintf(buf, sizeof buf, "PokerSim - %s, %d joueurs, %zu %s, %lld mains, %.1f s%s\n", ps::formatName(r.cfg.format), r.cfg.numPlayers,
                  r.summary.games.size(), r.cfg.isCash() ? "sessions" : "parties", r.totalHands, r.seconds, r.cancelled ? " (interrompu)" : "");
    o << buf;
    for (const PlayerView& p : r.players) {
        if (r.cfg.isCash()) std::snprintf(buf, sizeof buf, "%-22s sessions %4d  mains %8lld  net %10.2f  bb/100 %8.2f +/- %.2f\n", p.name.c_str(), p.games, p.hands, p.net, p.bb100, p.bb100Ci);
        else std::snprintf(buf, sizeof buf, "%-22s parties %5d  victoires %5d  place moy. %.3f  net %10.2f  ROI %8.2f%% +/- %.2f\n", p.name.c_str(), p.games, p.wins, p.avgPlace, p.net, p.roi, p.roiCi);
        o << buf;
    }
    return o.str();
}

bool App::exportCsv(const std::string& path) const {
    if (!results_) return false;
    const ResultsView& r = *results_;
    std::ofstream f(ps::pathFromUtf8(path));
    if (!f) return false;
    const int np = static_cast<int>(r.players.size());
    f << "partie,mains,secondes,cagnotte,multiplicateur";
    for (int i = 0; i < np; ++i) f << ',' << csvEscape(r.players[i].name + " place") << ',' << csvEscape(r.players[i].name + " net");
    f << '\n';
    for (const ps::GameResult& g : r.summary.games) {
        std::vector<const ps::PlayerResult*> byProfile(np, nullptr);
        for (const ps::PlayerResult& p : g.players) if (p.profileId >= 0 && p.profileId < np) byProfile[p.profileId] = &p;
        f << g.game + 1 << ',' << g.hands << ',' << g.seconds << ',' << g.prizepool << ',' << g.multiplier;
        for (int i = 0; i < np; ++i) { if (byProfile[i]) f << ',' << byProfile[i]->place << ',' << byProfile[i]->net; else f << ",,"; }
        f << '\n';
    }
    return static_cast<bool>(f);
}

ps::oh::ProfileReport& App::report(const std::string& path) {
    auto it = reports_.find(path);
    if (it == reports_.end()) it = reports_.emplace(path, ps::oh::inspectProfile(path, ohRoot)).first;
    return it->second;
}

void App::checkOhRoot() {
    if (ohRootChecked_ == ohRoot) return;
    ohRootChecked_ = ohRoot;
    std::error_code ec;
    ohRootOk_ = !ohRoot.empty() && std::filesystem::is_directory(ps::pathFromUtf8(ohRoot) / "OpenPPL" / "OpenPPL_Library", ec);
}

}  // namespace psgui
