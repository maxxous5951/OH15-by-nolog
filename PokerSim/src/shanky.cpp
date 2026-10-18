// Convertisseur PPL « Shanky » (.txt / .oPPL ancien format) -> texte .ohf.
// Port de SpinGoSimulator/shanky.py (règles de ShankyToOpenPPL), plus la conversion des blocs
// « NEW LIST nnn ... END LIST » en ##listnnn## que ni le Python ni le convertisseur C# officiel ne gèrent.
#include <algorithm>
#include <cctype>
#include <functional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include "pokersim/ohlang.hpp"

namespace ps::oh {
namespace {

using Re = std::regex;
const auto kIc = std::regex::ECMAScript | std::regex::icase;

std::string low(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string up(std::string s) { for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n\f\v"), b = s.find_last_not_of(" \t\r\n\f\v");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

std::string replaceWith(const std::string& s, const Re& re, const std::function<std::string(const std::smatch&)>& fn) {
    std::string out; auto begin = s.cbegin(); std::smatch m;
    while (std::regex_search(begin, s.cend(), m, re, begin == s.cbegin() ? std::regex_constants::match_default : std::regex_constants::match_prev_avail)) {
        out.append(begin, m[0].first);
        out += fn(m);
        if (m[0].length() == 0) { if (m[0].second == s.cend()) { begin = s.cend(); break; } out += *m[0].second; begin = m[0].second + 1; }
        else begin = m[0].second;
    }
    out.append(begin, s.cend());
    return out;
}

const std::unordered_map<std::string, std::string>& renames() {
    static const std::unordered_map<std::string, std::string> r = {
        {"calledonflop", "BotCalledOnFlop"}, {"calledonturn", "BotCalledOnTurn"}, {"fourof1suitonturn", "FourOfOneSuitOnTurn"},
        {"have2ndbestkicker", "HaveSecondBestKicker"}, {"have2ndbestkickerorbetter", "HaveSecondBestKickerOrBetter"},
        {"have2ndbestoverpairorbetter", "HaveSecondBestOverPairOrBetter"}, {"have2ndnutflush", "HaveSecondNutFlush"},
        {"have2ndnutflushdraw", "HaveSecondNutFlushDraw"}, {"have2ndnutlow", "HaveSecondNutLow"}, {"have2ndnutlowdraw", "HaveSecondNutLowDraw"},
        {"have2ndnutstraight", "HaveSecondNutStraight"}, {"have2ndoverpair", "HaveSecondOverPair"}, {"have2ndtoppair", "HaveSecondTopPair"},
        {"have2ndtopset", "HaveSecondTopSet"}, {"have3rdbestkicker", "HaveThirdBestKicker"}, {"have3rdbestkickerorbetter", "HaveThirdBestKickerOrBetter"},
        {"have3rdbestoverpairorbetter", "HaveThirdBestOverPairOrBetter"}, {"have3rdnutflush", "HaveThirdNutFlush"},
        {"have3rdnutflushdraw", "HaveThirdNutFlushDraw"}, {"have3rdnutlow", "HaveThirdNutLow"}, {"have3rdnutlowdraw", "HaveThirdNutLowDraw"},
        {"have3rdoverpair", "HaveThirdOverPair"}, {"have3rdtoppair", "HaveThirdTopPair"}, {"have3rdtopset", "HaveThirdTopSet"},
        {"have4thnutflush", "HaveFourthNutFlush"}, {"have4thnutflushdraw", "HaveFourthNutFlushDraw"}, {"have4thnutlow", "HaveFourthNutLow"},
        {"have4thnutlowdraw", "HaveFourthNutLowDraw"}, {"have4thoverpair", "HaveFourthOverPair"}, {"have4thtoppair", "HaveFourthTopPair"},
        {"have4thtopset", "HaveFourthTopSet"}, {"have5thnutflush", "HaveFifthNutFlush"}, {"have5thnutflushdraw", "HaveFifthNutFlushDraw"},
        {"have5thoverpair", "HaveFifthOverPair"}, {"havebackdoor2ndnutflushdraw", "HaveBackdoorSecondNutFlushDraw"},
        {"havebackdoor3rdnutflushdraw", "HaveBackdoorThirdNutFlushDraw"}, {"maxcurrentopponentstacksize", "MaxOpponentsLeftStackSize"},
        {"morethanonestraightpossibleonturn", "MoreThanOneStraightPossibleOnRiver"},
        {"nutfullhouseorfourofakind", "NutFullHouseOrFourOfAKind_ShankyCompatible"}, {"sitout", "Fold"}};
    return r;
}
const Re& renameRe() {
    static const Re re = [] {
        std::string alt;
        for (auto& kv : renames()) { if (!alt.empty()) alt += "|"; alt += kv.first; }
        return Re("\\b(" + alt + ")\\b", kIc);
    }();
    return re;
}

const std::unordered_map<std::string, std::string>& constCase() {
    static const std::unordered_map<std::string, std::string> m = {{"call", "Call"}, {"fold", "Fold"}, {"bet", "Bet"}, {"raise", "Raise"}, {"check", "Check"},
        {"play", "Play"}, {"beep", "Beep"}, {"raisemax", "RaiseMax"}, {"raisepot", "RaisePot"}, {"force", "FORCE"}};
    return m;
}

bool startsWithAny(const std::string& s) {
    for (const char* w : {"call", "fold", "bet", "raise", "check", "play", "beep", "sitout"}) if (s.rfind(w, 0) == 0) return true;
    return false;
}

std::string convertLine(const std::string& line) {
    std::string s = trim(line);
    static const Re delayRe("\\bdelay\\s+\\S+\\s*$", kIc);
    s = trim(std::regex_replace(s, delayRe, ""));
    s = replaceWith(s, renameRe(), [](const std::smatch& m) { return renames().at(low(m[1].str())); });
    for (auto ab : {std::pair<const char*, const char*>{"2nd", "second"}, {"3rd", "third"}, {"4th", "fourth"}, {"5th", "fifth"}}) {
        static const Re dummy("x");
        s = std::regex_replace(s, Re(ab.first, kIc), ab.second);
    }
    static const Re inRe("\\bin\\s+(?=(?:s|b|cutoff|middle|early|late))", kIc);
    s = std::regex_replace(s, inRe, "In");
    static const Re handRe("\\b(hand|board)\\s*=\\s*([2-9TJQKA]{1,2})(?:(s)(?![A-Za-z0-9$])|(?![A-Za-z0-9$])(?:\\s+(suited))?)", kIc);
    s = replaceWith(s, handRe, [](const std::smatch& m) {
        return low(m[1].str()) + "$" + up(m[2].str()) + ((m[3].matched || m[4].matched) ? "Suited" : "");
    });
    static const Re eqRe("\\b(BetSize|StackSize|AmountToCall)\\s*=(?![=~])", kIc);
    s = std::regex_replace(s, eqRe, "$1 ~~");
    static const Re constRe("\\b(call|fold|bet|raise|check|play|beep|raisemax|raisepot|force)\\b(?!\\$)", kIc);
    s = replaceWith(s, constRe, [](const std::smatch& m) { return constCase().at(low(m[0].str())); });
    // Variable utilisateur Shanky : « When <cond> User_X » (sans action) affecte la variable -> « Set User_X ».
    // Un « User_X » qui suit un opérateur, une parenthèse ou WHEN est une condition : on n'y touche pas.
    {
        size_t e = s.find_last_not_of(" \t");
        size_t b = (e == std::string::npos) ? std::string::npos : s.find_last_of(" \t", e);
        if (b != std::string::npos && e != std::string::npos) {
            std::string tail = s.substr(b + 1, e - b), lt = low(tail);
            std::string head = trim(s.substr(0, b));
            size_t pb = head.find_last_of(" \t");
            std::string prev = low(pb == std::string::npos ? head : head.substr(pb + 1));
            static const char* kOps[] = {"<", ">", "=", "<=", ">=", "!=", "and", "or", "not", "xor", "when", "%", "set", "force", "(", "~~", "==", "+", "-", "*", "/"};
            bool afterOp = prev.empty() || prev.back() == '(' ;
            for (const char* o : kOps) if (prev == o) afterOp = true;
            if (lt.rfind("user", 0) == 0 && !afterOp) s = head + " Set " + tail;
        }
    }
    // FORCE manquant en fin de ligne
    {
        std::vector<std::string> w; std::istringstream is(low(s)); for (std::string t; is >> t;) w.push_back(t);
        if (!w.empty() && w.back() != "force") {
            const std::string& tail = w.back();
            std::string prev = w.size() > 1 ? w[w.size() - 2] : "";
            static const Re numRe("^\\d+(\\.\\d+)?%?$");
            bool opPrev = prev == "<" || prev == ">" || prev == "=" || prev == "<=" || prev == ">=" || prev == "and" || prev == "or" || prev == "not" || prev == "!=" || prev == "%";
            if (startsWithAny(tail) && !opPrev && tail != "when") s += " FORCE";
            else if (prev.rfind("raise", 0) == 0 && std::regex_match(tail, numRe)) s += " FORCE";
        }
    }
    static const Re raiseByRe("\\b(?:Raise|Bet)\\s+(\\d+(?:\\.\\d+)?\\s*%?)\\s+FORCE\\s*$", kIc);
    s = replaceWith(s, raiseByRe, [](const std::smatch& m) { std::string g = m[1].str(); g.erase(std::remove(g.begin(), g.end(), ' '), g.end()); return "RaiseBy " + g + " FORCE"; });
    static const Re betForceRe("\\bBet\\s+FORCE\\s*$", kIc);
    s = std::regex_replace(s, betForceRe, "Raise FORCE");
    return s;
}

const std::string kRanks = "23456789TJQKA";

// Liste Shanky (ancien format) : « AK » = AKs + AKo, « KA » = « AK » ; les autres membres sont conservés tels quels.
std::string convertListMember(const std::string& tok) {
    std::string t = trim(tok);
    if (t.size() == 2) {
        size_t a = kRanks.find(static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])))), b = kRanks.find(static_cast<char>(std::toupper(static_cast<unsigned char>(t[1]))));
        if (a != std::string::npos && b != std::string::npos && a != b) {
            if (a < b) std::swap(a, b);
            std::string h{kRanks[a], kRanks[b]};
            return h + "s " + h + "o";
        }
    }
    return t;
}

}  // namespace

