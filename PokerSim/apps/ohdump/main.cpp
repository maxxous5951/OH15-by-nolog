// ohdump : sérialise l'arbre syntaxique de chaque fonction d'un ou plusieurs fichiers de formules
// (outil de test différentiel contre SpinGoSimulator/ohlang.py et de diagnostic des profils).
#include <algorithm>
#include <cstdio>
#include <map>
#include "pokersim/ohlang.hpp"

using namespace ps::oh;

int main(int argc, char** argv) {
    bool summary = false, convert = false;
    int rc = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--summary") { summary = true; continue; }
        if (a == "--convert") { convert = true; continue; }
        if (convert) { std::string raw = readFileLatin1(a); std::fputs((isShanky(raw) ? shankyToOhf(raw) : raw).c_str(), stdout); continue; }
        try {
            FormulaCollection fc;
            fc.addFile(a, false);
            fc.parseAll();
            std::map<std::string, std::shared_ptr<OHFunction>> sorted(fc.functions.begin(), fc.functions.end());
            if (summary) {
                int lists = 0, funcs = 0, errs = 0;
                for (auto& kv : sorted) { lists += kv.second->isList; funcs += !kv.second->isList; errs += !kv.second->error.empty(); }
                std::printf("%s: %d fonctions, %d listes, %d erreurs\n", a.c_str(), funcs, lists, errs);
                for (auto& kv : sorted) if (!kv.second->error.empty()) std::printf("   %s : %s\n", kv.first.c_str(), kv.second->error.c_str());
                continue;
            }
            std::printf("FILE %s\n", a.c_str());
            for (auto& kv : sorted) {
                const OHFunction& f = *kv.second;
                if (!f.error.empty()) std::printf("%s\tERROR\n", kv.first.c_str());
                else if (f.isList) std::printf("%s\tLIST %d\n", kv.first.c_str(), f.hands ? f.hands->nHands() : -1);
                else if (f.tree) std::printf("%s\t%s\n", kv.first.c_str(), dumpTree(f.tree).c_str());
                else std::printf("%s\tSKIP\n", kv.first.c_str());
            }
        } catch (const std::exception& e) { std::printf("FILE %s\nFATAL\t%s\n", a.c_str(), e.what()); rc = 1; }
    }
    return rc;
}
