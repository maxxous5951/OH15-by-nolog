#include "pokersim/config.hpp"
#include <fstream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include "nlohmann/json.hpp"

namespace ps {

using json = nlohmann::json;

std::vector<BlindLevel> defaultSpinLevels() {
    static const int v[][2] = {{10,20},{15,30},{20,40},{30,60},{40,80},{50,100},{60,120},{80,160},{100,200},{150,300},
                               {200,400},{300,600},{400,800},{500,1000},{600,1200},{800,1600},{1000,2000},{1500,3000},
                               {2000,4000},{3000,6000}};
    std::vector<BlindLevel> out;
    for (auto& p : v) out.push_back({p[0], p[1], 0});
    return out;
}

std::vector<BlindLevel> defaultSngLevels() {
    static const int v[][3] = {{10,20,0},{15,30,0},{25,50,0},{50,100,0},{75,150,0},{100,200,25},{150,300,25},
                               {200,400,50},{300,600,75},{400,800,100},{500,1000,100},{600,1200,200},{800,1600,200},
                               {1000,2000,300},{1500,3000,400},{2000,4000,500}};
    std::vector<BlindLevel> out;
    for (auto& p : v) out.push_back({p[0], p[1], p[2]});
    return out;
}

std::vector<std::pair<double, int>> defaultSpinMultipliers() {
    return {{1000, 5}, {100, 100}, {20, 500}, {10, 5000}, {5, 40000}, {4, 130000}, {3, 346200}, {2, 478195}};
}

std::vector<double> defaultPayoutShares(int n) {
    if (n <= 2) return {1.0};
    if (n == 3) return {0.75, 0.15, 0.10};
    if (n <= 6) return {0.65, 0.35};
    return {0.50, 0.30, 0.20};
}

const char* formatName(GameFormat f) {
    switch (f) { case GameFormat::Cash: return "cash"; case GameFormat::SNG: return "sng"; default: return "spin"; }
}

void SimConfig::finalize() {
    auto fail = [](const std::string& m) { throw std::runtime_error(m); };
    if (format == GameFormat::Cash) {
        if (numPlayers < 2 || numPlayers > 10) fail("Cash game : 2 à 10 joueurs");
        if (cashSb <= 0 || cashBb < cashSb) fail("Blinds cash invalides");
        if (cashStack < 2 * cashBb) fail("Le tapis cash doit valoir au moins 2 big blinds");
        if (cashHands < 1) fail("Nombre de mains par session invalide");
        if (rakePct < 0 || rakePct > 10) fail("Rake : 0 à 10 %");
        startingStack = cashStack;
        buyin = cashStack * chipValue;
    } else {
        if (format == GameFormat::SpinGo && (numPlayers < 2 || numPlayers > 3)) fail("Spin & Go : 2 ou 3 joueurs");
        if (numPlayers < 2 || numPlayers > 10) fail("SNG : 2 à 10 joueurs");
        if (startingStack <= 0) fail("Tapis de départ invalide");
        if (levels.empty()) levels = (format == GameFormat::SpinGo) ? defaultSpinLevels() : defaultSngLevels();
        for (auto& l : levels) if (l.sb <= 0 || l.bb < l.sb || l.ante < 0) fail("Niveau de blinds invalide");
        if (levelSeconds <= 0 || levelHands <= 0) fail("Durée de niveau invalide");
        if (format == GameFormat::SpinGo) {
            if (multipliers.empty()) multipliers = defaultSpinMultipliers();
            if (payoutShares.empty()) payoutShares = defaultPayoutShares(numPlayers);
        } else if (payoutShares.empty()) payoutShares = defaultPayoutShares(numPlayers);
        double s = std::accumulate(payoutShares.begin(), payoutShares.end(), 0.0);
        if (std::fabs(s - 1.0) > 1e-6) fail("La répartition des gains doit faire 100 %");
        if ((int)payoutShares.size() > numPlayers) fail("Plus de places payées que de joueurs");
    }
    if (numGames < 1) fail("Nombre de parties invalide");
    if (prwinIterations < 1) fail("prwinIterations invalide");
}

SimConfig SimConfig::fromJsonText(const std::string& text) {
    json j = json::parse(text, nullptr, true, true);
    SimConfig c;
    auto get = [&](const char* k, auto& dst) { if (j.contains(k)) dst = j[k].get<std::decay_t<decltype(dst)>>(); };
    if (j.contains("format")) {
        std::string f = j["format"].get<std::string>();
        if (f == "cash") c.format = GameFormat::Cash;
        else if (f == "sng") c.format = GameFormat::SNG;
        else if (f == "spin" || f == "spingo") c.format = GameFormat::SpinGo;
        else throw std::runtime_error("format inconnu : " + f);
    }
    get("numPlayers", c.numPlayers); get("numGames", c.numGames); get("seed", c.seed);
    get("startingStack", c.startingStack); get("buyin", c.buyin); get("feePct", c.feePct);
    get("rotateSeats", c.rotateSeats); get("levelSeconds", c.levelSeconds); get("levelHands", c.levelHands);
    get("maxHandsPerGame", c.maxHandsPerGame);
    get("handBaseSeconds", c.handBaseSeconds); get("actionSeconds", c.actionSeconds); get("showdownSeconds", c.showdownSeconds);
    get("cashSb", c.cashSb); get("cashBb", c.cashBb); get("cashAnte", c.cashAnte); get("cashHands", c.cashHands);
    get("cashStack", c.cashStack); get("chipValue", c.chipValue); get("rakePct", c.rakePct); get("rakeCap", c.rakeCap);
    get("noFlopNoDrop", c.noFlopNoDrop); get("payoutShares", c.payoutShares);
    get("multiplierTimesTotal", c.multiplierTimesTotal);
    get("prwinIterations", c.prwinIterations); get("prwinMaxIterations", c.prwinMaxIterations);
    get("workers", c.workers); get("outputDir", c.outputDir); get("writeHandHistory", c.writeHandHistory);
    if (j.contains("levelBy")) c.levelBy = (j["levelBy"].get<std::string>() == "hands") ? LevelBy::Hands : LevelBy::Time;
    if (j.contains("cashStackMode")) c.cashStackMode = (j["cashStackMode"].get<std::string>() == "reset") ? CashStackMode::Reset : CashStackMode::Rebuy;
    if (j.contains("levels"))
        for (auto& l : j["levels"]) c.levels.push_back({l[0].get<int>(), l[1].get<int>(), l.size() > 2 ? l[2].get<int>() : 0});
    if (j.contains("multipliers"))
        for (auto& m : j["multipliers"]) c.multipliers.push_back({m[0].get<double>(), m[1].get<int>()});
    if (j.contains("players"))
        for (auto& p : j["players"]) c.players.push_back({p.value("name", ""), p.value("profile", "builtin:tag")});
    return c;
}

SimConfig SimConfig::fromJsonFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Impossible d'ouvrir " + path);
    std::stringstream ss; ss << f.rdbuf();
    return fromJsonText(ss.str());
}

}  // namespace ps
