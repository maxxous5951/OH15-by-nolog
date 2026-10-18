// Évaluateur de mains 5 à 7 cartes. Le score est un entier comparable :
//   bits 20-23 : catégorie, bits 0-19 : rangs significatifs (nibbles, du plus important au moins important).
#pragma once
#include <bit>
#include <cstdint>
#include "pokersim/cards.hpp"

namespace ps {

enum HandCategory : int {
    HC_HighCard = 0, HC_Pair, HC_TwoPair, HC_Trips, HC_Straight, HC_Flush, HC_FullHouse, HC_Quads, HC_StraightFlush
};

inline const char* categoryName(int c) {
    static const char* n[] = {"high card", "a pair", "two pair", "three of a kind", "a straight",
                              "a flush", "a full house", "four of a kind", "a straight flush"};
    return n[c];
}

namespace detail {
inline int hiBit(uint32_t m) { return std::bit_width(m) - 1; }
// rang le plus haut d'une quinte (0 = roue A-5 -> rang 3), -1 si aucune
inline int straightTop(uint32_t mask) {
    uint32_t m = (mask << 1) | ((mask >> 12) & 1u);   // bit 0 = As bas
    uint32_t t = m & (m >> 1) & (m >> 2) & (m >> 3) & (m >> 4);
    return t ? hiBit(t) + 3 : -1;
}
// extrait les n plus hauts rangs d'un masque et les empile en nibbles à partir du bit `shift`
inline uint32_t topN(uint32_t mask, int n, int shift) {
    uint32_t out = 0;
    for (int i = 0; i < n && mask; ++i) {
        int r = hiBit(mask);
        mask &= ~(1u << r);
        out |= static_cast<uint32_t>(r) << shift;
        shift -= 4;
    }
    return out;
}
}  // namespace detail

inline uint32_t evaluate(const Card* c, int n) {
    using namespace detail;
    uint32_t suit[4] = {0, 0, 0, 0}, all = 0;
    uint8_t cnt[13] = {};
    for (int i = 0; i < n; ++i) {
        int r = rankOf(c[i]), s = suitOf(c[i]);
        suit[s] |= 1u << r; all |= 1u << r; ++cnt[r];
    }
    uint32_t flushMask = 0;
    for (int s = 0; s < 4; ++s) {
        if (std::popcount(suit[s]) >= 5) {
            int sf = straightTop(suit[s]);
            if (sf >= 0) return (uint32_t{HC_StraightFlush} << 20) | (uint32_t(sf) << 16);
            flushMask = suit[s];
        }
    }
    uint32_t quads = 0, trips = 0, pairs = 0;
    for (int r = 0; r < 13; ++r) {
        if (cnt[r] == 4) quads |= 1u << r;
        else if (cnt[r] == 3) trips |= 1u << r;
        else if (cnt[r] == 2) pairs |= 1u << r;
    }
    if (quads) {
        int q = hiBit(quads);
        uint32_t rest = all & ~(1u << q);
        return (uint32_t{HC_Quads} << 20) | (uint32_t(q) << 16) | topN(rest, 1, 12);
    }
    if (trips && (std::popcount(trips) >= 2 || pairs)) {
        int t = hiBit(trips);
        uint32_t pp = (trips & ~(1u << t)) | pairs;
        return (uint32_t{HC_FullHouse} << 20) | (uint32_t(t) << 16) | (uint32_t(hiBit(pp)) << 12);
    }
    if (flushMask) return (uint32_t{HC_Flush} << 20) | topN(flushMask, 5, 16);
    int st = straightTop(all);
    if (st >= 0) return (uint32_t{HC_Straight} << 20) | (uint32_t(st) << 16);
    if (trips) {
        int t = hiBit(trips);
        uint32_t rest = all & ~(1u << t);
        return (uint32_t{HC_Trips} << 20) | (uint32_t(t) << 16) | topN(rest, 2, 12);
    }
    if (std::popcount(pairs) >= 2) {
        int p1 = hiBit(pairs);
        uint32_t rem = pairs & ~(1u << p1);
        int p2 = hiBit(rem);
        uint32_t rest = all & ~(1u << p1) & ~(1u << p2);
        return (uint32_t{HC_TwoPair} << 20) | (uint32_t(p1) << 16) | (uint32_t(p2) << 12) | topN(rest, 1, 8);
    }
    if (pairs) {
        int p = hiBit(pairs);
        uint32_t rest = all & ~(1u << p);
        return (uint32_t{HC_Pair} << 20) | (uint32_t(p) << 16) | topN(rest, 3, 12);
    }
    return topN(all, 5, 16);
}

inline int categoryOf(uint32_t score) { return static_cast<int>(score >> 20); }

}  // namespace ps
