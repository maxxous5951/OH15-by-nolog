#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include "pokersim/runner.hpp"

using namespace ps;

static void usage() {
    std::puts(
        "pokersim - simulateur Hold'em (cash / SNG / Spin & Go)\n\n"
        "  pokersim [--config fichier.json] [options]\n\n"
        "  --format cash|sng|spin     --players N        --games N (parties ou sessions)\n"
        "  --seed N                   --stack N          --buyin X\n"
        "  --level-seconds N          --level-hands N    --by hands|time\n"
        "  --hands N                  (cash : mains par session)\n"
        "  --sb N --bb N              (cash : blinds)    --rake PCT   --rake-cap N\n"
        "  --player Nom=profil        (répétable ; profil = builtin:tag|call|random)\n"
        "  --workers N                --hh fichier.txt   (historique des mains, 1ère partie)\n");
}

int main(int argc, char** argv) {
    try {
        SimConfig cfg;
        std::string cfgPath, hhPath;
        for (int i = 1; i < argc; ++i) if (!std::strcmp(argv[i], "--config") && i + 1 < argc) cfgPath = argv[i + 1];
        if (!cfgPath.empty()) cfg = SimConfig::fromJsonFile(cfgPath);
        std::vector<PlayerSpec> cliPlayers;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            auto val = [&]() -> const char* { if (i + 1 >= argc) throw std::runtime_error("Valeur manquante pour " + a); return argv[++i]; };
            if (a == "--config") val();
            else if (a == "-h" || a == "--help") { usage(); return 0; }
            else if (a == "--format") {
                std::string f = val();
                cfg.format = f == "cash" ? GameFormat::Cash : f == "sng" ? GameFormat::SNG : GameFormat::SpinGo;
            }
            else if (a == "--players") cfg.numPlayers = std::atoi(val());
            else if (a == "--games") cfg.numGames = std::atoi(val());
            else if (a == "--seed") cfg.seed = std::strtoull(val(), nullptr, 10);
            else if (a == "--stack") { cfg.startingStack = cfg.cashStack = std::atoi(val()); }
            else if (a == "--buyin") cfg.buyin = std::atof(val());
            else if (a == "--level-seconds") cfg.levelSeconds = std::atoi(val());
            else if (a == "--level-hands") cfg.levelHands = std::atoi(val());
            else if (a == "--by") cfg.levelBy = std::string(val()) == "hands" ? LevelBy::Hands : LevelBy::Time;
            else if (a == "--hands") cfg.cashHands = std::atoi(val());
            else if (a == "--sb") cfg.cashSb = std::atoi(val());
            else if (a == "--bb") cfg.cashBb = std::atoi(val());
            else if (a == "--rake") cfg.rakePct = std::atof(val());
            else if (a == "--rake-cap") cfg.rakeCap = std::atoi(val());
            else if (a == "--workers") cfg.workers = std::atoi(val());
            else if (a == "--hh") hhPath = val();
            else if (a == "--player") {
                std::string v = val();
                auto eq = v.find('=');
                if (eq == std::string::npos) throw std::runtime_error("--player attend Nom=profil");
                cliPlayers.push_back({v.substr(0, eq), v.substr(eq + 1)});
            }
            else { std::fprintf(stderr, "Option inconnue : %s\n", a.c_str()); usage(); return 2; }
        }
        if (!cliPlayers.empty()) { cfg.players = cliPlayers; cfg.numPlayers = static_cast<int>(cliPlayers.size()); }
        if (cfg.players.empty()) {
            static const char* kinds[] = {"tag", "call", "random", "tag", "call", "random", "tag", "call", "random", "tag"};
            for (int i = 0; i < cfg.numPlayers; ++i)
                cfg.players.push_back({std::string(kinds[i]) + std::to_string(i + 1), std::string("builtin:") + kinds[i]});
        }
        cfg.finalize();
        std::mutex mu;
        auto progress = [&](int d, int n) {
            if (n >= 20 && (d % std::max(1, n / 20) == 0 || d == n)) { std::lock_guard<std::mutex> lk(mu); std::fprintf(stderr, "\r%d/%d", d, n); if (d == n) std::fputc('\n', stderr); }
        };
        SimSummary s = runSimulation(cfg, defaultBrainFactory(), !hhPath.empty(), progress);
        std::fputs(formatSummary(cfg, s).c_str(), stdout);
        if (!hhPath.empty() && !s.games.empty()) {
            std::ofstream f(hhPath);
            std::vector<std::string> names;
            for (auto& p : s.games[0].players) names.push_back(p.name);
            for (auto& h : s.games[0].records) f << formatHandHistory(h, names, cfg.format) << "\n";
            std::printf("Historique écrit : %s\n", hhPath.c_str());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Erreur : %s\n", e.what());
        return 1;
    }
    return 0;
}
