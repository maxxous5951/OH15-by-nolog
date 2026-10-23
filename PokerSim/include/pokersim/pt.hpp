// PokerTracker simulé : statistiques d'adversaires calculées à partir des mains jouées, avec les définitions de PokerTracker 4
// (colonnes flg_* / cnt_* de *_hand_player_statistics, cf. PokerTracker_Query_Definitions). Les symboles OpenHoldem pt_<stat><siège>
// lisent ces statistiques : fraction 0..1 (facteur d'agression : ratio), -1 quand il n'y a pas de données.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "pokersim/engine.hpp"

namespace ps::pt {

enum class Mode { Cash = 0, Tourney = 1 };

#define PT_STATS(X) \
    X(HANDS, "hands") X(VPIP, "vpip") X(PFR, "pfr") X(RFI, "rfi") X(WTSD, "wtsd") X(WSSD, "wssd") \
    X(STEAL_ATTEMPT, "steal_attempt") X(BB_FOLD_TO_STEAL, "bb_fold_to_steal") X(SB_FOLD_TO_STEAL, "sb_fold_to_steal") \
    X(P3BET_VS_STEAL, "3bet_vs_steal") X(BB_3BET_VS_STEAL, "bb_3bet_vs_steal") X(SB_3BET_VS_STEAL, "sb_3bet_vs_steal") \
    X(PREFLOP_CALLED_RAISE, "preflop_called_raise") X(P3BET, "3bet") X(FOLD_TO_3BET, "fold_to_3bet") X(FOLD_TO_RESTEAL, "fold_to_resteal") \
    X(P4BET, "4bet") X(FOLD_TO_4BET, "fold_to_4bet") X(PREFLOP_SQUEEZE, "preflop_squeeze") X(FOLD_TO_PF_SQUEEZE, "fold_to_pf_squeeze") \
    X(FOLD_TO_SQUEEZE_AS_RAISER, "fold_to_squeeze_as_raiser") X(FOLD_TO_SQUEEZE_AS_CALLER, "fold_to_squeeze_as_caller") \
    X(FLOP_AF, "flop_af") X(TURN_AF, "turn_af") X(RIVER_AF, "river_af") X(TOTAL_AF, "total_af") \
    X(FLOP_AFQ, "flop_afq") X(TURN_AFQ, "turn_afq") X(RIVER_AFQ, "river_afq") X(TOTAL_AFQ, "total_afq") \
    X(FLOP_SEEN, "flop_seen") X(TURN_SEEN, "turn_seen") X(RIVER_SEEN, "river_seen") \
    X(FLOP_CBET, "flop_cbet") X(FLOP_FOLD_TO_CBET, "flop_fold_to_cbet") X(FLOP_RAISE_CBET, "flop_raise_cbet") \
    X(TURN_CBET, "turn_cbet") X(TURN_FOLD_TO_CBET, "turn_fold_to_cbet") X(RIVER_FOLD_TO_CBET, "river_fold_to_cbet") \
    X(FLOP_CHECKRAISE, "flop_checkraise") X(TURN_CHECKRAISE, "turn_checkraise") X(TURN_CHECKCALL, "turn_checkcall") \
    X(FLOP_DONKBET, "flop_donkbet") X(RIVER_BET, "river_bet")

enum Stat : int {
#define X(id, name) S_##id,
    PT_STATS(X)
#undef X
    S_COUNT
};

const char* statName(int stat);
int statIndex(const std::string& name);            // -1 si inconnue ou non supportée

struct PlayerCounters {
    double num[S_COUNT] = {0}, den[S_COUNT] = {0};
    void add(int stat, double n, double d) { num[stat] += n; den[stat] += d; }
    void merge(const PlayerCounters& o) { for (int i = 0; i < S_COUNT; ++i) { num[i] += o.num[i]; den[i] += o.den[i]; } }
    // valeur PokerTracker : numérateur / occasions ; -1 sans données. « hands » = nombre de mains.
    double value(int stat) const;
};

struct PlayerRecord { PlayerCounters mode[2]; };   // cash / tournoi (PokerTracker les garde séparés)

class Database {
public:
    std::map<std::string, PlayerRecord> players;   // clé = nom du joueur
    PlayerCounters& counters(const std::string& name, Mode m) { return players[name].mode[static_cast<int>(m)]; }
    const PlayerCounters* find(const std::string& name, Mode m) const;
    void merge(const Database& other);
    long long totalHands(Mode m) const;
    std::string toJson() const;
    static Database fromJson(const std::string& text);   // lève std::runtime_error
    bool save(const std::string& path) const;
    static Database load(const std::string& path);        // lève std::runtime_error
};

// Analyse une main et ajoute les compteurs de chaque joueur à `into`. names[chair] = nom du joueur.
void analyzeHand(const HandRecord& h, const std::vector<std::string>& names, Database& into, Mode mode);

// Suivi pendant une partie : base de départ (immuable, partagée) + mains de la partie en cours (mises à jour en fin de main)
class Tracker {
public:
    Tracker(std::shared_ptr<const Database> baseline, Mode mode) : base_(std::move(baseline)), mode_(mode) {}
    void update(const HandRecord& h, const std::vector<std::string>& names) { analyzeHand(h, names, delta_, mode_); }
    double stat(const std::string& player, int stat) const;   // -1 sans données
    const Database& delta() const { return delta_; }
    Mode mode() const { return mode_; }
private:
    std::shared_ptr<const Database> base_;
    Database delta_;
    Mode mode_;
};

}  // namespace ps::pt
