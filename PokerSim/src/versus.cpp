#include "pokersim/versus.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include "pokersim/pokerval.hpp"

namespace ps::oh {
namespace {

struct Perms {
    uint8_t p[24][4];
    Perms() { std::array<int, 4> a{0, 1, 2, 3}; int k = 0; do { for (int i = 0; i < 4; ++i) p[k][i] = static_cast<uint8_t>(a[static_cast<size_t>(i)]); ++k; } while (std::next_permutation(a.begin(), a.end())); }
};
const Perms& perms() { static const Perms P; return P; }

}  // namespace

bool versusPreflopLookup(int h0, int h1, int o0, int o1, uint32_t& win, uint32_t& loss) {
    const int c[4] = {h0, h1, o0, o1};
    for (int i = 0; i < 4; ++i) { if (c[i] < 0 || c[i] > 51) return false; for (int j = i + 1; j < 4; ++j) if (c[i] == c[j]) return false; }
    int r[4], s[4];
    for (int i = 0; i < 4; ++i) { r[i] = c[i] % 13; s[i] = c[i] / 13; }
    uint32_t best = 0xFFFFFFFFu;
    bool bestSwap = false;
    const Perms& P = perms();
    for (int k = 0; k < 24; ++k) {
        uint32_t code[4];
        for (int i = 0; i < 4; ++i) code[i] = static_cast<uint32_t>(r[i] * 4 + P.p[k][s[i]]);
        const uint32_t hLo = std::min(code[0], code[1]), hHi = std::max(code[0], code[1]), oLo = std::min(code[2], code[3]), oHi = std::max(code[2], code[3]);
        const uint32_t keyA = (hLo << 18) | (hHi << 12) | (oLo << 6) | oHi;
        const uint32_t keyB = (oLo << 18) | (oHi << 12) | (hLo << 6) | hHi;
        if (keyA < best) { best = keyA; bestSwap = false; }
        if (keyB < best) { best = keyB; bestSwap = true; }
    }
    unsigned lo = 0, hi = kVersusEntries;
    while (lo < hi) {
        const unsigned mid = (lo + hi) / 2;
        if (kVersusData[mid * 3] < best) lo = mid + 1; else hi = mid;
    }
    if (lo >= kVersusEntries || kVersusData[lo * 3] != best) return false;
    const uint32_t w = kVersusData[lo * 3 + 1], l = kVersusData[lo * 3 + 2];
    win = bestSwap ? l : w;
    loss = bestSwap ? w : l;
    return true;
}

void computeVersus(int h0, int h1, uint64_t board, int nBoard, int nOppPlaying, VersusResult& R) {
    R = VersusResult{};
    if (h0 < 0 || h1 < 0 || h0 > 51 || h1 > 51 || h0 == h1) return;
    R.valid = true;
    if (h0 > h1) std::swap(h0, h1);
    const uint64_t hero = peBit(h0) | peBit(h1);
    const uint64_t used = hero | board;
    auto add = [](VersusGroup& g, uint32_t w, uint32_t t, uint32_t l) { ++g.nhands; g.win += w; g.tie += t; g.los += l; };
    if (nBoard == 0) {
        for (int i = 0; i < 51; ++i) for (int j = i + 1; j < 52; ++j) {
            if (i == h0 || i == h1 || j == h0 || j == h1) continue;
            uint32_t w = 0, l = 0;
            versusPreflopLookup(h0, h1, i, j, w, l);
            const uint32_t t = kVersusBoards - w - l;
            R.win[i][j] = w; R.tie[i][j] = t; R.loss[i][j] = l;
            add(R.all, w, t, l);
            add(w < l ? R.hi : w > l ? R.lo : R.ti, w, t, l);
        }
    } else {
        // une évaluation de MA main par runout (elle ne dépend pas de l'adversaire), puis toutes les mains adverses restantes
        const uint32_t heroNow = handValN(hero | board, nBoard + 2);
        const uint64_t known = used;
        int pool[52], np = 0;
        for (int c = 0; c < 52; ++c) if (!(known >> c & 1)) pool[np++] = c;
        auto runout = [&](uint64_t fullBoard) {
            const uint32_t a = handValN(hero | fullBoard, 7);
            int rest[52], n = 0;
            for (int c = 0; c < 52; ++c) if (!((fullBoard | hero) >> c & 1)) rest[n++] = c;
            for (int x = 0; x < n - 1; ++x) for (int y = x + 1; y < n; ++y) {
                const int i = rest[x], j = rest[y];
                const uint32_t b = handValN(peBit(i) | peBit(j) | fullBoard, 7);
                if (a > b) ++R.win[i][j]; else if (a < b) ++R.loss[i][j]; else ++R.tie[i][j];
            }
        };
        if (nBoard == 3) { for (int x = 0; x < np - 1; ++x) for (int y = x + 1; y < np; ++y) runout(board | peBit(pool[x]) | peBit(pool[y])); }
        else if (nBoard == 4) { for (int x = 0; x < np; ++x) runout(board | peBit(pool[x])); }
        else runout(board);
        for (int i = 0; i < 51; ++i) for (int j = i + 1; j < 52; ++j) {
            if ((known >> i & 1) || (known >> j & 1)) continue;
            const uint32_t w = R.win[i][j], t = R.tie[i][j], l = R.loss[i][j];
            add(R.all, w, t, l);
            add(w < l ? R.hi : w > l ? R.lo : R.ti, w, t, l);
            const uint32_t oppNow = handValN(peBit(i) | peBit(j) | board, nBoard + 2);
            add(heroNow < oppNow ? R.hiNow : heroNow > oppNow ? R.loNow : R.tiNow, w, t, l);
        }
    }
    const double total = R.all.win + R.all.tie + R.all.los;
    if (total > 0) {
        R.prwin = std::pow(R.all.win / total, nOppPlaying);      // OpenHoldem n'élève à la puissance que prwin (pas prtie ni prlos)
        R.prtie = R.all.tie / total;
        R.prlos = R.all.los / total;
    }
}

}  // namespace ps::oh
