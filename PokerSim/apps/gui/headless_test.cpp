// Test de l'interface sans écran : contexte Dear ImGui sans fenêtre, frames simulées, simulations réelles.
//   gui_headless_test [racine_OpenHoldem]   (la racine active les tests avec de vrais profils)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>
#include "gui_app.hpp"
#include "imgui.h"
#include "pokersim/pathutil.hpp"

using namespace psgui;
static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("ECHEC %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

struct FakePlatform : Platform {
    std::string nextOpen, nextSave, nextFolder;
    std::string openFile(const char*, const char*, const char*) override { return nextOpen; }
    std::string saveFile(const char*, const char*, const char*) override { return nextSave; }
    std::string pickFolder(const char*) override { return nextFolder; }
};

static void frames(int n) {
    for (int i = 0; i < n; ++i) {
        ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        // les tests appellent App::frame() via la lambda enregistrée
        extern App* gApp;
        gApp->frame();
        ImGui::Render();
    }
}
App* gApp = nullptr;

static void waitRun(App& app, int maxMs = 120000) {
    auto t0 = std::chrono::steady_clock::now();
    while (app.running()) {
        frames(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() > maxMs) { std::printf("TIMEOUT\n"); ++failures; return; }
    }
    frames(2);
}

static void allTabs() { for (int t = 0; t < 4; ++t) { gApp->requestedTab = t; frames(3); } }

int main(int argc, char** argv) {
    const std::string ohRoot = argc > 1 ? argv[1] : "";
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1400, 900);
    unsigned char* px; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);

    FakePlatform plat;
    const std::string settings = "/tmp/pokersim_gui_test_settings.json";
    std::remove(settings.c_str());
    {
        App app(plat, settings);
        gApp = &app;
        std::puts("onglets vides");
        allTabs();

        std::puts("Spin & Go avec bots intégrés");
        app.cfg.numGames = 30; app.cfg.recordGames = 1; app.cfg.seed = 5; app.cfg.workers = 2;
        for (int i = 0; i < 3; ++i) app.players[i].name = "P" + std::to_string(i);
        CHECK(app.startRun());
        CHECK(app.running());
        CHECK(!app.startRun());                       // déjà en cours
        waitRun(app);
        CHECK(!app.running());
        CHECK(app.lastError().empty());
        CHECK(app.results() != nullptr);
        if (app.results()) {
            const ResultsView& r = *app.results();
            CHECK(r.summary.games.size() == 30);
            CHECK(r.players.size() == 3);
            CHECK(r.totalHands > 30);
            CHECK(r.cumulative.size() == 3 && r.cumulative[0].size() == 30);
            double sumNet = 0; int wins = 0;
            for (auto& p : r.players) { sumNet += p.net; wins += p.wins; CHECK(p.games == 30); }
            CHECK(wins == 30);
            CHECK(!r.summary.games[0].records.empty());   // historique de la 1re partie
            CHECK(r.summary.games.back().records.empty());
            CHECK(sumNet < 0.0 + 1e-9 + 30 * 3 * 1.0);    // plausible
        }
        allTabs();
        CHECK(app.exportCsv("/tmp/pokersim_gui_test.csv"));
        { std::ifstream f("/tmp/pokersim_gui_test.csv"); int lines = 0; std::string l; while (std::getline(f, l)) ++lines; CHECK(lines == 31); }
        CHECK(app.summaryText().find("PokerSim") != std::string::npos);

        std::puts("sauvegarde et rechargement de la configuration");
        app.cfg.seed = 987654321; app.players[1].name = "Zoé"; app.players[2].kind = PlayerKind::ProfileFile; app.players[2].path = "/tmp/x.ohf";
        CHECK(app.saveConfigFile("/tmp/pokersim_gui_test_cfg.json"));
        app.cfg.seed = 1; app.players[1].name = "autre"; app.players[2].kind = PlayerKind::BuiltinTag;
        CHECK(app.loadConfigFile("/tmp/pokersim_gui_test_cfg.json"));
        CHECK(app.cfg.seed == 987654321 && app.players[1].name == "Zoé" && app.players[2].kind == PlayerKind::ProfileFile && app.players[2].path == "/tmp/x.ohf");
        CHECK(!app.loadConfigFile("/tmp/inexistant_pokersim.json"));
        app.players[2].kind = PlayerKind::BuiltinRandom;

        std::puts("erreurs de configuration");
        { PlayerKind k = app.players[0].kind; std::string pth = app.players[0].path;
          app.players[0].kind = PlayerKind::ProfileFile; app.players[0].path.clear();
          CHECK(!app.startRun()); CHECK(!app.lastError().empty()); frames(2);
          app.players[0].kind = k; app.players[0].path = pth; }
        { auto save = app.cfg.payoutShares; app.cfg.payoutShares = {0.5, 0.2};
          CHECK(!app.startRun()); CHECK(!app.lastError().empty());
          app.cfg.payoutShares = save; }
        app.players[0].kind = PlayerKind::ProfileFile; app.players[0].path = "/chemin/inexistant/profil.ohf";
        CHECK(app.startRun());                         // l'erreur survient dans le thread
        waitRun(app);
        CHECK(!app.lastError().empty());
        app.players[0].kind = PlayerKind::BuiltinTag; app.players[0].path.clear();

        std::puts("annulation");
        app.cfg.numGames = 200000; app.cfg.recordGames = 0;
        CHECK(app.startRun());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        app.stopRun();
        waitRun(app);
        CHECK(app.lastError().empty());
        CHECK(app.results() && app.results()->cancelled);
        CHECK(app.results() && app.results()->summary.games.size() > 0 && app.results()->summary.games.size() < 200000);
        allTabs();

        std::puts("cash game");
        app.setFormat(ps::GameFormat::Cash);
        CHECK(app.cfg.numPlayers == 6);
        app.cfg.numPlayers = 4; app.cfg.numGames = 3; app.cfg.cashHands = 300; app.cfg.recordGames = 1;
        CHECK(app.startRun());
        waitRun(app);
        CHECK(app.lastError().empty());
        CHECK(app.results() && app.results()->summary.games.size() == 3 && app.results()->cfg.isCash());
        if (app.results()) { long long h2 = 0; for (auto& p : app.results()->players) h2 += p.hands; CHECK(h2 == 3 * 300 * 4); }
        allTabs();
        app.handIndex = 299; frames(2);

        std::puts("historique complet des mains dans un fichier");
        app.cfg.numGames = 3; app.cfg.cashHands = 200; app.cfg.recordGames = 1;
        app.saveHands = true; app.cfg.handHistoryPath.clear();
        CHECK(!app.startRun());                       // chemin manquant
        CHECK(app.lastError().find("fichier") != std::string::npos);
        app.cfg.handHistoryPath = "/tmp/pokersim_gui_test_mains.txt";
        std::remove(app.cfg.handHistoryPath.c_str());
        CHECK(app.startRun());
        waitRun(app);
        CHECK(app.lastError().empty() && app.results());
        if (app.results()) {
            CHECK(app.results()->summary.handHistoryHands == 3 * 200);
            CHECK(app.results()->summary.handHistoryPath == app.cfg.handHistoryPath);
            std::ifstream f(app.cfg.handHistoryPath, std::ios::binary); std::stringstream ss; ss << f.rdbuf();
            const std::string t = ss.str();
            CHECK(t.find("SESSION 3") != std::string::npos && t.find("*** RÉSUMÉ ***") != std::string::npos);
            CHECK(static_cast<long long>(t.size()) == app.results()->summary.handHistoryBytes);
            CHECK(!app.results()->summary.games[0].records.empty() && app.results()->summary.games[1].records.empty());   // visionneuse : seule la 1re est gardée
        }
        CHECK(app.exportGameHistory("/tmp/pokersim_gui_test_partie.txt", 0));
        { std::ifstream f("/tmp/pokersim_gui_test_partie.txt", std::ios::binary); std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().find("SESSION 1") != std::string::npos); }
        CHECK(!app.exportGameHistory("/tmp/x_inutile.txt", 5));      // pas de 6e partie enregistrée
        allTabs();
        app.saveHands = false;

        std::puts("PokerTracker simulé : base enregistrée puis réutilisée");
        std::remove("/tmp/pokersim_gui_pt.json");
        app.cfg.numGames = 2; app.cfg.cashHands = 120; app.cfg.recordGames = 0; app.saveHands = false;
        app.cfg.ptEnabled = true; app.savePt = true; app.cfg.ptSavePath.clear(); app.cfg.ptBaselinePath.clear();
        CHECK(!app.startRun());                       // chemin de base manquant
        app.cfg.ptSavePath = "/tmp/pokersim_gui_pt.json";
        CHECK(app.startRun());
        waitRun(app);
        CHECK(app.lastError().empty() && app.results() && app.results()->summary.ptDatabase);
        long long first = app.results() ? app.results()->summary.ptDatabase->totalHands(ps::pt::Mode::Cash) : 0;
        CHECK(first == 2 * 120 * 4);                  // 4 joueurs x 120 mains x 2 sessions
        { std::ifstream f("/tmp/pokersim_gui_pt.json"); CHECK(static_cast<bool>(f)); }
        allTabs();
        app.cfg.ptBaselinePath = "/tmp/pokersim_gui_pt.json";
        CHECK(app.startRun());
        waitRun(app);
        CHECK(app.results() && app.results()->summary.ptDatabase && app.results()->summary.ptDatabase->totalHands(ps::pt::Mode::Cash) == 2 * first);   // base + nouvelles mains
        allTabs();
        app.cfg.ptBaselinePath = "/tmp/pt_inexistante.json";
        CHECK(app.startRun());
        waitRun(app);
        CHECK(!app.lastError().empty());              // base de départ introuvable : erreur affichée
        app.cfg.ptBaselinePath.clear(); app.savePt = false;
        CHECK(app.saveConfigFile("/tmp/pokersim_gui_test_cfg2.json"));
        app.savePt = true; CHECK(app.loadConfigFile("/tmp/pokersim_gui_test_cfg2.json") && !app.savePt);

        std::puts("SNG");
        app.setFormat(ps::GameFormat::SNG);
        CHECK(app.cfg.numPlayers == 9 && app.cfg.payoutShares.size() == 3);
        app.cfg.numPlayers = 6; app.cfg.numGames = 10; app.cfg.recordGames = 0;
        app.cfg.payoutShares = ps::defaultPayoutShares(6);
        CHECK(app.startRun());
        waitRun(app);
        CHECK(app.lastError().empty() && app.results() && app.results()->summary.games.size() == 10);
        allTabs();

        if (!ohRoot.empty()) {
            std::puts("profils OpenHoldem réels");
            app.ohRoot = ohRoot;
            ps::oh::ProfileReport rep = ps::oh::inspectProfile(ohRoot + "/DemoBots/OpenPPL_old_style_translated/No Limit/BSS/NL_6max_BSS_by_Subiendolo.txt", ohRoot);
            CHECK(rep.ok() && rep.functions == 6 && rep.lists == 11);
            ps::oh::ProfileReport bad = ps::oh::inspectProfile(ohRoot + "/DemoBots/OpenPPL_old_style_translated/Fixed Limit/Exin_FL_6_max/EKIN_FixedLimit_6_max.txt", ohRoot);
            CHECK(!bad.ok() && !bad.problems.empty());
            ps::oh::ProfileReport missing = ps::oh::inspectProfile("/nulle/part.ohf", ohRoot);
            CHECK(!missing.error.empty());
            app.setFormat(ps::GameFormat::SpinGo);
            app.cfg.numGames = 6; app.cfg.prwinMaxIterations = 400; app.cfg.recordGames = 1;
            app.players[0].kind = PlayerKind::ProfileFile; app.players[0].path = ohRoot + "/DemoBots/OpenPPL_old_style_translated/No Limit/BSS/NL_6max_BSS_by_Subiendolo.txt";
            app.players[1].kind = PlayerKind::ProfileFile; app.players[1].path = ohRoot + "/DemoBots/OH-Script/No Limit/Gecko_NL_6Max.ohf";
            app.players[2].kind = PlayerKind::BuiltinCall;
            CHECK(app.startRun());
            waitRun(app);
            CHECK(app.lastError().empty());
            CHECK(app.results() && app.results()->summary.games.size() == 6);
            allTabs();
        } else std::puts("(profils réels ignorés : pas de racine OpenHoldem en argument)");
        gApp = nullptr;
    }
    // le destructeur a écrit les réglages
    { std::ifstream f(settings); CHECK(static_cast<bool>(f)); }
    ImGui::DestroyContext();
    if (failures) { std::printf("%d échec(s)\n", failures); return 1; }
    std::puts("gui : OK");
    return 0;
}
