// Moteur Hold'em No Limit : cash, SNG, Spin & Go. Ne connaît que l'interface IBrain.
#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "pokersim/cards.hpp"
#include "pokersim/config.hpp"
#include "pokersim/rng.hpp"

namespace ps {

constexpr int PREFLOP = 1, FLOP = 2, TURN = 3, RIVER = 4;
// codes d'action OpenHoldem (prevaction)
constexpr int PA_UNDEFINED = -2, PA_FOLD = -1, PA_CHECK = 0, PA_CALL = 1, PA_RAISE = 2, PA_BETSIZE = 3, PA_ALLIN = 4;

enum class ActKind { Ante, SmallBlind, BigBlind, Fold, Check, Call, Bet, Raise };

struct Action {
    int round = PREFLOP, chair = 0;
    ActKind kind = ActKind::Fold;
    int amount = 0;     // jetons ajoutés
    int to = 0;         // mise totale du joueur sur le tour après l'action
    int code = PA_UNDEFINED;
    bool allin = false;
};

struct Decision {
    enum Kind { Fold, Check, Call, Raise, AllIn } kind = Fold;
    int to = 0;                 // mise totale visée pour Raise
    int code = PA_UNDEFINED;
    static Decision fold() { return {Fold, 0, PA_FOLD}; }
    static Decision check() { return {Check, 0, PA_CHECK}; }
    static Decision call() { return {Call, 0, PA_CALL}; }
    static Decision raiseTo(int to, int code = PA_BETSIZE) { return {Raise, to, code}; }
    static Decision allIn() { return {AllIn, 0, PA_ALLIN}; }
};

struct Seat {
    int chair = 0;
    std::string name;
    int profileId = 0;
    class IBrain* brain = nullptr;
    int stack = 0, bet = 0, committed = 0, startStack = 0;
    std::array<Card, 2> cards{kNoCard, kNoCard};
    bool inHand = false, allin = false, dealt = false, busted = false;
    std::array<std::vector<int>, 5> acts;   // codes d'action par tour (index 1..4)
    int prevAction = PA_UNDEFINED;
    int handsPlayed = 0, rebuys = 0;
    long long netChips = 0;
};

struct SeatRecord {
    int chair = 0, startStack = 0, endStack = 0, committed = 0, collected = 0;
    std::array<Card, 2> cards{};
    bool folded = false;
    std::string handType;
};
struct PotRecord { int amount = 0; std::vector<int> winners, eligible; bool uncalled = false; };

struct HandRecord {
    int game = 0, hand = 0, level = 0, sb = 0, bb = 0, ante = 0;
    double time = 0;
    int dealer = 0, sbChair = -1, bbChair = -1, rake = 0;
    std::vector<SeatRecord> seats;
    std::vector<Card> board;
    std::vector<Action> actions;
    std::vector<PotRecord> pots;
    bool showdown = false, flopSeen = false;
};

// État observable de la table
struct Table {
    int nchairs = 0, gameId = 0, dealer = 0, sbChair = -1, bbChair = -1;
    std::vector<Seat> seats;
    std::vector<Card> board;
    int betround = PREFLOP, sblind = 0, bblind = 0, ante = 0;
    std::vector<Action> actions;
    int potPrev = 0, handNo = 0, level = 0;
    double clock = 0, multiplier = 0, prizepool = 0, buyin = 0;
    int startingStack = 0, currentBet = 0, lastRaiseSize = 0;

    int pot() const { int p = potPrev; for (auto& s : seats) p += s.bet; return p; }
    int largestBet() const { int m = 0; for (auto& s : seats) if (s.inHand && s.bet > m) m = s.bet; return m; }
    int nInHand() const { int n = 0; for (auto& s : seats) n += s.inHand; return n; }
    int nextChair(int c) const { return (c + 1) % nchairs; }
};

class Game;

class IBrain {
public:
    virtual ~IBrain() = default;
    virtual void newGame(Game&) {}
    virtual void newHand(Game&, int /*chair*/) {}
    virtual Decision decide(Game& g, int chair) = 0;
    virtual void afterAction(Game&, int /*chair*/) {}
    virtual void endHand(Game&, const HandRecord&) {}
};

struct PlayerResult {
    int chair = 0, profileId = 0, place = 0, rebuys = 0, hands = 0;
    std::string name;
    double gain = 0, net = 0;       // en unités monétaires
    long long netChips = 0;
    double bb100 = 0;
};

struct GameResult {
    GameFormat format = GameFormat::SpinGo;
    int game = 0, hands = 0, rake = 0;
    double multiplier = 0, prizepool = 0, seconds = 0;
    std::vector<PlayerResult> players;
    std::vector<HandRecord> records;   // rempli si collectRecords
};

class Game {
public:
    Game(const SimConfig& cfg, std::vector<IBrain*> brains, std::vector<std::string> names,
         std::vector<int> profileIds, int gameId, uint64_t seed, bool collectRecords = false);
    GameResult play();

    Table t;
    Rng rng;
    const SimConfig& cfg;
    bool cash() const { return cfg.isCash(); }

private:
    struct Legal { Decision::Kind kind; int to; int code; bool aggr; };
    void blindsForState(int handsDone, int& lvl, int& sb, int& bb, int& ante) const;
    int aliveCount() const;
    void playHand();
    void bettingRound(int rnd, const std::vector<int>& order);
    Legal legalize(const Seat& s, const Decision& d, int toCall, bool canRaise) const;
    void apply(Seat& s, const Legal& d, int rnd, unsigned& acted);
    void log(int rnd, int chair, ActKind k, int amt, int to, int code, bool allin);
    void post(Seat& s, int amount, ActKind kind);
    int rakeFor(int base) const;
    struct Settle { bool showdown; std::vector<int> collected; std::vector<PotRecord> pots; std::vector<uint32_t> values; int rake; };
    Settle settle(const std::vector<Card>& boardCards);
    GameResult playCash();

    std::vector<HandRecord> records_;
    bool collect_;
    int handsPlayed_ = 0, totalRake_ = 0, prevDealer_ = 0;
    bool flopSeen_ = false;
    double handStartClock_ = 0;
    std::vector<int> finishOrder_;
    std::vector<BlindLevel> levels_;
};

// Tirage d'un multiplicateur dans la table (poids entiers)
double drawMultiplier(const std::vector<std::pair<double, int>>& table, Rng& rng);

}  // namespace ps
