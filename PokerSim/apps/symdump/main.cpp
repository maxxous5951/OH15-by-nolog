// symdump : évalue des symboles natifs sur des états de table fournis en JSON (test différentiel contre symbols.py).
//   symdump scenarios.json  ->  JSON { "results": [ [valeur|null, ...] par scénario ] }
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include "nlohmann/json.hpp"
#include "pokersim/ohsymbols.hpp"

using namespace ps;
using json = nlohmann::json;

static ActKind kindOf(const std::string& k) {
    static const std::map<std::string, ActKind> m = {{"sb", ActKind::SmallBlind}, {"bb", ActKind::BigBlind}, {"ante", ActKind::Ante}, {"fold", ActKind::Fold},
        {"check", ActKind::Check}, {"call", ActKind::Call}, {"bet", ActKind::Bet}, {"raise", ActKind::Raise}};
    auto it = m.find(k);
    return it == m.end() ? ActKind::Raise : it->second;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: symdump scenarios.json\n"); return 2; }
    std::ifstream f(argv[1]);
    std::stringstream ss; ss << f.rdbuf();
    json in = json::parse(ss.str());
    const auto& symbols = in["symbols"];
    json out; out["results"] = json::array();
    for (const auto& sc : in["scenarios"]) {
        SimConfig cfg;
        cfg.format = sc["cash"].get<bool>() ? GameFormat::Cash : GameFormat::SNG;   // SNG : pas de tirage de multiplicateur
        const int n = sc["nchairs"].get<int>();
        cfg.numPlayers = n; cfg.levelSeconds = sc["level_seconds"].get<int>(); cfg.startingStack = sc["starting_stack"].get<int>();
        cfg.cashStack = cfg.startingStack; cfg.handBaseSeconds = sc["hand_base"].get<double>(); cfg.actionSeconds = sc["action_sec"].get<double>();
        cfg.payoutShares = sc["payout"].get<std::vector<double>>();
        for (int i = 0; i < n; ++i) cfg.players.push_back({"p" + std::to_string(i), "builtin:call"});
        std::vector<std::unique_ptr<IBrain>> owned; std::vector<IBrain*> brains; std::vector<std::string> names; std::vector<int> ids;
        for (int i = 0; i < n; ++i) { owned.push_back(makeBuiltinBrain("call", 1, cfg)); brains.push_back(owned.back().get()); names.push_back("p" + std::to_string(i)); ids.push_back(i); }
        cfg.numGames = 1;
        Game g(cfg, brains, names, ids, 0, 1, false);
        Table& t = g.t;
        t.dealer = sc["dealer"]; t.sbChair = sc["sb_chair"]; t.bbChair = sc["bb_chair"]; t.betround = sc["betround"];
        t.sblind = sc["sblind"]; t.bblind = sc["bblind"]; t.ante = 0; t.potPrev = sc["pot_prev"]; t.clock = sc["clock"];
        t.buyin = sc["buyin"]; t.prizepool = sc["prizepool"]; t.multiplier = sc["multiplier"]; t.currentBet = sc["current_bet"];
        t.lastRaiseSize = sc["last_raise_size"]; t.startingStack = cfg.startingStack;
        t.board.clear();
        for (auto& c : sc["board"]) t.board.push_back(cardFromString(c.get<std::string>().c_str()));
        for (int i = 0; i < n; ++i) {
            const auto& js = sc["seats"][i];
            Seat& s = t.seats[i];
            s.stack = js["stack"]; s.bet = js["bet"]; s.committed = js["committed"]; s.startStack = js["start_stack"];
            s.inHand = js["in_hand"]; s.allin = js["allin"]; s.dealt = js["dealt"]; s.prevAction = js["prevaction"]; s.handsPlayed = js["hands_played"];
            s.cards = {cardFromString(js["cards"][0].get<std::string>().c_str()), cardFromString(js["cards"][1].get<std::string>().c_str())};
            for (int r = 1; r <= 4; ++r) { s.acts[r].clear(); for (auto& c : js["acts"][std::to_string(r)]) s.acts[r].push_back(c); }
        }
        t.actions.clear();
        for (auto& ja : sc["actions"]) {
            Action a; a.round = ja["r"]; a.chair = ja["c"]; a.kind = kindOf(ja["k"].get<std::string>()); a.amount = ja["amt"]; a.to = ja["to"];
            t.actions.push_back(a);
        }
        auto fc = std::make_shared<oh::FormulaCollection>();
        oh::SymbolContext ctx(fc, cfg, 1, "x");
        ctx.bind(g, sc["chair"]);
        json row = json::array();
        for (const auto& sym : symbols) {
            auto fn = ctx.resolveNative(sym.get<std::string>());
            if (!fn) row.push_back(nullptr);
            else { double v = fn(ctx); row.push_back(std::isfinite(v) ? json(v) : json(nullptr)); }
        }
        out["results"].push_back(row);
    }
    std::cout << out.dump() << "\n";
    return 0;
}
