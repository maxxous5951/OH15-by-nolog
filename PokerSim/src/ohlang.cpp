#include "pokersim/ohlang.hpp"
#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>

namespace ps::oh {

// ====================================================================== noms internés
int NameTable::intern(const std::string& s) {
    {
        std::shared_lock<std::shared_mutex> lk(mu_);
        auto it = map_.find(s);
        if (it != map_.end()) return it->second;
    }
    std::unique_lock<std::shared_mutex> lk(mu_);
    auto it = map_.find(s);
    if (it != map_.end()) return it->second;
    int id = static_cast<int>(names_.size());
    names_.push_back(s);
    map_.emplace(s, id);
    return id;
}
int NameTable::find(const std::string& s) const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    auto it = map_.find(s);
    return it == map_.end() ? -1 : it->second;
}
std::string NameTable::name(int id) const {
    std::shared_lock<std::shared_mutex> lk(mu_);
    return (id >= 0 && id < static_cast<int>(names_.size())) ? names_[id] : std::string();
}
int NameTable::size() const { std::shared_lock<std::shared_mutex> lk(mu_); return static_cast<int>(names_.size()); }

NameTable& names() {
    static NameTable* t = [] {
        auto* n = new NameTable();
        n->intern("ncallbets"); n->intern("bet"); n->intern("pot"); n->intern("nbetstocall");   // ordre = WellKnownName
        return n;
    }();
    return *t;
}

const char* opName(Op op) {
    switch (op) {
        case Op::Add: return "+"; case Op::Sub: return "-"; case Op::Mul: return "*"; case Op::Div: return "/";
        case Op::Mod: return "mod"; case Op::Pct: return "%pct"; case Op::Pow: return "**"; case Op::Eq: return "==";
        case Op::Approx: return "~~"; case Op::Ne: return "!="; case Op::Lt: return "<"; case Op::Le: return "<=";
        case Op::Gt: return ">"; case Op::Ge: return ">="; case Op::And: return "&&"; case Op::Or: return "||";
        case Op::Xor: return "^^"; case Op::BitAnd: return "&"; case Op::BitOr: return "|"; case Op::BitXor: return "^";
        case Op::Shl: return "<<"; case Op::Shr: return ">>"; case Op::Cond: return "?"; case Op::Colon: return ":";
        case Op::Neg: return "neg"; case Op::Ln: return "ln"; case Op::BitNot: return "~"; case Op::Not: return "!";
        case Op::BitCount: return "`"; default: return "";
    }
}

