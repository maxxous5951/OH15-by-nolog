// Optimisation automatique de profils : pilote les scripts Python du projet poker-profile-optimizer
// (make_pool.py, optimize.py, validate.py) en processus externes et relit leurs fichiers de résultats.
// Indépendant de Dear ImGui : testable sans écran.
#pragma once
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <vector>
#include "gui_process.hpp"

namespace psgui {

struct OptSettings {
    std::string python;          // exécutable Python ("python" sous Windows, "python3" ailleurs)
    std::string scriptsDir;      // dossier contenant optimize.py, validate.py, make_pool.py, simlib.py
    std::string ohpgDir;         // dossier du dépôt OpenHoldem-Profile-Generator-WIP
    std::string pokersimExe;     // pokersim en ligne de commande (avec --json)
    std::string poolDir;         // pool de bots (pool.json + profiles/)
    std::string runDir;          // dossier de cette optimisation (state.json, best.ohf...)
    int poolSeed = 1;
    // optimisation
    int gens = 30, mu = 8, lam = 8, refine = 3, t1 = 4, t2 = 10, games = 2, hands = 1000, seed = 1, jobs = 0, prwinMax = 10000;
    double k = 1.0, sigma = 0.10, rake = 0.0;
    int rakeCap = 0;
    bool useStats = false;
    // validation
    int vTables = 12, vGames = 4, vHands = 1000;
};

enum class OptTask { None, Check, Pool, Optimize, Validate };

struct HistPoint { int gen = 0; double mean = 0, ci = 0; long long hands = 0; };
struct ValRow { std::string name; double trMean = 0, trCi = 0, vaMean = 0, vaCi = 0; long long trHands = 0, vaHands = 0; };

class Optimizer {
public:
    Optimizer() = default;

    OptSettings s;

    // Cherche les dossiers par défaut près de l'exécutable (sans écraser les champs déjà renseignés).
    void initDefaults(const std::string& exeDir);

    bool start(OptTask t, bool resume, const std::string& ohRoot, std::string& error);
    void stop();
    void poll();                                   // à appeler à chaque frame : journal, fin de tâche, relecture des fichiers (1 Hz)
    bool busy() const { return proc_ && proc_->running(); }
    OptTask task() const { return task_; }
    bool stopping() const { return stopping_; }

    // ---- état lisible par l'interface
    bool envChecked = false, envOk = false;
    std::string envMessage;
    int poolBots = 0, poolTrain = 0, poolValidation = 0;
    bool stateExists = false;                      // state.json présent : on peut reprendre
    int currentGen = -1;                           // dernière génération terminée (-1 : aucune)
    bool bestExists = false;
    double bestMean = 0, bestCi = 0;
    long long bestHands = 0;
    std::vector<HistPoint> history;
    std::vector<ValRow> validation;
    std::string lastError, statusLine;
    std::deque<std::string> log;
    int lastExitCode = 0;

    double elapsedSeconds() const;
    double etaSeconds() const;                     // -1 si inconnu
    std::string bestOhfPath() const;
    std::string runFile(const std::string& name) const;
    void refreshFiles();                           // relit pool.json, state.json, best.json, history.jsonl, validation.json

    // réglages <-> JSON (sauvegardés avec ceux de l'application)
    std::string toJsonText() const;
    void fromJsonText(const std::string& text);

private:
    std::vector<std::string> buildArgv(OptTask t, bool resume, const std::string& ohRoot) const;
    void addLog(const std::string& line);
    void finishTask();

    std::unique_ptr<Process> proc_;
    OptTask task_ = OptTask::None;
    bool stopping_ = false;
    std::chrono::steady_clock::time_point t0_{}, lastRefresh_{};
    int genAtStart_ = -1;
};

std::string joinPath(const std::string& a, const std::string& b);

}  // namespace psgui
