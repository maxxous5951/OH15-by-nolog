#include "pokersim/handrank.hpp"
#include <cstring>

namespace ps::oh {
namespace {
const char kCardChars[] = "23456789TJQKA";
struct PairTables {
    int suited[170] = {0}, offsuited[170] = {0};
    PairTables() {
        for (int i = 0; i < 169; ++i) {
            const char* p = kPrwinHandrank169[i];
            int j = static_cast<int>(std::strchr(kCardChars, p[0]) - kCardChars) * 13 + static_cast<int>(std::strchr(kCardChars, p[1]) - kCardChars);
            (p[2] == 's' ? suited : offsuited)[j] = i + 1;
        }
    }
};
const PairTables& tables() { static const PairTables t; return t; }
}  // namespace

int prwinPairRank(bool suited, int r1, int r2) {
    if (r2 > r1) { int t = r1; r1 = r2; r2 = t; }
    return (suited ? tables().suited : tables().offsuited)[r1 * 13 + r2];
}

HandrankValues computeHandrank(int c0, int c1, int nOpp) {
    HandrankValues v;
    int r0 = c0 % 13, r1 = c1 % 13;
    int hi = c0, lo = c1;
    if (r0 < r1) { hi = c1; lo = c0; }
    char s[4];
    s[0] = kCardChars[hi % 13];
    s[1] = kCardChars[lo % 13];
    s[2] = (s[0] == s[1] || hi / 13 != lo / 13) ? 'o' : 's';
    s[3] = 0;
    if (nOpp >= 1 && nOpp <= 10) {          // au-delà, l'original lit hors tableau : on laisse 0
        for (int i = 0; i < 169; ++i) {
            if (std::strcmp(s, kHandrank169[nOpp - 1][i]) == 0) {
                v.r169 = i + 1;
                v.r2652 = kHandrank2652[nOpp - 1][i];
                break;
            }
        }
    }
    v.r1326 = v.r2652 / 2;
    v.r1000 = 1000 * v.r2652 / 2652;
    v.rp = 2652.0 / (1.0 + nOpp);
    return v;
}

}  // namespace ps::oh