// ====================================================================== tokenizer
namespace {

// Texte lu en latin-1 : les octets >= 0xC0 (sauf × et ÷) sont des lettres, 0x85/0xA0 des espaces (comme Python).
bool isSpaceCh(unsigned char c) { return c == ' ' || (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x1f) || c == 0x85 || c == 0xa0; }
bool isAlphaCh(unsigned char c) { return std::isalpha(c) || (c >= 0xC0 && c != 0xD7 && c != 0xF7); }
bool isDigitCh(unsigned char c) { return c >= '0' && c <= '9'; }
bool isAlnumCh(unsigned char c) { return isAlphaCh(c) || isDigitCh(c); }
bool isHexCh(unsigned char c) { return isDigitCh(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool isCardRankCh(unsigned char c) { return std::strchr("23456789TJQKAtjqka", c) != nullptr && c != 0; }
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

struct Keyword { const char* word; Op op; };
const Keyword kKeywords[] = {{"or", Op::Or}, {"and", Op::And}, {"not", Op::Not}, {"xor", Op::Xor}, {"mod", Op::Mod}, {"ln", Op::Ln},
                             {"bitor", Op::BitOr}, {"bitand", Op::BitAnd}, {"bitxor", Op::BitXor}, {"bitnot", Op::BitNot}, {"bitcount", Op::BitCount}};

const char* const kActions[] = {"Bet", "Call", "Fold", "Play", "Beep", "Raise", "RaiseTo", "RaiseBy", "Check", "Allin", "BetMin",
    "BetFourthPot", "BetThirdPot", "BetHalfPot", "BetTwoThirdPot", "BetThreeFourthPot", "BetPot", "BetMax", "RaiseMin",
    "RaiseFourthPot", "RaiseThirdPot", "RaiseHalfPot", "RaiseTwoThirdPot", "RaiseThreeFourthPot", "RaisePot", "RaiseMax", "SitOut"};
const char* canonicalAction(const std::string& word) {
    std::string l = lower(word);
    for (const char* a : kActions) if (lower(a) == l) return a;
    return nullptr;
}

}  // namespace

double stringToNumber(const std::string& str, bool& ok) {
    ok = false;
    std::string s = str;
    while (!s.empty() && isSpaceCh(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && isSpaceCh(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::string low = lower(s);
    bool neg = false;
    if (!low.empty() && low[0] == '-') { neg = true; low.erase(0, 1); }
    double v = 0;
    if (low.rfind("0x", 0) == 0) {
        if (low.size() == 2) return 0;
        for (size_t i = 2; i < low.size(); ++i) { if (!isHexCh(static_cast<unsigned char>(low[i]))) return 0; v = v * 16 + (isDigitCh(low[i]) ? low[i] - '0' : low[i] - 'a' + 10); }
    } else if (low.rfind("0b", 0) == 0) {
        if (low.size() == 2) return 0;
        for (size_t i = 2; i < low.size(); ++i) { if (low[i] != '0' && low[i] != '1') return 0; v = v * 2 + (low[i] - '0'); }
    } else {
        if (low.empty()) return 0;
        char* end = nullptr;
        v = std::strtod(low.c_str(), &end);
        if (end != low.c_str() + low.size()) return 0;
        for (char c : low) if (!(isDigitCh(static_cast<unsigned char>(c)) || c == '.' || c == 'e' || c == '+' || c == '-')) return 0;   // pas de inf/nan/hex-float
    }
    ok = true;
    return neg ? -v : v;
}

std::vector<Token> tokenize(const std::string& text) {
    std::vector<Token> toks;
    const size_t n = text.size();
    size_t i = 0;
    int line = 1;
    bool insideOpenppl = false;
    Tok last = Tok::Eof;
    bool haveLast = false;
    auto push = [&](Tok k, std::string t, Op op = Op::None) { toks.push_back({k, std::move(t), op, line}); last = k; haveLast = true; };
    auto binaryMinusContext = [&]() { return haveLast && (last == Tok::Ident || last == Tok::Num || last == Tok::Close || last == Tok::Cards); };
    while (i < n) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch == '\n') { ++line; ++i; continue; }
        if (isSpaceCh(ch)) { ++i; continue; }
        if (ch == '/' && i + 1 < n && text[i + 1] == '/') { while (i < n && text[i] != '\n') ++i; continue; }
        if (ch == '/' && i + 1 < n && text[i + 1] == '*') {
            size_t j = text.find("*/", i + 2);
            if (j == std::string::npos) throw ParseError("Commentaire multi-lignes non terminé (ligne " + std::to_string(line) + ")");
            line += static_cast<int>(std::count(text.begin() + i, text.begin() + j, '\n'));
            i = j + 2;
            continue;
        }
        if (isAlphaCh(ch) || ch == '$') {
            size_t j = i;
            while (j < n && (isAlnumCh(static_cast<unsigned char>(text[j])) || text[j] == '$' || text[j] == '_')) ++j;
            std::string word = text.substr(i, j - i), low = lower(word);
            i = j;
            bool kw = false;
            for (auto& k : kKeywords) if (low == k.word) { push(Tok::Op, word, k.op); kw = true; break; }
            if (kw) continue;
            if (low == "when") { insideOpenppl = true; push(Tok::When, word); continue; }
            if (low == "force") { push(Tok::Force, word); continue; }
            if (low == "return") { push(Tok::Return, word); continue; }
            if (low == "set") { push(Tok::Set, word); continue; }
            push(Tok::Ident, word);
            continue;
        }
        if (isDigitCh(ch) || ch == '.') {
            if (i + 2 < n && isCardRankCh(static_cast<unsigned char>(text[i + 1])) && (std::tolower(static_cast<unsigned char>(text[i + 2])) == 's' || std::tolower(static_cast<unsigned char>(text[i + 2])) == 'o')) {
                size_t j = i;
                while (j < n && isAlnumCh(static_cast<unsigned char>(text[j]))) ++j;
                push(Tok::Cards, text.substr(i, j - i));
                i = j;
                continue;
            }
            size_t j = i;
            while (j < n && (isHexCh(static_cast<unsigned char>(text[j])) || text[j] == '.' || text[j] == 'x' || text[j] == 'X' ||
                             ((text[j] == 'b' || text[j] == 'B') && j == i + 1 && text[i] == '0'))) ++j;
            push(Tok::Num, text.substr(i, j - i));
            i = j;
            continue;
        }
        if (std::strchr("+-*/<>`=!?:%&|~^", ch) && ch != 0) {
            const char two1 = i + 1 < n ? text[i + 1] : '\0';
            Op op = Op::None;
            size_t adv = 1;
            switch (ch) {
                case '+': op = Op::Add; break;
                case '-':
                    if (binaryMinusContext()) op = Op::Sub;
                    else if (i + 1 < n && isDigitCh(static_cast<unsigned char>(text[i + 1]))) {
                        size_t j = i + 1;
                        while (j < n && (isHexCh(static_cast<unsigned char>(text[j])) || text[j] == '.' || text[j] == 'x' || text[j] == 'X')) ++j;
                        push(Tok::Num, text.substr(i, j - i));
                        i = j;
                        continue;
                    } else op = Op::Neg;
                    break;
                case '*': if (two1 == '*') { op = Op::Pow; adv = 2; } else op = Op::Mul; break;
                case '/': op = Op::Div; break;
                case '<': if (two1 == '=') { op = Op::Le; adv = 2; } else if (two1 == '<') { op = Op::Shl; adv = 2; } else op = Op::Lt; break;
                case '>': if (two1 == '=') { op = Op::Ge; adv = 2; } else if (two1 == '>') { op = Op::Shr; adv = 2; } else op = Op::Gt; break;
                case '`': op = Op::BitCount; break;
                case '=': op = Op::Eq; adv = (two1 == '=') ? 2 : 1; break;
                case '!': if (two1 == '=') { op = Op::Ne; adv = 2; } else op = Op::Not; break;
                case '?': op = Op::Cond; break;
                case ':': op = Op::Colon; break;
                case '%': op = insideOpenppl ? Op::Pct : Op::Mod; break;
                case '&': if (two1 == '&') { op = Op::And; adv = 2; } else op = Op::BitAnd; break;
                case '|': if (two1 == '|') { op = Op::Or; adv = 2; } else op = Op::BitOr; break;
                case '~': if (two1 == '~') { op = Op::Approx; adv = 2; } else op = Op::BitNot; break;
                case '^': if (two1 == '^') { op = Op::Xor; adv = 2; } else op = Op::BitXor; break;
            }
            push(Tok::Op, text.substr(i, adv), op);
            i += adv;
            continue;
        }
        if (ch == '(' || ch == '[' || ch == '{') { push(Tok::Open, std::string(1, static_cast<char>(ch))); ++i; continue; }
        if (ch == ')' || ch == ']' || ch == '}') { push(Tok::Close, std::string(1, static_cast<char>(ch))); ++i; continue; }
        if (ch == '#' && i + 1 < n && text[i + 1] == '#') { push(Tok::Shebang, "##"); i += 2; insideOpenppl = false; continue; }
        if (ch == '"') {
            size_t j = text.find('"', i + 1);
            if (j == std::string::npos) j = n - 1;
            push(Tok::Ident, text.substr(i, j + 1 - i));
            i = j + 1;
            continue;
        }
        throw ParseError(std::string("Caractère inattendu '") + static_cast<char>(ch) + "' (ligne " + std::to_string(line) + ")");
    }
    toks.push_back({Tok::Eof, "", Op::None, line});
    return toks;
}

// ====================================================================== évaluation
namespace {
constexpr double kEps = 1e-6;
uint32_t toU32(double v) {   // (unsigned long) sous Windows = 32 bits
    if (!(v == v)) return 0;
    if (v >= 9.2e18) return static_cast<uint32_t>(INT64_MAX);
    if (v <= -9.2e18) return static_cast<uint32_t>(INT64_MIN);
    return static_cast<uint32_t>(static_cast<int64_t>(v));
}
int roundHalfUp(double d) {
    double x = d + 0.5;
    if (!(x == x)) return 0;
    if (x >= 2147483647.0) return INT32_MAX;
    if (x <= -2147483648.0) return INT32_MIN;
    return static_cast<int>(x);
}
}  // namespace

double Unary::ev(IEval& c) const {
    double v = a->ev(c);
    switch (op) {
        case Op::Neg: return 0.0 - v;
        case Op::Not: return v != 0.0 ? 0.0 : 1.0;
        case Op::Ln: return std::log(v);
        case Op::BitNot: return static_cast<double>(~toU32(v));
        case Op::BitCount: return static_cast<double>(std::popcount(toU32(v)));
        default: return v;
    }
}

double Binary::ev(IEval& c) const {
    double x = a->ev(c);
    if (op == Op::And) { if (x == 0.0) return 0.0; return b->ev(c) != 0.0 ? 1.0 : 0.0; }
    if (op == Op::Or) { if (x != 0.0) return 1.0; return b->ev(c) != 0.0 ? 1.0 : 0.0; }
    double y = b->ev(c);
    switch (op) {
        case Op::Add: return x + y;
        case Op::Sub: return x - y;
        case Op::Mul: return x * y;
        case Op::Div: return y == 0.0 ? kUndefined : x / y;
        case Op::Mod: { if (y == 0.0) return kUndefined; uint32_t d = toU32(y); return d == 0 ? kUndefined : static_cast<double>(toU32(x) % d); }
        case Op::Pow: return std::pow(x, y);
        case Op::Eq: return (x > y - kEps && x < y + kEps) ? 1.0 : 0.0;
        case Op::Approx: return roundHalfUp(x) == roundHalfUp(y) ? 1.0 : 0.0;
        case Op::Ne: return x != y ? 1.0 : 0.0;
        case Op::Lt: return x <= y - kEps ? 1.0 : 0.0;
        case Op::Le: return x < y + kEps ? 1.0 : 0.0;
        case Op::Gt: return x >= y + kEps ? 1.0 : 0.0;
        case Op::Ge: return x > y - kEps ? 1.0 : 0.0;
        case Op::Xor: return ((x != 0.0) != (y != 0.0)) ? 1.0 : 0.0;
        case Op::BitAnd: return static_cast<double>(toU32(x) & toU32(y));
        case Op::BitOr: return static_cast<double>(toU32(x) | toU32(y));
        case Op::BitXor: return static_cast<double>(toU32(x) ^ toU32(y));
        case Op::Shl: return static_cast<double>(toU32(x) << (toU32(y) & 31u));   // comme x86 : compteur masqué à 5 bits
        case Op::Shr: return static_cast<double>(toU32(x) >> (toU32(y) & 31u));
        case Op::Pct: return x * y * 0.01;
        default: return 0.0;
    }
}

double BetsizeAction::ev(IEval& c) const {
    double v = expr->ev(c);
    if (mode == To) return v;
    if (mode == By) return c.symbol(N_ncallbets) + v;
    double bet = c.symbol(N_bet);
    double potAfterCall = (bet != 0.0 ? c.symbol(N_pot) / bet : 0.0) + c.symbol(N_nbetstocall);
    return c.symbol(N_ncallbets) + 0.01 * v * potAfterCall;
}

double When::ev(IEval& c) const {
    const When* w = this;
    for (;;) {
        if (w->cond->ev(c) != 0.0) {
            double val = w->second ? w->second->ev(c) : 0.0;
            if (!(w->second && w->second->kind == NK::UserVarSet)) return val;   // variable utilisateur : on continue
        }
        const Node* t = w->third;
        if (!t) return 0.0;
        if (t->kind == NK::When) { w = static_cast<const When*>(t); continue; }
        return t->ev(c);
    }
}

// ====================================================================== dump (tests différentiels)
namespace {
std::string fmtNum(double v) { char b[64]; std::snprintf(b, sizeof b, "%.17g", v); return b; }
}

void Num::dump(std::string& o, std::unordered_map<const Node*, int>&) const { o += "(num " + fmtNum(value) + ")"; }
void Ident::dump(std::string& o, std::unordered_map<const Node*, int>&) const { o += "(id " + names().name(id) + ")"; }
void Unary::dump(std::string& o, std::unordered_map<const Node*, int>& m) const { o += std::string("(u ") + opName(op) + " "; a->dump(o, m); o += ")"; }
void Bracket::dump(std::string& o, std::unordered_map<const Node*, int>& m) const { o += "(( "; a->dump(o, m); o += ")"; }
void Binary::dump(std::string& o, std::unordered_map<const Node*, int>& m) const { o += std::string("(b ") + opName(op) + " "; a->dump(o, m); o += " "; b->dump(o, m); o += ")"; }
void Ternary::dump(std::string& o, std::unordered_map<const Node*, int>& m) const { o += "(? "; c->dump(o, m); o += " "; a->dump(o, m); o += " "; b->dump(o, m); o += ")"; }
void UserVarSet::dump(std::string& o, std::unordered_map<const Node*, int>&) const { o += "(set " + names().name(id) + ")"; }
void BetsizeAction::dump(std::string& o, std::unordered_map<const Node*, int>& m) const {
    o += std::string("(bs ") + (mode == To ? "to" : mode == By ? "by" : "pct") + " "; expr->dump(o, m); o += ")";
}
void EndOfFunction::dump(std::string& o, std::unordered_map<const Node*, int>& m) const {
    auto it = m.find(this);
    if (it != m.end()) { o += "(ref " + std::to_string(it->second) + ")"; return; }
    int id = static_cast<int>(m.size()); m[this] = id;
    o += "(end#" + std::to_string(id) + " " + (fallback >= 0 ? names().name(fallback) : std::string("-")) + ")";
}
void When::dump(std::string& o, std::unordered_map<const Node*, int>& m) const {
    auto it = m.find(this);
    if (it != m.end()) { o += "(ref " + std::to_string(it->second) + ")"; return; }
    int id = static_cast<int>(m.size()); m[this] = id;
    o += "(when#" + std::to_string(id) + " "; cond->dump(o, m);
    o += " "; if (second) second->dump(o, m); else o += "nil";
    o += " "; if (third) third->dump(o, m); else o += "nil";
    o += ")";
}
std::string dumpTree(const Node* n) {
    std::string out; std::unordered_map<const Node*, int> ids;
    if (n) n->dump(out, ids); else out = "nil";
    return out;
}

// ====================================================================== parseur
namespace {

int priority(Op op) {
    switch (op) {
        case Op::Add: case Op::Sub: return 11;
        case Op::Mul: case Op::Div: case Op::Mod: case Op::Pct: return 12;
        case Op::Pow: return 14;
        case Op::Eq: case Op::Approx: case Op::Ne: return 8;
        case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: return 9;
        case Op::And: return 4; case Op::Or: return 2; case Op::Xor: return 3;
        case Op::BitAnd: return 7; case Op::BitOr: return 5; case Op::BitXor: return 6;
        case Op::Shl: case Op::Shr: return 10;
        case Op::Cond: return 1;
        default: return -1;
    }
}
bool isUnary(Op op) { return op == Op::Neg || op == Op::Ln || op == Op::BitNot || op == Op::Not || op == Op::BitCount; }

class Parser {
public:
    Parser(std::vector<Token> t, std::string fname, int fallback, Arena& a) : toks_(std::move(t)), fname_(std::move(fname)), fallback_(fallback), arena_(a) {}

    const Node* parseFunctionBody() {
        const Token& t = peek();
        if (t.kind == Tok::Eof) return arena_.make<EndOfFunction>(fallback_);
        if (t.kind == Tok::When) {
            When* first = parseWhenSequence();
            backpatch(first);
            return first;
        }
        const Node* e = parseExpression(0);
        if (peek().kind != Tok::Eof) {
            if (peek().kind == Tok::When) error("Condition WHEN après une expression OH-script");
            error("Tokens superflus après la fin de la fonction");
        }
        return e;
    }

private:
    const Token& peek(size_t k = 0) const { size_t p = pos_ + k; return p >= toks_.size() ? toks_.back() : toks_[p]; }
    const Token& get() { const Token& t = toks_[pos_]; if (t.kind != Tok::Eof) ++pos_; return t; }
    [[noreturn]] void error(const std::string& msg, const Token* tok = nullptr) const {
        const Token& t = tok ? *tok : peek();
        throw ParseError(msg + " [fonction " + fname_ + ", ligne " + std::to_string(t.line) + ", près de '" + t.text + "']");
    }
    struct Depth { Parser& p; explicit Depth(Parser& pp) : p(pp) { if (++p.depth_ > 1500) { --p.depth_; throw ParseError("Récursion trop profonde"); } } ~Depth() { --p.depth_; } };

    const Node* parseExpression(int minPrio) {
        Depth d(*this);
        const Node* left = parsePrefix();
        for (;;) {
            const Token& t = peek();
            if (t.kind != Tok::Op) break;
            int prio = priority(t.op);
            if (prio < 0 || prio < minPrio) break;
            Op op = t.op;
            if (op == Op::Pct) {
                Tok nk = peek(1).kind;
                if (nk == Tok::Force || nk == Tok::When || nk == Tok::Eof) break;   // "RaiseBy 60% Force"
            }
            if (op == Op::Cond) {
                get();
                const Node* a = parseExpression(0);
                const Token& c = get();
                if (!(c.kind == Tok::Op && c.op == Op::Colon)) error("Attendu ':' dans l'expression conditionnelle", &c);
                const Node* b = parseExpression(priority(Op::Cond));
                left = arena_.make<Ternary>(left, a, b);
                continue;
            }
            get();
            int nextMin = (op == Op::Pow) ? prio : prio + 1;
            const Node* right = parseExpression(nextMin);
            left = arena_.make<Binary>(op, left, right);
        }
        return left;
    }

    const Node* parsePrefix() {
        const Token& t = peek();
        if (t.kind == Tok::Open) {
            get();
            const Node* e = parseExpression(0);
            const Token& c = get();
            if (c.kind != Tok::Close) error("Parenthèse fermante attendue", &c);
            return arena_.make<Bracket>(e);
        }
        if (t.kind == Tok::Op && isUnary(t.op)) {
            Op op = t.op;
            get();
            const Node* e = parseExpression(op == Op::Ln ? 14 : 13);
            return arena_.make<Unary>(op, e);
        }
        if (t.kind == Tok::Num) {
            const Token& nt = get();
            bool ok;
            double v = stringToNumber(nt.text, ok);
            if (!ok) error("Nombre invalide", &nt);
            return arena_.make<Num>(v);
        }
        if (t.kind == Tok::Ident || t.kind == Tok::Cards) { const Token& it = get(); return arena_.make<Ident>(names().intern(it.text)); }
        error("Token inattendu dans une expression", &t);
    }

    bool isActionToken(const Token& t) const {
        if (t.kind == Tok::Return || t.kind == Tok::Set) return true;
        return t.kind == Tok::Ident && canonicalAction(t.text) != nullptr;
    }

    void expectForce() {
        if (peek().kind != Tok::Force) error("Mot-clé FORCE manquant après l'action");
        get();
        if (peek().kind == Tok::Force) error("FORCE en double");
        if (peek().kind == Tok::Ident && lower(peek().text) == "delay") { get(); if (peek().kind == Tok::Num) get(); }
    }

    const Node* parseAction() {
        const Token& t = get();
        if (t.kind == Tok::Return) { const Node* e = parseExpression(0); expectForce(); return e; }
        if (t.kind == Tok::Set) {
            const Token& v = get();
            if (v.kind != Tok::Ident) error("Variable utilisateur attendue après SET", &v);
            int id = names().intern(v.text);
            if (peek().kind == Tok::Ident && !peek().text.empty() && peek().text[0] == '"') get();   // prw1326_cmd "RECALC"
            return arena_.make<UserVarSet>(id);
        }
        std::string name = canonicalAction(t.text);
        std::string low = lower(name);
        auto pctOrBy = [&]() -> const Node* {
            const Node* e = parseExpression(0);
            if (peek().kind == Tok::Op && peek().op == Op::Pct) { get(); expectForce(); return arena_.make<BetsizeAction>(BetsizeAction::Pct, e); }
            expectForce();
            return arena_.make<BetsizeAction>(BetsizeAction::By, e);
        };
        if (low == "raiseto") { const Node* e = parseExpression(0); expectForce(); return arena_.make<BetsizeAction>(BetsizeAction::To, e); }
        if (low == "raiseby") return pctOrBy();
        if (low == "raise" && peek().kind != Tok::Force) return pctOrBy();   // « Raise N » (Shanky)
        expectForce();
        return arena_.make<Ident>(names().intern(name));
    }

    When* parseWhenSequence() {
        When* last = nullptr; When* first = nullptr; bool lastOpen = false;
        while (peek().kind == Tok::When) {
            get();
            Tok nt = peek().kind;
            if (nt == Tok::Eof || nt == Tok::When) error("Condition WHEN vide");
            const Node* cond = parseExpression(0);
            When* w = arena_.make<When>(cond);
            if (!first) first = w;
            if (last) { if (lastOpen) last->second = w; else last->third = w; }
            last = w;
            const Token& t = peek();
            if (isActionToken(t)) { w->second = parseAction(); lastOpen = false; }
            else if (t.kind == Tok::When) { lastOpen = true; continue; }
            else if (t.kind == Tok::Eof) break;
            else error("Action manquante après la condition WHEN", &t);
        }
        return first;
    }

    void backpatch(When* first) {
        When* lastOpen = nullptr;
        const Node* cur = first;
        const Node* lastWhen = cur;
        int guard = 0;
        while (cur && ++guard <= 100000) {
            lastWhen = cur;
            if (cur->kind != NK::When) break;
            When* w = const_cast<When*>(static_cast<const When*>(cur));
            if (w->isOpenEnded()) {
                if (lastOpen && lastOpen != w) lastOpen->third = w;
                lastOpen = w;
                cur = w->second;
            } else {
                const Node* right = w->third;
                if (!right || right->kind != NK::When) break;
                cur = right;
            }
        }
        const Node* end = arena_.make<EndOfFunction>(fallback_);
        if (lastWhen && lastWhen->kind == NK::When) const_cast<When*>(static_cast<const When*>(lastWhen))->third = end;
        if (lastOpen && lastOpen->isOpenEnded()) lastOpen->third = end;
    }

    std::vector<Token> toks_;
    size_t pos_ = 0;
    std::string fname_;
    int fallback_;
    Arena& arena_;
    int depth_ = 0;
};

}  // namespace

const Node* parseFunctionBody(const std::string& text, const std::string& fname, const std::string& fallbackName, Arena& arena) {
    Parser p(tokenize(text), fname, fallbackName.empty() ? -1 : names().intern(fallbackName), arena);
    return p.parseFunctionBody();
}

// ====================================================================== listes de mains (COHScriptList)
void HandList::clear() {
    for (int i = 0; i < 13; ++i) for (int j = 0; j < 13; ++j) { matrix_[i][j] = false; weight_[i][j] = 0; }
}

static int rankFromChar(char c) {
    switch (std::tolower(static_cast<unsigned char>(c))) {
        case '2': return 2; case '3': return 3; case '4': return 4; case '5': return 5; case '6': return 6;
        case '7': return 7; case '8': return 8; case '9': return 9; case 't': return 10; case 'j': return 11;
        case 'q': return 12; case 'k': return 13; case 'a': return 14; default: return -1;
    }
}

// OpenHoldem : Set(first, second, weight) -> matrice [second][first] ; suited = premier > second
void HandList::set(int first, int second, bool suited, int weight) {
    if (first < second) std::swap(first, second);
    int f = suited ? first : second, s = suited ? second : first;
    matrix_[s - 2][f - 2] = true;
    weight_[s - 2][f - 2] = static_cast<uint8_t>(weight);
}

bool HandList::isOnListNoRandom(int r1, int r2, bool suited) const {
    if (r1 < r2) std::swap(r1, r2);
    int f = suited ? r1 : r2, s = suited ? r2 : r1;
    return matrix_[s - 2][f - 2];
}
int HandList::weightOf(int r1, int r2, bool suited) const {
    if (r1 < r2) std::swap(r1, r2);
    int f = suited ? r1 : r2, s = suited ? r2 : r1;
    return weight_[s - 2][f - 2];
}
bool HandList::isOnList(int r1, int r2, bool suited, Rng& rng) const {
    if (r1 < 2 || r1 > 14 || r2 < 2 || r2 > 14) return false;
    if (r1 < r2) std::swap(r1, r2);
    int f = suited ? r1 : r2, s = suited ? r2 : r1;
    if (static_cast<int>(weight_[s - 2][f - 2]) >= static_cast<int>(rng.below(101))) return matrix_[s - 2][f - 2];
    return false;
}
int HandList::nHands() const { int n = 0; for (auto& r : matrix_) for (bool b : r) n += b; return n; }

bool HandList::setMember(const std::string& member0, std::string& error) {
    std::string member = member0;
    auto bad = [&]() { error = "Invalid list member " + member0; return false; };
    bool openEnded = member.find('+') != std::string::npos;
    bool group = member.find('-') != std::string::npos;
    if (openEnded && group) return bad();
    if (openEnded) member.erase(std::remove(member.begin(), member.end(), '+'), member.end());
    // OpenHoldem : en groupe « début-fin », `hand_start` = avant '-', `hand` = après '-' (sans le poids)
    std::string handStart, rest = member;
    if (group) { size_t d = member.find('-'); handStart = member.substr(0, d); rest = member.substr(d + 1); }
    std::string hand = rest;
    int weight = 100;
    size_t colon = rest.find(':');
    if (colon != std::string::npos) {
        hand = rest.substr(0, colon);
        bool ok; double w = stringToNumber(rest.substr(colon + 1), ok);
        if (!ok) return bad();
        weight = static_cast<int>(w);
    }
    size_t length = hand.size();
    if (length < 2 || length > 3 || weight < 0 || weight > 100) return bad();
    int first2 = 0, second2 = 0;
    if (group) {
        if (handStart.size() != length) return bad();
        first2 = rankFromChar(handStart[0]); second2 = rankFromChar(handStart[1]);
        if (first2 < 0 || second2 < 0) return bad();
    }
    char c0 = static_cast<char>(std::tolower(static_cast<unsigned char>(hand[0])));
    char c1 = static_cast<char>(std::tolower(static_cast<unsigned char>(hand[1])));
    int first = rankFromChar(c0), second = rankFromChar(c1);
    if (first < 0 || second < 0) return bad();
    if (length == 2) {
        if (!group && c0 == c1) {
            set(first, second, false, weight);
            if (openEnded) for (int i = first + 1; i <= 14; ++i) set(i, i, false, weight);
            return true;
        }
        char s0 = group ? static_cast<char>(std::tolower(static_cast<unsigned char>(handStart[0]))) : 0;
        char s1 = group ? static_cast<char>(std::tolower(static_cast<unsigned char>(handStart[1]))) : 0;
        if (group && c0 == c1 && s0 == s1) {
            if (first > first2) std::swap(first, first2);
            for (int i = first; i <= first2; ++i) set(i, i, false, weight);
            return true;
        }
        error = "Old style list format detected: " + member0 + " (use AKs for suited, AKo for offsuited, AKs AKo for both)";
        return false;
    }
    if (group) {
        if (first < second) std::swap(first, second);
        if (first2 < second2) std::swap(first2, second2);
        if (first2 < first) { std::swap(first2, first); std::swap(second2, second); }
        if (second2 < second) return bad();
        if (first2 > first && !((first2 - second2 == first - second) && (first2 - second2 <= 3))) return bad();
    } else if (first < second) std::swap(first, second);
    char suit = static_cast<char>(std::tolower(static_cast<unsigned char>(hand[2])));
    if (group && suit != static_cast<char>(std::tolower(static_cast<unsigned char>(handStart[2])))) return bad();
    if (suit != 's' && suit != 'o') return bad();
    bool suited = suit == 's';
    set(first, second, suited, weight);
    if (openEnded) for (int i = second + 1; i < first; ++i) set(first, i, suited, weight);
    if (group) {
        if (first2 == first) for (int i = second + 1; i <= second2; ++i) set(first, i, suited, weight);
        if (first2 > first) { int j = second + 1; for (int i = first + 1; i <= first2; ++i) set(i, j++, suited, weight); }
    }
    return true;
}

bool HandList::parse(const std::string& body, std::string& error) {
    clear();
    // commentaires // et /* */, séparateurs espaces et virgules
    std::string clean;
    for (size_t i = 0; i < body.size();) {
        if (body[i] == '/' && i + 1 < body.size() && body[i + 1] == '/') { while (i < body.size() && body[i] != '\n') ++i; clean += ' '; continue; }
        if (body[i] == '/' && i + 1 < body.size() && body[i + 1] == '*') { size_t j = body.find("*/", i + 2); i = (j == std::string::npos) ? body.size() : j + 2; clean += ' '; continue; }
        clean += (body[i] == ',') ? ' ' : body[i];
        ++i;
    }
    std::istringstream is(clean);
    std::vector<std::string> raw, toks;
    for (std::string t; is >> t;) raw.push_back(t);
    // « QQ - 99 », « AA : 50 » : on recolle les opérateurs isolés au token voisin
    for (const std::string& t : raw) {
        bool glue = !toks.empty() && (t[0] == '-' || t[0] == ':' || toks.back().back() == '-' || toks.back().back() == ':');
        if (glue) toks.back() += t; else toks.push_back(t);
    }
    for (const std::string& tok : toks) if (!setMember(tok, error)) return false;
    return true;
}

// ====================================================================== collection
std::string defaultFunctionFor(const std::string& name) {
    if (name == "f$preflop") return "DefaultPreflop";
    if (name == "f$flop") return "DefaultFlop";
    if (name == "f$turn") return "DefaultTurn";
    if (name == "f$river") return "DefaultRiver";
    return "";
}

std::string readFileLatin1(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Impossible d'ouvrir " + path);
    std::stringstream ss; ss << f.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) s.erase(0, 3);
    return s;
}

static std::vector<std::string> splitLines(const std::string& t) {
    std::vector<std::string> out; std::string cur;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '\n') { out.push_back(cur); cur.clear(); }
        else if (t[i] == '\r') { out.push_back(cur); cur.clear(); if (i + 1 < t.size() && t[i + 1] == '\n') ++i; }
        else cur += t[i];
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<OHFunction> splitFormulaText(const std::string& text, const std::string& path, bool readOnly) {
    std::vector<OHFunction> out;
    bool have = false;
    OHFunction cur;
    std::string body;
    auto flush = [&]() {
        if (!have) return;
        cur.text = body; cur.path = path; cur.readOnly = readOnly; cur.isList = cur.name.rfind("list", 0) == 0;
        out.push_back(cur); body.clear();
    };
    int idx = 0;
    for (const std::string& raw : splitLines(text)) {
        ++idx;
        if (raw.rfind("##", 0) == 0) {
            size_t e = raw.find("##", 2);
            if (e != std::string::npos) {
                flush();
                have = true; cur = OHFunction();
                std::string nm = raw.substr(2, e - 2);
                while (!nm.empty() && isSpaceCh(static_cast<unsigned char>(nm.front()))) nm.erase(nm.begin());
                while (!nm.empty() && isSpaceCh(static_cast<unsigned char>(nm.back()))) nm.pop_back();
                cur.name = nm; cur.line = idx;
                std::string rest = raw.substr(e + 2);
                if (rest.find_first_not_of(" \t\r\n") != std::string::npos) body += rest + "\n";
                continue;
            }
        }
        if (have) body += raw + "\n";
    }
    flush();
    // le texte est joint par "\n" sans retour final, comme le Python
    for (auto& f : out) if (!f.text.empty() && f.text.back() == '\n') f.text.pop_back();
    return out;
}

void FormulaCollection::addText(const std::string& text, const std::string& path, bool readOnly) {
    for (auto& f : splitFormulaText(text, path, readOnly)) functions[f.name] = std::make_shared<OHFunction>(f);
}

void FormulaCollection::addFile(const std::string& path, bool readOnly) {
    std::string raw = readFileLatin1(path);
    if (isShanky(raw)) addText(shankyToOhf(raw), path, readOnly);
    else addText(raw, path, readOnly);
}

std::shared_ptr<OHFunction> FormulaCollection::get(const std::string& name) const {
    auto it = functions.find(name);
    return it == functions.end() ? nullptr : it->second;
}

void FormulaCollection::buildLowerIndex() {
    lowerIndex.clear();
    std::vector<std::string> keys;
    for (auto& kv : functions) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    for (auto& k : keys) lowerIndex.emplace(lower(k), k);
}

void FormulaCollection::parseAll() { for (auto& kv : functions) parseOne(*kv.second); }

void FormulaCollection::parseOne(OHFunction& f) {
    if (f.parsed) return;
    f.parsed = true;
    std::string low = lower(f.name);
    if ((f.name.size() >= 2 && isDigitCh(static_cast<unsigned char>(f.name[0])) && isDigitCh(static_cast<unsigned char>(f.name[1]))) || low == "notes" || low == "dll") return;
    try {
        if (f.isList) {
            f.hands = std::make_shared<HandList>();
            std::string err;
            if (!f.hands->parse(f.text, err)) { f.error = err; f.hands.reset(); }
        } else {
            f.arena = std::make_shared<Arena>();
            f.tree = parseFunctionBody(f.text, f.name, defaultFunctionFor(f.name), *f.arena);
        }
    } catch (const ParseError& e) {
        f.error = e.what(); f.tree = nullptr;
    }
}

bool FormulaCollection::isOpenPPLProfile() const {
    for (const char* n : {"f$preflop", "f$flop", "f$turn", "f$river"}) {
        auto it = functions.find(n);
        if (it != functions.end() && !it->second->readOnly) {
            const std::string& t = it->second->text;
            if (t.find_first_not_of(" \t\r\n") != std::string::npos) return true;
        }
    }
    return false;
}

std::vector<std::pair<std::string, std::string>> FormulaCollection::errors(bool includeReadOnly) const {
    std::vector<std::pair<std::string, std::string>> out;
    for (auto& kv : functions) if (!kv.second->error.empty() && (includeReadOnly || !kv.second->readOnly)) out.push_back({kv.first, kv.second->error});
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace ps::oh
