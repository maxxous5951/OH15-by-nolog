// Exécution multithread de N parties/sessions + agrégation des statistiques par profil.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include "pokersim/brains.hpp"
#include "pokersim/engine.hpp"

namespace ps {

struct ProfileStats {
    std::string name;
    int games = 0, wins = 0, hands = 0, rebuys = 0;
    double totalNet = 0, totalBuyin = 0;
    long long netChips = 0;
    double placeSum = 0;
    double roi() const { return totalBuyin > 0 ? 100.0 * totalNet / totalBuyin : 0.0; }
    double avgPlace() const { return games ? placeSum / games : 0.0; }
    double bb100(int bb) const { return hands ? 100.0 * netChips / bb / hands : 0.0; }
};

struct SimSummary {
    std::vector<GameResult> games;        // dans l'ordre des parties
    std::vector<ProfileStats> profiles;   // un par joueur déclaré
    double seconds = 0;
    bool cancelled = false;
};

// progress(done, total) est appelé depuis les threads de travail (protéger côté appelant si besoin)
// cancel : si non nul et passé à true, les parties en cours se terminent puis l'exécution s'arrête (résultats partiels conservés)
SimSummary runSimulation(const SimConfig& cfg, const BrainFactory& factory, bool collectRecords = false,
                         const std::function<void(int, int)>& progress = nullptr, std::atomic<bool>* cancel = nullptr);

std::string formatHandHistory(const HandRecord& h, const std::vector<std::string>& names, GameFormat f);
std::string formatSummary(const SimConfig& cfg, const SimSummary& s);

}  // namespace ps