bool isShanky(const std::string& text) {
    static const Re sec("^\\s*(preflop|flop|turn|river)\\s*$", kIc);
    bool has = false;
    std::istringstream is(text);
    for (std::string raw; std::getline(is, raw);) {
        std::string s = trim(raw);
        if (s.rfind("##", 0) == 0) return false;
        if (std::regex_match(s, sec)) has = true;
    }
    return has;
}

std::string shankyToOhf(const std::string& text) {
    static const Re secRe("^\\s*(preflop|flop|turn|river)\\s*$", kIc);
    static const Re newListRe("^new\\s+list\\s+(\\S+)$", kIc);
    static const Re endListRe("^end\\s+list$", kIc);
    std::vector<std::string> out;
    std::string cur;
    bool inSections = false, inList = false;
    auto flush = [&]() { if (!cur.empty()) { out.push_back("\t" + convertLine(cur)); cur.clear(); } };
    std::istringstream is(text);
    for (std::string raw; std::getline(is, raw);) {
        std::string s = trim(raw);
        std::replace(s.begin(), s.end(), '\t', ' ');
        if (s.empty()) continue;
        std::smatch m;
        // blocs de listes
        if (!inList && std::regex_match(s, m, newListRe)) { flush(); inList = true; out.push_back("\n##list" + low(m[1].str()) + "##"); continue; }
        if (inList) {
            if (std::regex_match(s, endListRe)) { inList = false; continue; }
            size_t c = s.find("//");
            if (c != std::string::npos) s = trim(s.substr(0, c));
            if (s.empty()) continue;
            std::string line;
            std::istringstream ls(s);
            for (std::string t; ls >> t;) {
                std::replace(t.begin(), t.end(), ',', ' ');
                std::istringstream ts(t);
                for (std::string u; ts >> u;) line += (line.empty() ? "" : " ") + convertListMember(u);
            }
            out.push_back(line);
            continue;
        }
        if (s.rfind("//", 0) == 0) { flush(); if (inSections) out.push_back(s); continue; }
        size_t c = s.find("//");
        if (c != std::string::npos) { s = trim(s.substr(0, c)); if (s.empty()) continue; }
        if (std::regex_match(s, m, secRe)) { flush(); inSections = true; out.push_back("\n##f$" + low(m[1].str()) + "##"); continue; }
        if (!inSections) continue;     // options du bot, mot-clé « custom », ...
        std::istringstream ws(s); std::string first; ws >> first;
        if (low(first) == "when") { flush(); cur = s; }
        else cur = cur.empty() ? s : cur + " " + s;
    }
    flush();
    std::string res;
    for (size_t i = 0; i < out.size(); ++i) { if (i) res += "\n"; res += out[i]; }
    return res + "\n";
}

}  // namespace ps::oh
