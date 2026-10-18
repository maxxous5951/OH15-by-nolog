// Bots intégrés (tests et adversaires de référence). Les profils OpenHoldem passeront par la même interface IBrain.
#pragma once
#include <functional>
#include <memory>
#include "pokersim/engine.hpp"

namespace ps {

// Usine de cerveaux : (spec joueur, index profil, graine, config) -> cerveau
using BrainFactory = std::function<std::unique_ptr<IBrain>(const PlayerSpec&, int, uint64_t, const SimConfig&)>;

std::unique_ptr<IBrain> makeBuiltinBrain(const std::string& kind, uint64_t seed, const SimConfig& cfg);
// Usine par défaut : ne gère que "builtin:*"
BrainFactory defaultBrainFactory();

}  // namespace ps
