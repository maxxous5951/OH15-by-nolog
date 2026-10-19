#include <cmath>
#include <filesystem>
#include <mutex>
#include "pokersim/ohsymbols.hpp"

namespace ps::oh {
namespace {

enum Flag { F_ALLIN, F_BP21, F_BP11, F_BP34, F_BP23, F_BP12, F_BP13, F_BP14, F_BETSIZE, F_RAISE, F_CALL, F_CHECK, F_FOLD, F_COUNT };
const double kMultipleOfPot[F_COUNT] = {0, 2.0, 1.0, 0.75, 2.0 / 3, 0.5, 1.0 / 3, 0.25, 0, 0, 0, 0, 0};
const std::pair<Flag, Flag> kBackupChain[] = {{F_ALLIN, F_BP11}, {F_BP11, F_BP34}, {F_BP34, F_BP23}, {F_BP23, F_BP12}, {F_BP12, F_BP13},
    {F_BP13, F_BP14}, {F_BP14, F_RAISE}, {F_BETSIZE, F_RAISE}, {F_RAISE, F_CALL}, {F_CALL, F_CHECK}, {F_CHECK, F_FOLD}};
const std::pair<Flag, const char*> kActionFuncs[] = {{F_ALLIN, "RaiseMax"}, {F_BP21, "RaiseTwoPot"}, {F_BP11, "RaisePot"}, {F_BP34, "RaiseThreeFourthPot"},
    {F_BP23, "RaiseTwoThirdPot"}, {F_BP12, "RaiseHalfPot"}, {F_BP13, "RaiseThirdPot"}, {F_BP14, "RaiseFourthPot"}, {F_RAISE, "Raise"},
    {F_CALL, "Call"}, {F_CHECK, "Check"}, {F_FOLD, "Fold"}};
const char* const kMainOpenPPL[4] = {"f$preflop", "f$flop", "f$turn", "f$river"};

std::mutex gUnknownMu;
std::map<std::string, long> gUnknown;

class OhBrain : public IBrain {
public:
    OhBrain(std::shared_ptr<const FormulaCollection> fc, const SimConfig& cfg, uint64_t seed, const std::string& name)
        : ctx_(fc, cfg, seed, name), script_(isOHScript(*fc)) {}
    ~OhBrain() override {
        if (ctx_.unknownSymbols().empty()) return;
        std::lock_guard<std::mutex> lk(gUnknownMu);
        for (auto& kv : ctx_.unknownSymbols()) gUnknown[kv.first] += kv.second;
    }
    void newGame(Game& g) override { ctx_.newGame(g); }
    Decision decide(Game& g, int chair) override {
        Table& t = g.t;
        if (ctx_.game != &g || ctx_.handNo != t.handNo) { ctx_.newHand(g, chair); ctx_.handNo = t.handNo; }
        ctx_.prepare(g, chair);
        double flags[F_COUNT] = {0};
        if (script_) flagsScript(flags); else flagsOpenPPL(flags);
        return toDecision(flags);
    }
    void afterAction(Game& g, int chair) override { ctx_.afterAction(g, chair); }

private:
    double ev(std::initializer_list<const char*> names) {
        for (const char* n : names) if (ctx_.fc->has(n)) return ctx_.evaluateNamed(n);
        return 0.0;
    }
    void flagsScript(double* f) {
        f[F_ALLIN] = ev({"f$allin", "f$alli"});
        f[F_BP21] = ev({"f$betpot_2_1"}); f[F_BP11] = ev({"f$betpot_1_1"}); f[F_BP34] = ev({"f$betpot_3_4"}); f[F_BP23] = ev({"f$betpot_2_3"});
        f[F_BP12] = ev({"f$betpot_1_2"}); f[F_BP13] = ev({"f$betpot_1_3"}); f[F_BP14] = ev({"f$betpot_1_4"});
        f[F_BETSIZE] = ev({"f$betsize"});
        f[F_RAISE] = ev({"f$raise", "f$rais"});
        f[F_CALL] = ev({"f$call"}); f[F_CHECK] = ev({"f$check"}); f[F_FOLD] = ev({"f$fold"});
        // ancien WinHoldem : f$srai > 0 = relance de « f$srai » jetons au-dessus de call + mise courante
        if (ctx_.fc->has("f$srai")) {
            bool any = false;
            for (Flag k : {F_ALLIN, F_BP21, F_BP11, F_BP34, F_BP23, F_BP12, F_BP13, F_BP14, F_BETSIZE, F_RAISE}) any = any || f[k] != 0.0;
            if (!any) { double sr = ctx_.evaluateNamed("f$srai"); if (sr > 0) f[F_BETSIZE] = ctx_.me->bet + ctx_.callAmount() + sr; }
        }
    }
    void flagsOpenPPL(double* f) {
        Table& t = *ctx_.t;
        const char* name = kMainOpenPPL[t.betround - 1];
        auto fn = ctx_.fc->get(name);
        double decision;
        if (!fn || fn->text.find_first_not_of(" \t\r\n") == std::string::npos) {
            std::string d = defaultFunctionFor(name);
            decision = d.empty() ? 0.0 : ctx_.evaluateNamed(d);
        } else decision = ctx_.evaluateNamed(name);
        if (decision > 0) f[F_BETSIZE] = decision * ctx_.unit();
        else if (decision > -1000 && decision < 0) {
            double call = ctx_.callAmount();
            f[F_BETSIZE] = ctx_.me->bet + call + (-decision) * (t.pot() + call);
        } else if (decision < -1000) {
            for (auto& kv : kActionFuncs) {
                bool found = false;
                double v = ctx_.evaluateNamed(kv.second, &found);
                if (found && decision == v) f[kv.first] = 1.0;
            }
        } else { f[F_CHECK] = 1.0; f[F_FOLD] = 1.0; }
        for (auto& ab : kBackupChain) if (f[ab.first] != 0.0 && f[ab.second] == 0.0) f[ab.second] = 1.0;
        if (ctx_.me->stack + ctx_.me->bet <= t.currentBet && f[F_CALL] != 0.0 && f[F_ALLIN] == 0.0) f[F_ALLIN] = 1.0;
    }
    Decision toDecision(const double* f) {
        Table& t = *ctx_.t;
        double call = ctx_.callAmount(), pot = t.pot();
        for (int k = 0; k < F_COUNT; ++k) {
            double v = f[k];
            if (v == 0.0 || std::isnan(v)) continue;
            if (k == F_ALLIN) return Decision::allIn();
            if (k >= F_BP21 && k <= F_BP14) {
                double to = ctx_.me->bet + call + kMultipleOfPot[k] * (pot + call);
                if (!std::isfinite(to)) continue;
                return Decision::raiseTo(static_cast<int>(std::nearbyint(to)), PA_BETSIZE);
            }
            if (k == F_BETSIZE) {
                if (!std::isfinite(v)) continue;
                return Decision::raiseTo(static_cast<int>(std::nearbyint(std::min(v, 2e9))), PA_BETSIZE);
            }
            if (k == F_RAISE) return Decision::raiseTo(t.currentBet + std::max(t.lastRaiseSize, t.bblind), PA_RAISE);
            if (k == F_CALL) return Decision::call();
            if (k == F_CHECK) return Decision::check();
            if (k == F_FOLD) return Decision::fold();
        }
        return Decision::fold();
    }
    SymbolContext ctx_;
    bool script_;
};

std::mutex gProfileMu;
std::map<std::string, std::shared_ptr<FormulaCollection>> gLibraries;
std::map<std::string, std::shared_ptr<const FormulaCollection>> gProfiles;

}  // namespace

std::map<std::string, long> collectUnknownSymbols(bool reset) {
    std::lock_guard<std::mutex> lk(gUnknownMu);
    auto r = gUnknown;
    if (reset) gUnknown.clear();
    return r;
}

std::string findOhRoot(const std::string& start) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = fs::absolute(start.empty() ? "." : start, ec);
    for (int i = 0; i < 10 && !p.empty(); ++i) {
        if (fs::is_directory(p / "OpenPPL" / "OpenPPL_Library", ec)) return p.string();
        if (p == p.parent_path()) break;
        p = p.parent_path();
    }
    return "";
}

