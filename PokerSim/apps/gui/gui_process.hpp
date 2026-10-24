// Lancement d'un programme externe (Python) avec lecture de sa sortie en direct et arrêt de TOUT son arbre de processus.
// Windows : CreateProcess + objet Job (les processus enfants sont arrêtés avec le parent). POSIX : fork/exec + groupe de processus.
#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace psgui {

class Process {
public:
    Process();
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    // argv[0] = programme (cherché dans le PATH). cwd peut être vide. Retourne false + error en cas d'échec de lancement.
    bool start(const std::vector<std::string>& argv, const std::string& cwd, std::string& error);
    bool running() const { return started_ && !finished_.load(); }
    bool started() const { return started_; }
    bool finished() const { return started_ && finished_.load(); }
    int exitCode() const { return exitCode_.load(); }       // valide quand finished()
    void terminate();                                         // arrête le processus et ses enfants
    void join();                                              // attend la fin du thread de lecture (après finished() ou terminate())

    // Récupère les lignes de sortie (stdout + stderr) arrivées depuis le dernier appel.
    std::vector<std::string> drainLines();

private:
    void readerLoop();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::thread reader_;
    std::mutex mu_;
    std::deque<std::string> lines_;
    std::atomic<bool> finished_{false};
    std::atomic<int> exitCode_{-1};
    bool started_ = false;
};

}  // namespace psgui
