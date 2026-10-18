// Langage de formules OpenHoldem : OH-script + OpenPPL.
// Port du tokenizer / parseur de SpinGoSimulator/ohlang.py, avec la sémantique des opérateurs de
// OpenHoldem (CParseTreeOperatorNode.cpp) et des listes de mains (COHScriptList.cpp).
#pragma once
#include <bitset>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "pokersim/rng.hpp"

namespace ps::oh {

struct ParseError : std::runtime_error { using std::runtime_error::runtime_error; };

// ---------------------------------------------------------------- noms internés
// Les identifiants sont internés une fois : l'évaluation travaille sur des entiers.
class NameTable {
public:
    int intern(const std::string& s);
    int find(const std::string& s) const;   // -1 si absent
    std::string name(int id) const;
    int size() const;
private:
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, int> map_;
    std::deque<std::string> names_;
};
NameTable& names();

// Identifiants connus d'avance (symboles utilisés par RaiseBy)
enum WellKnownName : int { N_ncallbets = 0, N_bet, N_pot, N_nbetstocall, N_WellKnownCount };

// ---------------------------------------------------------------- évaluation
class IEval {
public:
    virtual ~IEval() = default;
    virtual double symbol(int nameId) = 0;        // fonction utilisateur ou symbole natif
    virtual double setVariable(int nameId) = 0;   // « Set user_xxx »
    virtual bool hasFunction(int nameId) = 0;     // pour la fin de séquence WHEN (DefaultPreflop...)
};

constexpr double kUndefined = -1.0;   // valeur renvoyée par OpenHoldem (division par zéro...)

// ---------------------------------------------------------------- tokens
enum class Tok { Eof, Num, Ident, Cards, Op, Open, Close, When, Force, Return, Set, Shebang };
enum class Op {
    None, Add, Sub, Mul, Div, Mod, Pct, Pow, Eq, Approx, Ne, Lt, Le, Gt, Ge, And, Or, Xor,
    BitAnd, BitOr, BitXor, Shl, Shr, Cond, Colon, Neg, Ln, BitNot, Not, BitCount
};
const char* opName(Op op);

struct Token { Tok kind = Tok::Eof; std::string text; Op op = Op::None; int line = 0; };
std::vector<Token> tokenize(const std::string& text);
double stringToNumber(const std::string& s, bool& ok);

// ---------------------------------------------------------------- AST
enum class NK { Num, Ident, Unary, Bracket, Binary, Ternary, UserVarSet, End, Betsize, When };

struct Node {
    explicit Node(NK k) : kind(k) {}
    virtual ~Node() = default;
    virtual double ev(IEval& c) const = 0;
    virtual void dump(std::string& out, std::unordered_map<const Node*, int>& ids) const = 0;
    NK kind;
};

struct Num : Node { double value; explicit Num(double v) : Node(NK::Num), value(v) {}
    double ev(IEval&) const override { return value; }
    void dump(std::string&, std::unordered_map<const Node*, int>&) const override; };
struct Ident : Node { int id; explicit Ident(int i) : Node(NK::Ident), id(i) {}
    double ev(IEval& c) const override { return c.symbol(id); }
    void dump(std::string&, std::unordered_map<const Node*, int>&) const override; };
struct Unary : Node { Op op; const Node* a; Unary(Op o, const Node* x) : Node(NK::Unary), op(o), a(x) {}
    double ev(IEval& c) const override;
    void dump(std::string&, std::unordered_map<const Node*, int>&) const override; };
struct Bracket : Node { const Node* a; explicit Bracket(const Node* x) : Node(NK::Bracket), a(x) {}
    double ev(IEval& c) const override { return a->ev(c); }
    void dump(std::string&, std::unordered_map<const Node*, int>&) const override; };
struct Binary : Node { Op op; const Node *a, *b; Binary(Op o, const Node* x, const Node* y) : Node(NK::Binary), op(o), a(x), b(y) {}
    double ev(IEval& c) const override;
    void dump(std::string&, std::unordered_map<const Node*, int>&) const override; };
struct Ternary : Node { const Node *c, *a, *b; Ternary(const Node* cc, const Node* x, const Node* y) : Node(NK::Ternary), c(cc), a(x), b(y) {}
    double ev(IEval& ctx) const override { return c->ev(ctx) != 0.0 ? a->ev(ctx) : b->ev(ctx); }
    void dump(std::string&, std::unordered_map<const Node*, int>&) const override; };
struct UserVarSet : Node { int id; explicit UserVarSet(int i) : Node(NK::UserVarSet), id(i) {}
    double ev(IEval& c) const override { return c.setVariable(id); }
    void dump(std::string&, std::unordered_map<const Node*, int>&) const override; };
struct EndOfFunction : Node { int fallback;   // nom interné de DefaultPreflop... ou -1
    explicit EndOfFunction(int f) : Node(NK::End), fallback(f) {}
    double ev(IEval& c) const override { return (fallback >= 0 && c.hasFunction(fallback)) ? c.symbol(fallback) : 0.0; }
    void dump(std::string&, std::unordered_map<const Node*, int>&) const override; };
struct BetsizeAction : Node { enum Mode { To, By, Pct } mode; const Node* expr;
    BetsizeAction(Mode m, const Node* e) : Node(NK::Betsize), mode(m), expr(e) {}
    double ev(IEval& c) const override;
    void dump(std::string&, std::unordered_map<const Node*, int>&) const override; };
struct When : Node { const Node* cond; const Node* second = nullptr; const Node* third = nullptr;
    explicit When(const Node* c) : Node(NK::When), cond(c) {}
    bool isOpenEnded() const { return (!second && !third) || (second && second->kind == NK::When); }
    double ev(IEval& c) const override;    // itératif le long de la chaîne « third »
    void dump(std::string&, std::unordered_map<const Node*, int>&) const override; };

class Arena {
public:
    template <class T, class... A> T* make(A&&... a) { auto p = std::make_unique<T>(std::forward<A>(a)...); T* r = p.get(); nodes_.push_back(std::move(p)); return r; }
private:
    std::vector<std::unique_ptr<Node>> nodes_;
};

// Analyse le corps d'une fonction ; `fallbackName` = DefaultPreflop... pour f$preflop etc. ("" sinon)
const Node* parseFunctionBody(const std::string& text, const std::string& fname, const std::string& fallbackName, Arena& arena);
std::string dumpTree(const Node* n);

// ---------------------------------------------------------------- listes de mains (COHScriptList)
class HandList {
public:
    HandList() { clear(); }
    void clear();
    // Analyse le corps d'une liste ; renvoie false (et remplit error) si un membre est invalide
    bool parse(const std::string& body, std::string& error);
    bool setMember(const std::string& member, std::string& error);
    // rangs OpenHoldem 2..14 ; tirage aléatoire pondéré comme OpenHoldem (poids >= rand()%101)
    bool isOnList(int rank1, int rank2, bool suited, Rng& rng) const;
    bool isOnListNoRandom(int rank1, int rank2, bool suited) const;   // sans pondération (tests)
    int weightOf(int rank1, int rank2, bool suited) const;
    int nHands() const;
private:
    void set(int first, int second, bool suited, int weight);
    bool matrix_[13][13];
    uint8_t weight_[13][13];
};

// ---------------------------------------------------------------- collection de fonctions
struct OHFunction {
    std::string name, text, path;
    int line = 0;
    bool isList = false, readOnly = false, parsed = false;
    std::string error;
    std::shared_ptr<Arena> arena;
    const Node* tree = nullptr;
    std::shared_ptr<HandList> hands;
};

class FormulaCollection {
public:
    void addText(const std::string& text, const std::string& path, bool readOnly);
    // Détecte le format (.ohf / .oppl / Shanky .txt) et ajoute les fonctions
    void addFile(const std::string& path, bool readOnly);
    bool has(const std::string& name) const { return functions.count(name) != 0; }
    std::shared_ptr<OHFunction> get(const std::string& name) const;
    void parseAll();
    void parseOne(OHFunction& f);
    bool isOpenPPLProfile() const;
    std::vector<std::pair<std::string, std::string>> errors(bool includeReadOnly) const;

    std::unordered_map<std::string, std::shared_ptr<OHFunction>> functions;
    std::string profilePath, profileName;
};

std::vector<OHFunction> splitFormulaText(const std::string& text, const std::string& path, bool readOnly);
std::string defaultFunctionFor(const std::string& name);   // "f$preflop" -> "DefaultPreflop"
std::string readFileLatin1(const std::string& path);      // octets bruts, BOM UTF-8 retiré

// ---------------------------------------------------------------- PPL « Shanky » (.txt / .oPPL ancien format)
bool isShanky(const std::string& text);
std::string shankyToOhf(const std::string& text);   // texte .ohf avec ##f$preflop## ... et ##listNNN##

}  // namespace ps::oh
