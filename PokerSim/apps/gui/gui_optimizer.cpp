#include "gui_optimizer.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "nlohmann/json.hpp"
#include "pokersim/pathutil.hpp"

namespace psgui {
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
bool exists(const std::string& p) { std::error_code ec; return !p.empty() && fs::exists(ps::pathFromUtf8(p), ec); }

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(ps::pathFromUtf8(path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// moyenne et IC 95 % d'un accumulateur [n, somme, somme des carrés, mains] (format de simlib.Acc)
void accStats(const json& a, double& mean, double& ci, long long& hands) {
    mean = ci = 0; hands = 0;
    if (!a.is_array() || a.size() < 4) return;
    const double n = a[0].get<double>(), s = a[1].get<double>(), ss = a[2].get<double>();
    hands = a[3].get<long long>();
    if (n <= 0) return;
    mean = s / n;
    if (n >= 2) ci = 1.96 * std::sqrt(std::max(0.0, (ss - s * s / n) / (n - 1)) / n);
}
}  // namespace

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char c = a.back();
    return (c == '/' || c == '\\') ? a + b : a + "/" + b;
}

// ============================================================ valeurs par défaut
void Optimizer::initDefaults(const std::string& exeDir) {
#ifdef _WIN32
    if (s.python.empty()) s.python = "python";
    const char* exe = "pokersim.exe";
#else
    if (s.python.empty()) s.python = "python3";
    const char* exe = "pokersim";
#endif
    if (s.pokersimExe.empty()) s.pokersimExe = joinPath(exeDir, exe);
    std::vector<std::string> bases;
    std::string b = exeDir.empty() ? "." : exeDir;
    for (int i = 0; i < 5; ++i) { bases.push_back(b); b = joinPath(b, ".."); }
    if (s.scriptsDir.empty()) {
        // dépôt poker-profile-optimizer, ou dossier optimizer/ du dépôt du générateur
        for (const auto& base : bases)
            for (const char* name : {"poker-profile-optimizer", "OpenHoldem-Profile-Generator-WIP/optimizer", "optimizer"})
                if (s.scriptsDir.empty() && exists(joinPath(joinPath(base, name), "optimize.py"))) s.scriptsDir = joinPath(base, name);
    }
    if (s.ohpgDir.empty()) {
        std::vector<std::string> cands = bases;
        if (!s.scriptsDir.empty()) cands.push_back(joinPath(s.scriptsDir, ".."));
        if (!s.scriptsDir.empty() && exists(joinPath(joinPath(joinPath(s.scriptsDir, ".."), "ohpg"), "params.py"))) s.ohpgDir = joinPath(s.scriptsDir, "..");   // optimizer/ dans le dépôt du générateur
        for (const auto& base : cands)
            if (s.ohpgDir.empty() && exists(joinPath(joinPath(joinPath(base, "OpenHoldem-Profile-Generator-WIP"), "ohpg"), "params.py")))
                s.ohpgDir = joinPath(base, "OpenHoldem-Profile-Generator-WIP");
    }
    const std::string root = s.scriptsDir.empty() ? exeDir : s.scriptsDir;
    if (s.poolDir.empty()) s.poolDir = joinPath(root, "pool");
    if (s.runDir.empty()) s.runDir = joinPath(joinPath(root, "runs"), "run1");
}

// ============================================================ lancement
std::vector<std::string> Optimizer::buildArgv(OptTask t, bool resume, const std::string& ohRoot) const {
    auto num = [](double v) { std::ostringstream o; o << v; return o.str(); };
    std::vector<std::string> a{s.python};
    switch (t) {
    case OptTask::Check:
        a.insert(a.end(), {"-c",
            "import sys, subprocess; sys.path.insert(0, sys.argv[1]); import ohpg; "
            "h = subprocess.run([sys.argv[2], '--help'], capture_output=True, text=True).stdout; "
            "print('OK Python', sys.version.split()[0], '| ohpg', getattr(ohpg, '__version__', '?'), '| pokersim --json :', '--json' in h)",
            s.ohpgDir, s.pokersimExe});
        break;
    case OptTask::Pool:
        a.insert(a.end(), {joinPath(s.scriptsDir, "make_pool.py"), "--ohpg", s.ohpgDir, "--out", s.poolDir, "--seed", std::to_string(s.poolSeed)});
        break;
    case OptTask::Optimize:
        a.insert(a.end(), {joinPath(s.scriptsDir, "optimize.py"), "--pokersim", s.pokersimExe, "--oh-root", ohRoot, "--ohpg", s.ohpgDir,
                           "--pool", s.poolDir, "--run", s.runDir, "--gens", std::to_string(s.gens), "--mu", std::to_string(s.mu),
                           "--lam", std::to_string(s.lam), "--refine", std::to_string(s.refine), "--t1", std::to_string(s.t1),
                           "--t2", std::to_string(s.t2), "--games", std::to_string(s.games), "--hands", std::to_string(s.hands),
                           "--k", num(s.k), "--sigma", num(s.sigma), "--seed", std::to_string(s.seed), "--jobs", std::to_string(s.jobs),
                           "--prwin-max", std::to_string(s.prwinMax), "--rake", num(s.rake), "--rake-cap", std::to_string(s.rakeCap)});
        if (s.useStats) a.push_back("--use-stats");
        if (resume) a.push_back("--resume");
        break;
    case OptTask::Validate:
        a.insert(a.end(), {joinPath(s.scriptsDir, "validate.py"), "--pokersim", s.pokersimExe, "--oh-root", ohRoot, "--ohpg", s.ohpgDir,
                           "--pool", s.poolDir, "--run", s.runDir, "--tables", std::to_string(s.vTables), "--games", std::to_string(s.vGames),
                           "--hands", std::to_string(s.vHands), "--jobs", std::to_string(s.jobs), "--prwin-max", std::to_string(s.prwinMax),
                           "--rake", num(s.rake), "--rake-cap", std::to_string(s.rakeCap)});
        break;
    default: break;
    }
    return a;
}

bool Optimizer::start(OptTask t, bool resume, const std::string& ohRoot, std::string& error) {
    error.clear();
    if (busy()) { error = "une tache est deja en cours"; return false; }
    if (s.python.empty()) { error = "Indiquez l'executable Python."; return false; }
    if (s.scriptsDir.empty() || !exists(joinPath(s.scriptsDir, "optimize.py")))
        { error = "Dossier des scripts introuvable : il doit contenir optimize.py (depot poker-profile-optimizer)."; return false; }
    if (s.ohpgDir.empty() || !exists(joinPath(joinPath(s.ohpgDir, "ohpg"), "params.py")))
        { error = "Dossier du generateur introuvable : il doit contenir ohpg/params.py (depot OpenHoldem-Profile-Generator-WIP)."; return false; }
    if (t != OptTask::Pool && !exists(s.pokersimExe)) { error = "pokersim (ligne de commande) introuvable : " + s.pokersimExe; return false; }
    if (t == OptTask::Optimize || t == OptTask::Validate) {
        if (ohRoot.empty()) { error = "La racine OpenHoldem (OpenPPL/OpenPPL_Library) n'est pas definie (onglet Configuration)."; return false; }
        if (!exists(joinPath(s.poolDir, "pool.json"))) { error = "Pool de bots introuvable : creez-le d'abord (bouton 'Creer le pool')."; return false; }
    }
    if (t == OptTask::Validate && !exists(joinPath(s.runDir, "best.ohf"))) { error = "Aucun meilleur profil a valider : lancez d'abord une optimisation."; return false; }
    if (t == OptTask::Optimize && resume && !exists(joinPath(s.runDir, "state.json"))) { error = "Rien a reprendre dans ce dossier de run."; return false; }

    auto p = std::make_unique<Process>();
    const std::vector<std::string> argv = buildArgv(t, resume, ohRoot);
    std::string cmd;
    for (const auto& x : argv) cmd += (cmd.empty() ? "" : " ") + x;
    if (!p->start(argv, s.scriptsDir, error)) return false;
    proc_ = std::move(p);
    task_ = t;
    stopping_ = false;
    lastError.clear();
    t0_ = std::chrono::steady_clock::now();
    genAtStart_ = -1;
    if (t == OptTask::Optimize) { refreshFiles(); genAtStart_ = currentGen; }
    log.clear();
    addLog("> " + cmd);
    return true;
}

void Optimizer::stop() {
    if (proc_ && proc_->running()) { stopping_ = true; proc_->terminate(); addLog("[arret demande]"); }
}

void Optimizer::addLog(const std::string& line) {
    log.push_back(line);
    while (log.size() > 600) log.pop_front();
}

void Optimizer::poll() {
    if (!proc_) return;
    for (auto& l : proc_->drainLines()) addLog(l);
    const auto now = std::chrono::steady_clock::now();
    if (proc_->running()) {
        if (task_ == OptTask::Optimize && std::chrono::duration<double>(now - lastRefresh_).count() >= 1.0) { lastRefresh_ = now; refreshFiles(); }
        return;
    }
    if (proc_->finished()) {
        proc_->join();
        for (auto& l : proc_->drainLines()) addLog(l);
        finishTask();
    }
}

void Optimizer::finishTask() {
    lastExitCode = proc_->exitCode();
    const OptTask t = task_;
    std::string okLine;
    for (auto it = log.rbegin(); it != log.rend(); ++it) if (it->rfind("OK ", 0) == 0) { okLine = *it; break; }
    char buf[160];
    std::snprintf(buf, sizeof buf, stopping_ ? "[arrete par l'utilisateur, code %d]" : "[termine, code %d]", lastExitCode);
    addLog(buf);
    if (t == OptTask::Check) {
        envChecked = true;
        envOk = lastExitCode == 0 && !okLine.empty() && okLine.find("pokersim --json : True") != std::string::npos;
        if (envOk) envMessage = okLine;
        else if (lastExitCode == 0 && !okLine.empty()) envMessage = okLine + "  -> ce pokersim n'a pas l'option --json : recompilez la branche gui-optimizer.";
        else envMessage = "Echec : voir le journal ci-dessous (Python introuvable, ohpg absent ou pokersim invalide).";
    } else if (lastExitCode != 0 && !stopping_) {
        lastError = "La tache s'est terminee avec une erreur (code " + std::to_string(lastExitCode) + ") : voir le journal.";
    }
    refreshFiles();
    task_ = OptTask::None;
    stopping_ = false;
    proc_.reset();
}

double Optimizer::elapsedSeconds() const {
    if (!proc_ || !proc_->started()) return 0;
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
}

double Optimizer::etaSeconds() const {
    if (task_ != OptTask::Optimize || genAtStart_ < -1) return -1;
    const int done = currentGen - std::max(genAtStart_, 0);
    if (done < 1 || currentGen >= s.gens) return -1;
    return elapsedSeconds() / done * (s.gens - currentGen);
}

std::string Optimizer::bestOhfPath() const { return joinPath(s.runDir, "best.ohf"); }
std::string Optimizer::runFile(const std::string& name) const { return joinPath(s.runDir, name); }

// ============================================================ relecture des fichiers de résultats
void Optimizer::refreshFiles() {
    std::string txt;
    // pool
    poolBots = poolTrain = poolValidation = 0;
    if (readFile(joinPath(s.poolDir, "pool.json"), txt)) {
        try {
            const json j = json::parse(txt);
            for (const auto& b : j.at("bots")) {
                ++poolBots;
                if (b.value("split", "") == "train") ++poolTrain; else ++poolValidation;
            }
        } catch (...) { poolBots = poolTrain = poolValidation = 0; }
    }
    // état / meilleur
    stateExists = exists(joinPath(s.runDir, "state.json"));
    bestExists = exists(joinPath(s.runDir, "best.ohf"));
    currentGen = -1;
    if (readFile(joinPath(s.runDir, "best.json"), txt)) {
        try {
            const json j = json::parse(txt);
            currentGen = j.value("generation", -1);
            bestMean = j.value("mean_bb100", 0.0);
            bestCi = j.value("ci95", 0.0);
            bestHands = j.value("hands", 0LL);
        } catch (...) {}
    }
    // historique : meilleur de chaque génération
    history.clear();
    {
        std::ifstream f(ps::pathFromUtf8(joinPath(s.runDir, "history.jsonl")), std::ios::binary);
        std::string line;
        while (f && std::getline(f, line)) {
            if (line.empty()) continue;
            try {
                const json j = json::parse(line);
                HistPoint h;
                h.gen = j.at("gen").get<int>();
                accStats(j.at("best").at("acc"), h.mean, h.ci, h.hands);
                history.push_back(h);
            } catch (...) {}
        }
    }
    // validation
    validation.clear();
    if (readFile(joinPath(s.runDir, "validation.json"), txt)) {
        try {
            const json j = json::parse(txt);
            for (auto it = j.begin(); it != j.end(); ++it) {
                ValRow r;
                r.name = it.key();
                for (auto sp = it.value().begin(); sp != it.value().end(); ++sp) {
                    const bool isVal = sp.key().rfind("valid", 0) == 0;
                    const double m = sp.value().value("mean", 0.0), c = sp.value().value("ci95", 0.0);
                    const long long h = sp.value().value("hands", 0LL);
                    if (isVal) { r.vaMean = m; r.vaCi = c; r.vaHands = h; } else { r.trMean = m; r.trCi = c; r.trHands = h; }
                }
                validation.push_back(r);
            }
        } catch (...) { validation.clear(); }
    }
}

// ============================================================ réglages
std::string Optimizer::toJsonText() const {
    json j;
    j["python"] = s.python; j["scriptsDir"] = s.scriptsDir; j["ohpgDir"] = s.ohpgDir; j["pokersimExe"] = s.pokersimExe;
    j["poolDir"] = s.poolDir; j["runDir"] = s.runDir; j["poolSeed"] = s.poolSeed;
    j["gens"] = s.gens; j["mu"] = s.mu; j["lam"] = s.lam; j["refine"] = s.refine; j["t1"] = s.t1; j["t2"] = s.t2;
    j["games"] = s.games; j["hands"] = s.hands; j["seed"] = s.seed; j["jobs"] = s.jobs; j["prwinMax"] = s.prwinMax;
    j["k"] = s.k; j["sigma"] = s.sigma; j["rake"] = s.rake; j["rakeCap"] = s.rakeCap; j["useStats"] = s.useStats;
    j["vTables"] = s.vTables; j["vGames"] = s.vGames; j["vHands"] = s.vHands;
    return j.dump();
}

void Optimizer::fromJsonText(const std::string& text) {
    try {
        const json j = json::parse(text);
        auto str = [&](const char* k, std::string& v) { if (j.contains(k) && j[k].is_string() && !j[k].get<std::string>().empty()) v = j[k].get<std::string>(); };
        auto num = [&](const char* k, auto& v, auto lo, auto hi) {
            if (j.contains(k) && j[k].is_number()) v = std::clamp(j[k].get<std::decay_t<decltype(v)>>(), static_cast<std::decay_t<decltype(v)>>(lo), static_cast<std::decay_t<decltype(v)>>(hi));
        };
        str("python", s.python); str("scriptsDir", s.scriptsDir); str("ohpgDir", s.ohpgDir); str("pokersimExe", s.pokersimExe);
        str("poolDir", s.poolDir); str("runDir", s.runDir);
        num("poolSeed", s.poolSeed, 0, 1000000000);
        num("gens", s.gens, 1, 100000); num("mu", s.mu, 2, 200); num("lam", s.lam, 1, 400); num("refine", s.refine, 1, 400);
        num("t1", s.t1, 1, 1000); num("t2", s.t2, 1, 10000); num("games", s.games, 1, 1000); num("hands", s.hands, 50, 1000000);
        num("seed", s.seed, 0, 1000000000); num("jobs", s.jobs, 0, 256); num("prwinMax", s.prwinMax, 0, 1000000);
        num("k", s.k, 0.0, 10.0); num("sigma", s.sigma, 0.001, 1.0); num("rake", s.rake, 0.0, 20.0); num("rakeCap", s.rakeCap, 0, 100000);
        num("vTables", s.vTables, 1, 10000); num("vGames", s.vGames, 1, 1000); num("vHands", s.vHands, 50, 1000000);
        if (j.contains("useStats") && j["useStats"].is_boolean()) s.useStats = j["useStats"].get<bool>();
    } catch (...) {}
}

}  // namespace psgui
