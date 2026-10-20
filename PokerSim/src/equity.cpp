#include "pokersim/equity.hpp"
#include "pokersim/evaluator.hpp"

namespace ps {

Equity equityVsRandom(const Card hole[2], const Card* board, int nBoard, int nOpp, int iterations, Rng& rng) {
    Equity eq;
    if (nOpp < 1) { eq.win = 1; eq.samples = 1; return eq; }
    Deck base;
    base.remove(hole[0]); base.remove(hole[1]);
    for (int i = 0; i < nBoard; ++i) base.remove(board[i]);
    long long win = 0, tie = 0, lose = 0;
    Card seven[7];
    for (int it = 0; it < iterations; ++it) {
        Deck d = base;
        Card full[5];
        for (int i = 0; i < nBoard; ++i) full[i] = board[i];
        for (int i = nBoard; i < 5; ++i) full[i] = d.draw(rng);
        seven[0] = hole[0]; seven[1] = hole[1];
        for (int i = 0; i < 5; ++i) seven[2 + i] = full[i];
        uint32_t mine = evaluate(seven, 7);
        bool beaten = false, tied = false;
        for (int o = 0; o < nOpp && !beaten; ++o) {
            seven[0] = d.draw(rng); seven[1] = d.draw(rng);
            uint32_t theirs = evaluate(seven, 7);
            if (theirs > mine) beaten = true;
            else if (theirs == mine) tied = true;
        }
        if (beaten) ++lose; else if (tied) ++tie; else ++win;
    }
    eq.samples = iterations;
    double n = static_cast<double>(iterations);
    eq.win = win / n; eq.tie = tie / n; eq.lose = lose / n;
    return eq;
}

Equity equityExactHeadsUp(const Card hole[2], const Card* board, int nBoard) {
    Equity eq;
    Deck base;
    base.remove(hole[0]); base.remove(hole[1]);
    for (int i = 0; i < nBoard; ++i) base.remove(board[i]);
    const int rem = base.size();
    const int missing = 5 - nBoard;
    if (missing > 2) return eq;   // trop coûteux : l'appelant utilisera Monte-Carlo
    long long win = 0, tie = 0, lose = 0;
    Card seven[7], opp[7];
    for (int i = 0; i < nBoard; ++i) seven[2 + i] = opp[2 + i] = board[i];
    seven[0] = hole[0]; seven[1] = hole[1];
    const Card* d = base.data();
    auto settle = [&](int a, int b) {
        opp[0] = d[a]; opp[1] = d[b];
    };
    auto score = [&]() {
        uint32_t m = evaluate(seven, 7), t = evaluate(opp, 7);
        if (m > t) ++win; else if (m == t) ++tie; else ++lose;
    };
    for (int a = 0; a < rem; ++a) for (int b = a + 1; b < rem; ++b) {
        settle(a, b);
        if (missing == 0) { score(); continue; }
        if (missing == 1) {
            for (int c = 0; c < rem; ++c) if (c != a && c != b) {
                seven[2 + nBoard] = opp[2 + nBoard] = d[c];
                score();
            }
        } else {
            for (int c = 0; c < rem; ++c) if (c != a && c != b)
                for (int e = c + 1; e < rem; ++e) if (e != a && e != b) {
                    seven[2 + nBoard] = opp[2 + nBoard] = d[c];
                    seven[3 + nBoard] = opp[3 + nBoard] = d[e];
                    score();
                }
        }
    }
    double n = static_cast<double>(win + tie + lose);
    eq.win = win / n; eq.tie = tie / n; eq.lose = lose / n;
    eq.samples = win + tie + lose; eq.exact = true;
    return eq;
}

}  // namespace ps
