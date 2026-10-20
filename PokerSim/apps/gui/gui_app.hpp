// Interface graphique de PokerSim : logique et dessin Dear ImGui, indépendants de la plateforme.
// Le programme Windows (main_win32.cpp) ne fait que créer la fenêtre DirectX 11 ; les tests « sans écran » pilotent cette classe directement.
#pragma once
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "pokersim/ohsymbols.hpp"
#include "pokersim/runner.hpp"

namespace psgui {

// Services dépendant du système (boîtes de dialogue). Implémentation Windows : gui_platform_win32.cpp.
struct Platform {
    virtual ~Platform() = default;
    virtual std::string openFile(const char* /*title*/, const char* /*filterName*/, const char* /*patterns*/) { return {}; }
    virtual std::string saveFile(const char* /*title*/, const char* /*defaultName*/, const char* /*ext*/) { return {}; }
    virtual std::string pickFolder(const char* /*title*/) { return {}; }
    virtual std::string exeDir() { return "."; }
};

enum class PlayerKind : int { BuiltinTag = 0, BuiltinCall = 1, BuiltinRandom = 2, ProfileFile = 3 };

struct PlayerRow {
    std::string name;
    PlayerKind kind = PlayerKind::BuiltinTag;
    std::string path;                   // profil OpenHoldem (.ohf / .oppl / .txt)
};

struct PlayerView {
    std::string name;
    int games = 0, wins = 0, rebuys = 0;
    long long hands = 0, netChips = 0;
    std::vector<int> placeCount;        // placeCount[0] = 1res places...
    double avgPlace = 0, net = 0, roi = 0, roiCi = 0, bb100 = 0, bb100Ci = 0;
};

struct ResultsView {
    ps::SimSummary summary;
    ps::SimConfig cfg;                  // configuration réellement exécutée
    std::vector<PlayerView> players;
    std::vector<std::vector<float>> cumulative;   // gain cumulé par joueur, partie après partie
    long long totalHands = 0;
    double seconds = 0;
    bool cancelled = false;
    std::vector<std::pair<long long, std::string>> unknownSymbols;   // (évaluations, nom), tri décroissant
};

class App {
public:
    App(Platform& platform, std::string settingsPath);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    void frame();                       // à appeler entre ImGui::NewFrame() et ImGui::Render()

    // ---- exécution
    bool startRun();                    // false + lastError() si la configuration est invalide
    void stopRun();                     // demande l'arrêt ; les résultats partiels sont conservés
    void poll();                        // récupère une exécution terminée (appelé par frame())
    bool running() const { return running_; }
    int progressDone() const { return done_.load(); }
    int progressTotal() const { return total_.load(); }
    const ResultsView* results() const { return results_.get(); }
    const std::string& lastError() const { return error_; }

    // ---- export / persistance
    bool exportCsv(const std::string& path) const;
    std::string summaryText() const;
    bool saveSettings() const;
    bool loadSettings();
    bool saveConfigFile(const std::string& path) const;
    bool loadConfigFile(const std::string& path);
    void setFormat(ps::GameFormat f);
    void resetPlayers();
    ps::SimConfig buildRunConfig(std::string& error) const;   // configuration + joueurs ; error non vide si invalide

    // ---- modèle éditable (public : piloté par l'interface et par les tests)
    ps::SimConfig cfg;
    std::vector<PlayerRow> players;
    std::string ohRoot;
    int requestedTab = -1;              // force la sélection d'un onglet au prochain frame (tests)
    int handGame = 0, handIndex = 0;

private:
    void drawTopBar();
    void drawConfigTab();
    void drawFormatSpecific();
    void drawPlayersTab();
    void drawResultsTab();
    void drawHandsTab();
    void checkOhRoot();
    ps::oh::ProfileReport& report(const std::string& path);
    static std::unique_ptr<ResultsView> makeResults(ps::SimSummary&& s, const ps::SimConfig& cfg, double seconds);

    Platform& platform_;
    std::string settingsPath_;
    std::thread worker_;
    std::atomic<int> done_{0}, total_{0};
    std::atomic<bool> cancel_{false}, finished_{false};
    bool running_ = false;
    std::mutex mu_;
    std::unique_ptr<ResultsView> pending_;
    std::string pendingError_;
    std::unique_ptr<ResultsView> results_;
    std::string error_;
    std::chrono::steady_clock::time_point t0_;
    std::map<std::string, ps::oh::ProfileReport> reports_;
    std::string reportPath_;
    bool ohRootOk_ = false;
    std::string ohRootChecked_;
    std::string statusMsg_;
};

}  // namespace psgui
