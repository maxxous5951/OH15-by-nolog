// Test de l'interface sans écran : contexte Dear ImGui sans fenêtre, frames simulées, simulations réelles.
//   gui_headless_test [racine_OpenHoldem]   (la racine active les tests avec de vrais profils)
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include "gui_app.hpp"
#include "gui_process.hpp"
#include "imgui.h"
#include "imgui_internal.h"
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

static void waitOpt(App& app, int maxMs = 300000) {
    auto t0 = std::chrono::steady_clock::now();
    while (app.opt.busy()) {
        frames(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() > maxMs) { std::printf("TIMEOUT (optimisation)\n"); ++failures; return; }
    }
    frames(2);
}

// Aucun contenu de l'onglet Optimisation ne doit dépasser la largeur de sa zone (champs, étiquettes et boutons visibles).
static void checkOptimizerLayout(const char* when) {
    gApp->requestedTab = 4; frames(4);
    int seen = 0;
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    for (ImGuiWindow* w : ctx->Windows) {
        const char* last = std::strrchr(w->Name, '/');          // nom du dernier niveau : "opt_<id>" (le journal imbriqué est "optlog_<id>")
        if (!last || std::strncmp(last + 1, "opt_", 4) != 0) continue;
        ++seen;
        if (w->ContentSize.x > w->InnerRect.GetWidth() + 2.0f) {
            std::printf("ECHEC debordement (%s) : contenu %.0f px > zone %.0f px\n", when, w->ContentSize.x, w->InnerRect.GetWidth());
            ++failures;
        }
    }
    CHECK(seen == 1);
}

static void allTabs() { for (int t = 0; t < 5; ++t) { gApp->requestedTab = t; frames(3); } }

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
        std::puts("mise en page de l'onglet Optimisation (1400 et 1000 px de large)");
        checkOptimizerLayout("1400 px");
        io.DisplaySize = ImVec2(1000, 900); checkOptimizerLayout("1000 px");
        io.DisplaySize = ImVec2(1400, 900);

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

        // ------------------------------------------------------------------ détection des dossiers (téléchargements ZIP GitHub, extraction en double)
        std::puts("détection des dossiers de l'onglet Optimisation");
        {
            namespace fs = std::filesystem;
            auto same = [](const std::string& a, const fs::path& b) { return fs::path(a).lexically_normal() == b.lexically_normal(); };   // indépendant du séparateur de chemin
            const fs::path root = "/tmp/pokersim_gui_detect";
            std::error_code ec; fs::remove_all(root, ec);
            auto touch = [](const fs::path& f) { fs::create_directories(f.parent_path()); std::ofstream(f) << "x"; };
            const fs::path exeDir = root / "Downloads/OH15-by-nolog-gui-optimizer/PokerSim/bin/Release";
            fs::create_directories(exeDir);
            // générateur téléchargé en ZIP de la branche, extrait en double ; scripts dans optimizer/
            const fs::path gen = root / "Downloads/OpenHoldem-Profile-Generator-WIP-add-optimizer/OpenHoldem-Profile-Generator-WIP-add-optimizer";
            touch(gen / "ohpg/params.py"); touch(gen / "optimizer/optimize.py");
            Optimizer o1; o1.initDefaults(exeDir.string());
            CHECK(same(o1.s.scriptsDir, gen / "optimizer"));
            CHECK(same(o1.s.ohpgDir, gen));
            CHECK(o1.s.pokersimExe.find("pokersim") != std::string::npos);
            CHECK(same(o1.s.poolDir, fs::path(o1.s.scriptsDir) / "pool"));
            // des réglages déjà renseignés ne sont jamais écrasés
            Optimizer o2; o2.s.scriptsDir = "/deja/la"; o2.initDefaults(exeDir.string());
            CHECK(o2.s.scriptsDir == "/deja/la");
            // rien trouvé : champs vides, données dans optimizer_data près de l'exécutable
            fs::remove_all(root / "Downloads/OpenHoldem-Profile-Generator-WIP-add-optimizer", ec);
            Optimizer o3; o3.initDefaults(exeDir.string());
            CHECK(o3.s.scriptsDir.empty() && o3.s.ohpgDir.empty());
            CHECK(o3.s.poolDir.find("optimizer_data") != std::string::npos);
            std::string e;
            CHECK(!o3.start(OptTask::Pool, false, "", e) && e.find("'...'") != std::string::npos);   // message qui explique quoi faire
            // ancien dépôt poker-profile-optimizer + générateur sous son nom simple
            touch(root / "Downloads/poker-profile-optimizer-main/optimize.py");
            touch(root / "Downloads/OpenHoldem-Profile-Generator-WIP/ohpg/params.py");
            Optimizer o4; o4.initDefaults(exeDir.string());
            CHECK(same(o4.s.scriptsDir, root / "Downloads/poker-profile-optimizer-main"));
            CHECK(same(o4.s.ohpgDir, root / "Downloads/OpenHoldem-Profile-Generator-WIP"));
            fs::remove_all(root, ec);
        }
        // ------------------------------------------------------------------ lanceur de processus (sans dépendance externe)
        std::puts("processus externe : sortie en direct, code de sortie, arrêt");