std::unique_ptr<IBrain> makeOhBrain(std::shared_ptr<const FormulaCollection> fc, const SimConfig& cfg, uint64_t seed, const std::string& name) {
    return std::make_unique<OhBrain>(std::move(fc), cfg, seed, name);
}

BrainFactory profileBrainFactory(const std::string& repoRoot) {
    return [repoRoot](const PlayerSpec& p, int, uint64_t seed, const SimConfig& cfg) -> std::unique_ptr<IBrain> {
        namespace fs = std::filesystem;
        const std::string pre = "builtin:";
        if (p.profile.rfind(pre, 0) == 0) return makeBuiltinBrain(p.profile.substr(pre.size()), seed, cfg);
        std::shared_ptr<const FormulaCollection> prof;
        {
            std::lock_guard<std::mutex> lk(gProfileMu);
            std::error_code ec;
            std::string key = p.profile + "|" + std::to_string(fs::exists(p.profile, ec) ? static_cast<long long>(fs::last_write_time(p.profile, ec).time_since_epoch().count()) : 0);
            auto it = gProfiles.find(key);
            if (it == gProfiles.end()) {
                std::string root = repoRoot.empty() ? findOhRoot(".") : repoRoot;
                if (root.empty()) throw std::runtime_error("Racine OpenHoldem introuvable (OpenPPL/OpenPPL_Library) : utilisez --oh-root");
                std::string libDir = (fs::path(root) / "OpenPPL" / "OpenPPL_Library").string();
                std::string botDir = (fs::path(root) / "##_OpenHoldem_Release_Directory_##" / "bot_logic" / "DefaultBot").string();
                auto lib = gLibraries.find(libDir);
                if (lib == gLibraries.end()) lib = gLibraries.emplace(libDir, loadLibrary(libDir, botDir)).first;
                if (!fs::exists(p.profile, ec) && p.profile != "DefaultBot") throw std::runtime_error("Profil introuvable : " + p.profile);
                it = gProfiles.emplace(key, buildProfile(p.profile, *lib->second)).first;
            }
            prof = it->second;
        }
        return makeOhBrain(prof, cfg, seed, p.name);
    };
}

}  // namespace ps::oh
