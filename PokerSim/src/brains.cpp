#include "pokersim/brains.hpp"
#include <algorithm>
#include <stdexcept>
#include "pokersim/equity.hpp"

namespace ps {
namespace {

class CallStation : public IBrain {
public:
    Decision decide(Game&, int) override { return Decision::call(); }
};

class RandomBot : public IBrain {
public:
    explicit RandomBot(uint64_t seed) : rng_(seed) {}
    Decision decide(Game& g, int) override {
        double r = rng_.uniform();
        if (r < 0.2) return Decision::fold();
        if (r < 0.7) return Decision::call();
        if (r < 0.9) return Decision::raiseTo(g.t.currentBet + g.t.bblind * rng_.range(2, 6));
        return Decision::allIn();
    }
private:
    Rng rng_;
};

// Joueur serré-agressif basé sur l'équité (prwin) contre des mains aléatoires.
class TagBot : public IBrain {
public:
    TagBot(uint64_t seed, int iters) : rng_(seed), iters_(iters) {}
    Decision decide(Game& g, int chair) override {
        const Table& t = g.t;
        const Seat& s = t.seats[chair];
        int opp = 0;
        for (auto& x : t.seats) if (x.inHand && x.chair != chair) ++opp;
        opp = std::max(1, opp);
        Equity e = equityVsRandom(s.cards.data(), t.board.data(), static_cast<int>(t.board.size()), opp, iters_, rng_);
        double eq = e.win + 0.5 * e.tie;
        int toCall = t.currentBet - s.bet;
        int pot = t.pot();
        double base = 1.0 / (opp + 1);
        double potOdds = toCall > 0 ? static_cast<double>(toCall) / (pot + toCall) : 0.0;
        if (eq > base * 1.9) {
            int size = std::max(t.bblind * 2, static_cast<int>(pot * 0.7));
            return Decision::raiseTo(t.currentBet + size);
        }
        if (toCall == 0) return Decision::check();
        if (eq > potOdds + 0.04 && eq > base * 1.1) return Decision::call();
        return Decision::fold();
    }
private:
    Rng rng_;
    int iters_;
};

}  // namespace

std::unique_ptr<IBrain> makeBuiltinBrain(const std::string& kind, uint64_t seed, const SimConfig& cfg) {
    if (kind == "call") return std::make_unique<CallStation>();
    if (kind == "random") return std::make_unique<RandomBot>(seed);
    if (kind == "tag") return std::make_unique<TagBot>(seed, std::min(cfg.prwinIterations, 400));
    throw std::runtime_error("Bot intégré inconnu : " + kind + " (call, random, tag)");
}

BrainFactory defaultBrainFactory() {
    return [](const PlayerSpec& p, int, uint64_t seed, const SimConfig& cfg) -> std::unique_ptr<IBrain> {
        const std::string pre = "builtin:";
        if (p.profile.rfind(pre, 0) == 0) return makeBuiltinBrain(p.profile.substr(pre.size()), seed, cfg);
        throw std::runtime_error("Profil non supporté pour l'instant : " + p.profile);
    };
}

}  // namespace ps
