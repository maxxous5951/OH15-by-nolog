// Couche de symboles OpenHoldem : relie l'état de la partie (Game/Table) aux formules (ohlang) et
// transforme le résultat des formules en décision pour le moteur (OHBrain).
// Phase 3 : parité avec SpinGoSimulator/symbols.py ; prwin / nhands / handrank suivent l'original OpenHoldem.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include "pokersim/brains.hpp"
#include "pokersim/engine.hpp"
#include "pokersim/handrank.hpp"
#include "pokersim/ohlang.hpp"
#include "pokersim/pokerval.hpp"
#include "pokersim/prwin.hpp"

namespace ps::oh {

// ----------------------------------------------------------------- chargement des profils
// Bibliothèque OpenPPL (dossier OpenPPL_Library/*.ohf) + DefaultBot (fichiers de bot_logic/DefaultBot), partagée et immuable.
std::shared_ptr<FormulaCollection> loadLibrary(const std::string& libraryDir, const std::string& defaultBotDir);
// Profil utilisateur (.ohf / .oppl / .txt Shanky) superposé à la bibliothèque ; fonctions f$debug* ignorées.
std::shared_ptr<FormulaCollection> buildProfile(const std::string& path, const FormulaCollection& lib);
bool isOHScript(const FormulaCollection& fc);

// Diagnostic d'un profil (pour l'interface) : format, nombre de fonctions/listes, erreurs de syntaxe
struct ProfileReport {
    std::string path, format, error;       // error : échec de lecture
    int functions = 0, lists = 0;
    std::vector<std::pair<std::string, std::string>> problems;   // (fonction, message)
    bool ok() const { return error.empty() && problems.empty(); }
};
ProfileReport inspectProfile(const std::string& path, const std::string& ohRoot);

// ----------------------------------------------------------------- contexte de symboles
class SymbolContext : public IEval {
public:
    using Native = std::function<double(SymbolContext&)>;
    SymbolContext(std::shared_ptr<const FormulaCollection> fc, const SimConfig& cfg, uint64_t seed, std::string name);

    // cycle de vie (appelé par OHBrain)
    void newGame(Game& g);
    void newHand(Game& g, int chair);
    void prepare(Game& g, int chair);
    void afterAction(Game& g, int chair);
    double evaluateNamed(const std::string& name, bool* found = nullptr);

    // IEval
    double symbol(int id) override;
    double setVariable(int id) override;
    bool hasFunction(int id) override;

    // diagnostics : symboles inconnus rencontrés (nom -> nombre d'évaluations)
    const std::map<std::string, long long>& unknownSymbols() const { return unknown_; }
    void noteUnknown(const std::string& n) { ++unknown_[n]; }

    // ---- état lié (public : utilisé par la table de symboles)
    std::shared_ptr<const FormulaCollection> fc;
    const SimConfig& cfg;
    Rng rng;
    std::string name;
    Game* game = nullptr;
    Table* t = nullptr;
    Seat* me = nullptr;
    int chair = 0;
    std::vector<Card> hole, board;
    std::unordered_map<std::string, double> memory;
    std::set<std::string> uservars;
    std::map<std::pair<std::string, int>, double> hist;   // hi_* : (symbole, tour)
    uint32_t phandval[6] = {0}, chandval[6] = {0};
    double handRand = 0, beatRand = 0, roundRand[6] = {0};
    int roundsSeen = 0, handNo = -1;

    // ---- aides d'état (portage de symbols.py)
    double unit() const { return t->bblind > 0 ? static_cast<double>(t->bblind) : 1.0; }
    double largestBet() const { return static_cast<double>(t->largestBet()); }
    double callAmount() const;
    std::vector<const Action*> roundActions(int rnd = 0) const;
    std::vector<const Action*> aggr(int rnd = 0) const;
    int dealpos(int c) const;
    int betpos(int c) const;
    int chairByDealpos(int p) const;
    int logicalChair(int offset) const;
    std::vector<int> playing() const;
    std::vector<int> dealt() const;
    static int bitsOf(const std::vector<int>& v) { int b = 0; for (int c : v) b |= 1 << c; return b; }
    int raisbits(int rnd) const;
    int callbits(int rnd) const;
    int foldbits(int rnd) const;
    int raischair() const;
    int nTrulyRaising() const;
    int nCalling() const;
    double did(const char* kind, int rnd) const;
    double nplayersround(int rnd) const;
    double nbetsround(int rnd) const;
    int blindBits() const;
    double balanceSorted(int rank) const;
    double stackSorted(int rank) const;
    std::vector<int> aliveStacks() const;

    // ---- cartes
    struct PokerVal { int pv = 0, bits = 0, pvPlayer = 0, pvCommon = 0; };
    const PokerVal& pokervalAll();
    std::pair<int, std::array<int, 5>> handTypeInfo();
    struct CardStats { int tsuit = 0, nsuited = 0, tsuitcommon = 0, nsuitedcommon = 0, nranked = 0, trank = 0, nrankedcommon = 0, trankcommon = 0,
        nstraight = 0, nstraightfill = 0, nstraightcommon = 0, nstraightfillcommon = 0, nstraightflush = 0, nstraightflushfill = 0,
        nstraightflushcommon = 0, nstraightflushfillcommon = 0; };
    const CardStats& cardStats();
    std::pair<int, int> noutsBetter();
    double isHand(const std::string& name);
    double handBoardExpr(const std::string& name);
    double cardCode(const std::string& name);
    double isList(const std::string& num);
    // prwin & co (original OpenHoldem)
    int nopponentsForPrwin();
    PrwinResult prwinTriplet();
    NhandsResult nhands();
    HandrankValues handrank();
    // mémoire me_*
    double memorySymbol(const std::string& name);

    // ---- résolution des symboles
    struct Binding { enum Kind : uint8_t { None, Func, Nat, Unknown } kind = None; std::shared_ptr<OHFunction> fn; Native native; };
    Binding& binding(int id);
    Native resolveNative(const std::string& name);
    double callFunction(OHFunction& f);
    void bind(Game& g, int chair);
    void snapshotHistory();
    bool usesHiPrwin();

private:
    std::vector<Binding> bindings_;
    std::unordered_map<std::string, std::string> fnLower_;
    std::unordered_map<int, double> fnCache_;
    std::set<int> active_;
    int depth_ = 0;
    std::map<std::string, long long> unknown_;
    // caches par état lié
    bool pvOk_ = false, csOk_ = false, noutsOk_ = false, prwOk_ = false, nhOk_ = false, hrOk_ = false, hiPrwinKnown_ = false, hiPrwin_ = false;
    PokerVal pv_; CardStats cs_; std::pair<int, int> nouts_; PrwinResult prw_; NhandsResult nh_; HandrankValues hr_;
};

// ----------------------------------------------------------------- cerveau
std::unique_ptr<IBrain> makeOhBrain(std::shared_ptr<const FormulaCollection> fc, const SimConfig& cfg, uint64_t seed, const std::string& name);
// Symboles inconnus rencontrés pendant la simulation, cumulés sur tous les cerveaux (nom -> évaluations)
std::map<std::string, long long> collectUnknownSymbols(bool reset = true);
// Racine d'un dépôt OpenHoldem (contient OpenPPL/OpenPPL_Library) en remontant depuis `start`, "" si introuvable
std::string findOhRoot(const std::string& start);
// Usine complète : "builtin:*", sinon fichier de profil ; library/defaultBot résolus depuis `repoRoot`
BrainFactory profileBrainFactory(const std::string& repoRoot);

}  // namespace ps::oh