#ifndef _WIN32
        {
            Process p; std::string err;
            CHECK(p.start({"sh", "-c", "echo un; echo deux 1>&2; exit 3"}, "", err));
            auto t0 = std::chrono::steady_clock::now();
            std::vector<std::string> got;
            while (!p.finished() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) { for (auto& l : p.drainLines()) got.push_back(l); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            p.join();
            for (auto& l : p.drainLines()) got.push_back(l);
            CHECK(p.finished() && p.exitCode() == 3);
            CHECK(got.size() == 2);
            Process q;
            CHECK(!q.start({"programme_inexistant_pokersim"}, "", err) && !err.empty());
            Process r;
            CHECK(r.start({"sh", "-c", "sleep 60 & sleep 60"}, "", err));
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            CHECK(r.running());
            auto t1 = std::chrono::steady_clock::now();
            r.terminate(); r.join();
            CHECK(r.finished());
            CHECK(std::chrono::steady_clock::now() - t1 < std::chrono::seconds(5));
        }
#else
        {
            Process p; std::string err;
            CHECK(p.start({"cmd", "/c", "echo un& echo deux 1>&2& exit 3"}, "", err));
            auto t0 = std::chrono::steady_clock::now();
            std::vector<std::string> got;
            while (!p.finished() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20)) { for (auto& l : p.drainLines()) got.push_back(l); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
            p.join();
            for (auto& l : p.drainLines()) got.push_back(l);
            CHECK(p.finished() && p.exitCode() == 3);
            CHECK(got.size() == 2);
            Process q;
            CHECK(!q.start({"programme_inexistant_pokersim"}, "", err) && !err.empty());
            Process r;
            CHECK(r.start({"ping", "-n", "60", "127.0.0.1"}, "", err));
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            CHECK(r.running());
            auto t1 = std::chrono::steady_clock::now();
            r.terminate(); r.join();
            CHECK(r.finished());
            CHECK(std::chrono::steady_clock::now() - t1 < std::chrono::seconds(10));
        }
#endif
        // ------------------------------------------------------------------ modes de recherche (sans scripts)
        {
            Optimizer o; std::vector<std::string> a; std::string e;
            o.s.searchMode = 0; CHECK(o.searchArgs(a, e) && a.size() == 2 && a[1] == "base" && o.stageCount() == 1 && o.paramCount() == 23);
            o.s.searchMode = 1; CHECK(o.searchArgs(a, e) && a[1] == "all" && o.paramCount() == 87);
            o.s.searchMode = 2; o.s.gens = 5; CHECK(o.searchArgs(a, e) && a[0] == "--stages" && o.stageCount() == 3 && o.totalGens() == 15);
            o.s.useStats = true; CHECK(o.stageCount() == 4 && o.totalGens() == 20);
            o.s.useStats = false;
            o.s.searchMode = 3; o.s.customGroups = 0; CHECK(!o.searchArgs(a, e) && !e.empty());
            o.s.customGroups = (1 << 3) | (1 << 7); CHECK(o.searchArgs(a, e) && a[1] == "flop,global");
            Optimizer o2; o2.fromJsonText(o.toJsonText()); CHECK(o2.s.searchMode == 3 && o2.s.customGroups == o.s.customGroups);
        }
        // ------------------------------------------------------------------ onglet Optimisation (vrais scripts si les variables sont définies)
        {
            const char* sc = std::getenv("PS_OPT_SCRIPTS");
            const char* og = std::getenv("PS_OPT_OHPG");
            const char* px = std::getenv("PS_OPT_POKERSIM");
            const char* ohr = std::getenv("PS_OPT_OHROOT");
            if (sc && og && px && ohr) {
                std::puts("onglet Optimisation : environnement, pool, optimisation, arrêt, reprise, validation");
                const std::string work = "/tmp/pokersim_gui_opt";
                std::error_code ec; std::filesystem::remove_all(work, ec); std::filesystem::create_directories(work, ec);
                Optimizer& o = app.opt;
                o.s.scriptsDir = sc; o.s.ohpgDir = og; o.s.pokersimExe = px; o.s.poolDir = work + "/pool"; o.s.runDir = work + "/run";
                o.s.gens = 3; o.s.mu = 4; o.s.lam = 4; o.s.refine = 2; o.s.t1 = 2; o.s.t2 = 3; o.s.games = 1; o.s.hands = 300; o.s.seed = 5;
                o.s.vTables = 2; o.s.vGames = 1; o.s.vHands = 300;
                o.s.searchMode = 0;
                std::string e;
                CHECK(!o.start(OptTask::Optimize, false, ohr, e) && !e.empty());          // pas de pool : refusé avec un message
                CHECK(!o.start(OptTask::Validate, false, ohr, e) && !e.empty());
                CHECK(o.start(OptTask::Check, false, ohr, e)); waitOpt(app);
                CHECK(o.envChecked && o.envOk);
                CHECK(o.start(OptTask::Pool, false, ohr, e)); waitOpt(app);
                CHECK(o.lastExitCode == 0 && o.poolBots == 15 && o.poolTrain == 10 && o.poolValidation == 5);
                allTabs();
                CHECK(o.start(OptTask::Optimize, false, ohr, e)); CHECK(!o.start(OptTask::Optimize, false, ohr, e));   // déjà en cours
                auto t0 = std::chrono::steady_clock::now();
                while (o.busy() && o.currentGen < 1 && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(120)) { frames(1); std::this_thread::sleep_for(std::chrono::milliseconds(50)); }
                allTabs();
                o.stop(); waitOpt(app);
                CHECK(!o.busy() && o.lastError.empty());                                  // un arrêt demandé n'est pas une erreur
                CHECK(o.stateExists && o.bestExists && o.currentGen >= 0 && !o.history.empty());
                const int stopped = o.currentGen;
                o.s.gens = stopped + 2;
                CHECK(o.start(OptTask::Optimize, true, ohr, e)); waitOpt(app);              // reprise
                CHECK(o.lastExitCode == 0 && o.currentGen == stopped + 2);
                CHECK(static_cast<int>(o.history.size()) >= 2);
                allTabs();
                {   // recherche complete par etapes : 3 etapes x 1 generation
                    const std::string run0 = o.s.runDir; const int g0 = o.s.gens;
                    o.s.runDir = work + "/run_staged"; o.s.searchMode = 2; o.s.gens = 1;
                    CHECK(o.start(OptTask::Optimize, false, ohr, e)); waitOpt(app, 600000);
                    CHECK(o.lastExitCode == 0 && o.currentGen == 3 && o.curStages == 3 && o.curStage == 3 && !o.activeLabel.empty());
                    CHECK(o.history.size() == 3 && o.history[0].stage == 1 && o.history[2].stage == 3);
                    allTabs();
                    o.s.searchMode = 3; o.s.customGroups = 0;
                    CHECK(!o.start(OptTask::Optimize, false, ohr, e) && !e.empty());     // aucun groupe coche : refuse
                    o.s.runDir = run0; o.s.gens = g0; o.s.searchMode = 0; o.refreshFiles();
                }
                CHECK(o.start(OptTask::Validate, false, ohr, e)); waitOpt(app);
                CHECK(o.lastExitCode == 0 && o.validation.size() == 7);
                checkOptimizerLayout("avec resultats, 1400 px");
                io.DisplaySize = ImVec2(1000, 900); checkOptimizerLayout("avec resultats, 1000 px");
                io.DisplaySize = ImVec2(1400, 900);
                bool hasCand = false; for (auto& r : o.validation) if (r.name == "CANDIDAT" && r.vaHands > 0) hasCand = true;
                CHECK(hasCand);
                allTabs();
                CHECK(app.useProfileAsPlayer(o.bestOhfPath(), "OPT_BEST") && app.players[0].kind == PlayerKind::ProfileFile);
                // les réglages sont sauvegardés et rechargés
                o.s.gens = 77;
                CHECK(app.saveConfigFile("/tmp/pokersim_gui_test_cfg3.json"));
                o.s.gens = 5;
                CHECK(app.loadConfigFile("/tmp/pokersim_gui_test_cfg3.json") && o.s.gens == 77);
                // le meilleur profil joue vraiment dans le simulateur
                app.setFormat(ps::GameFormat::Cash);
                app.cfg.numPlayers = 3; app.cfg.numGames = 2; app.cfg.cashHands = 100; app.cfg.recordGames = 0; app.cfg.prwinMaxIterations = 400;
                app.players[1].kind = PlayerKind::BuiltinCall; app.players[2].kind = PlayerKind::BuiltinTag;
                app.ohRoot = ohr; app.savePt = false; app.saveHands = false;
                CHECK(app.startRun()); waitRun(app);
                CHECK(app.lastError().empty() && app.results() && app.results()->summary.games.size() == 2);
            } else std::puts("(onglet Optimisation ignoré : PS_OPT_SCRIPTS, PS_OPT_OHPG, PS_OPT_POKERSIM, PS_OPT_OHROOT non définies)");
        }
        gApp = nullptr;
    }
    // le destructeur a écrit les réglages
    { std::ifstream f(settings); CHECK(static_cast<bool>(f)); }
    ImGui::DestroyContext();
    if (failures) { std::printf("%d échec(s)\n", failures); return 1; }
    std::puts("gui : OK");
    return 0;
}
