#include "pokersim/pokerval.hpp"
#include <bit>
#include <climits>

namespace ps::oh {
namespace {

constexpr uint32_t kMask13 = 0x1FFF;
constexpr int kHearts = 0, kDiamonds = 13, kClubs = 26, kSpades = 39;   // décalages des couleurs dans le masque

inline uint32_t suitBits(uint64_t m, int shift) { return static_cast<uint32_t>((m >> shift) & kMask13); }
inline int nBits(uint32_t x) { return std::popcount(x); }
inline int topCard(uint32_t x) { return x ? 31 - std::countl_zero(x) : 0; }     // topCardTable

// topFiveCardsTable : 5 meilleurs rangs empilés (top<<16 ... fifth<<0), les absents valent 0
uint32_t topFive(uint32_t x) {
    uint32_t out = 0;
    for (int i = 0; i < 5 && x; ++i) { int r = topCard(x); x &= ~(1u << r); out |= static_cast<uint32_t>(r) << (16 - 4 * i); }
    return out;
}
// straightTable : rang du haut de la meilleure quinte (roue = 3), 0 si aucune
int straightTop(uint32_t x) {
    uint32_t m = (x << 1) | ((x >> 12) & 1u);
    uint32_t t = m & (m >> 1) & (m >> 2) & (m >> 3) & (m >> 4);
    return t ? topCard(t) + 3 : 0;
}

constexpr uint32_t HT(int t) { return static_cast<uint32_t>(t) << 24; }
constexpr uint32_t TOP(uint32_t c) { return c << 16; }
constexpr uint32_t SECOND(uint32_t c) { return c << 12; }
constexpr uint32_t THIRD(uint32_t c) { return c << 8; }
constexpr int NOPAIR = 0, ONEPAIR = 1, TWOPAIR = 2, TRIPS = 3, STRAIGHT = 4, FLUSH = 5, FULLHOUSE = 6, QUADS = 7, STFLUSH = 8;

}  // namespace

// Port littéral de StdDeck_StdRules_EVAL_N (poker-eval)
uint32_t handValN(uint64_t cards, int nCards) {
    const uint32_t SH = suitBits(cards, kHearts), SD = suitBits(cards, kDiamonds), SC = suitBits(cards, kClubs), SS = suitBits(cards, kSpades);
    uint32_t retval = 0;
    const uint32_t ranks = SC | SD | SH | SS;
    const int nRanks = nBits(ranks);
    const int nDups = nCards - nRanks;
    if (nRanks >= 5) {
        const uint32_t suits[4] = {SS, SC, SD, SH};   // ordre de l'original : S, C, D, H
        bool done = false;
        for (uint32_t s : suits) {
            if (nBits(s) >= 5) {
                int st = straightTop(s);
                if (st) return HT(STFLUSH) + TOP(st);
                retval = HT(FLUSH) + topFive(s);
                done = true;
                break;
            }
        }
        if (!done) { int st = straightTop(ranks); if (st) retval = HT(STRAIGHT) + TOP(st); }
        if (retval && nDups < 3) return retval;
    }
    switch (nDups) {
        case 0: return HT(NOPAIR) + topFive(ranks);
        case 1: {
            uint32_t twoMask = ranks ^ (SC ^ SD ^ SH ^ SS);
            retval = HT(ONEPAIR) + TOP(topCard(twoMask));
            uint32_t t = ranks ^ twoMask;
            uint32_t kickers = (topFive(t) >> 4) & ~0xFu;      // (>> CARD_WIDTH) & ~FIFTH_CARD_MASK
            return retval + kickers;
        }
        case 2: {
            uint32_t twoMask = ranks ^ (SC ^ SD ^ SH ^ SS);
            if (twoMask) {
                uint32_t t = ranks ^ twoMask;
                return HT(TWOPAIR) + (topFive(twoMask) & (0x000F0000u | 0x0000F000u)) + THIRD(topCard(t));
            }
            uint32_t threeMask = ((SC & SD) | (SH & SS)) & ((SC & SH) | (SD & SS));
            retval = HT(TRIPS) + TOP(topCard(threeMask));
            uint32_t t = ranks ^ threeMask;
            int second = topCard(t);
            retval += SECOND(second);
            t ^= (1u << second);
            return retval + THIRD(topCard(t));
        }
        default: {
            uint32_t fourMask = SH & SD & SC & SS;
            if (fourMask) {
                int tc = topCard(fourMask);
                return HT(QUADS) + TOP(tc) + SECOND(topCard(ranks ^ (1u << tc)));
            }
            uint32_t twoMask = ranks ^ (SC ^ SD ^ SH ^ SS);
            if (nBits(twoMask) != nDups) {
                uint32_t threeMask = ((SC & SD) | (SH & SS)) & ((SC & SH) | (SD & SS));
                int tc = topCard(threeMask);
                uint32_t t = (twoMask | threeMask) ^ (1u << tc);
                return HT(FULLHOUSE) + TOP(tc) + SECOND(topCard(t));
            }
            if (retval) return retval;
            int top = topCard(twoMask);
            retval = HT(TWOPAIR) + TOP(top);
            int second = topCard(twoMask ^ (1u << top));
            retval += SECOND(second);
            return retval + THIRD(topCard(ranks ^ (1u << top) ^ (1u << second)));
        }
    }
}

// Port littéral de CSymbolEnginePokerval::CalculatePokerval
int calcPokerval(uint32_t hv, int n, int* pcb, int card0, int card1, uint64_t allCards) {
    constexpr uint32_t kOnePair = 0x01000000, kTwoPair = 0x02000000, kTrips = 0x04000000, kStraight = 0x08000000,
                       kFlush = 0x10000000, kFullHouse = 0x20000000, kQuads = 0x40000000, kStFlush = 0x80000000;
    double pv = 0.0;
    int bits = 0;
    const int type = hvType(hv);
    auto TOPc = [&] { return hvCard(hv, 0); };
    auto SEC = [&] { return hvCard(hv, 1); };
    auto THI = [&] { return hvCard(hv, 2); };
    auto FOU = [&] { return hvCard(hv, 3); };
    auto FIF = [&] { return hvCard(hv, 4); };
    auto RANK = [](int c) { return c % 13; };
    auto SUIT = [](int c) { return c / 13; };

    int flushSuit = -1;
    if (type == STFLUSH || type == FLUSH) {
        int max = 0;
        const struct { int shift, suit; } order[4] = {{kSpades, kPeSpades}, {kHearts, kPeHearts}, {kDiamonds, kPeDiamonds}, {kClubs, kPeClubs}};
        for (auto& o : order) {
            int c = nBits(suitBits(allCards, o.shift));
            if (c > max && c > 0) { max = c; flushSuit = o.suit; }
        }
    }
    const int j0 = RANK(card0), k0 = RANK(card1);
    switch (type) {
        case STFLUSH:
            pv += kStFlush;
            pv += (TOPc() + 2 - 0) << 16; pv += (TOPc() + 2 - 1) << 12; pv += (TOPc() + 2 - 2) << 8;
            pv += (TOPc() + 2 - 3) << 4;  pv += (TOPc() + 2 - 4) << 0;
            for (int i = 0; i < 5; ++i) {
                int j = j0, k = k0;
                if ((j == TOPc() - i && SUIT(card0) == flushSuit) || (k == TOPc() - i && SUIT(card1) == flushSuit)) bits |= (1 << (4 - i));
                if (j == 12 || k == 12) {
                    if (j == 12) j = -1;
                    if (k == 12) k = -1;
                    if ((j == TOPc() - i && SUIT(card0) == flushSuit) || (k == TOPc() - i && SUIT(card1) == flushSuit)) bits |= (1 << (4 - i));
                }
            }
            break;
        case QUADS:
            pv += kQuads;
            pv += (TOPc() + 2) << 16; pv += (TOPc() + 2) << 12; pv += (TOPc() + 2) << 8; pv += (TOPc() + 2) << 4;
            if (n >= 5) pv += (SEC() + 2) << 0;
            if ((RANK(card0) == TOPc()) != (RANK(card1) == TOPc())) bits |= (1 << 4);
            else if (RANK(card0) == TOPc() && RANK(card1) == TOPc()) { bits |= (1 << 4); bits |= (1 << 3); }
            if (n >= 5 && (RANK(card0) == SEC() || RANK(card1) == SEC())) bits |= (1 << 0);
            break;
        case FULLHOUSE:
            pv += kFullHouse;
            pv += (TOPc() + 2) << 16; pv += (TOPc() + 2) << 12; pv += (TOPc() + 2) << 8; pv += (SEC() + 2) << 4; pv += (SEC() + 2) << 0;
            if ((RANK(card0) == TOPc()) != (RANK(card1) == TOPc())) bits |= (1 << 4);
            else if (RANK(card0) == TOPc() && RANK(card1) == TOPc()) { bits |= (1 << 4); bits |= (1 << 3); }
            if ((RANK(card0) == SEC()) != (RANK(card1) == SEC())) bits |= (1 << 1);
            else if (RANK(card0) == SEC() && RANK(card1) == SEC()) { bits |= (1 << 1); bits |= (1 << 0); }
            break;
        case FLUSH: {
            pv += kFlush;
            pv += (TOPc() + 2) << 16; pv += (SEC() + 2) << 12; pv += (THI() + 2) << 8; pv += (FOU() + 2) << 4; pv += (FIF() + 2) << 0;
            const int cr[5] = {TOPc(), SEC(), THI(), FOU(), FIF()};
            for (int i = 0; i < 5; ++i)
                if ((RANK(card0) == cr[i] && SUIT(card0) == flushSuit) || (RANK(card1) == cr[i] && SUIT(card1) == flushSuit)) bits |= (1 << (4 - i));
            break;
        }
        case STRAIGHT:
            pv += kStraight;
            pv += (TOPc() + 2 - 0) << 16; pv += (TOPc() + 2 - 1) << 12; pv += (TOPc() + 2 - 2) << 8;
            pv += (TOPc() + 2 - 3) << 4;  pv += (TOPc() + 2 - 4) << 0;
            for (int i = 0; i < 5; ++i) {
                int j = j0, k = k0;
                if (j == TOPc() - i || k == TOPc() - i) bits |= (1 << (4 - i));
                if (j == 12 || k == 12) {
                    if (j == 12) j = -1;
                    if (k == 12) k = -1;
                    if (j == TOPc() - i || k == TOPc() - i) bits |= (1 << (4 - i));
                }
            }
            break;
        case TRIPS:
            pv += kTrips;
            pv += (TOPc() + 2) << 16; pv += (TOPc() + 2) << 12; pv += (TOPc() + 2) << 8;
            if (n >= 4) pv += (SEC() + 2) << 4;
            if (n >= 5) pv += (THI() + 2) << 0;
            if ((RANK(card0) == TOPc()) != (RANK(card1) == TOPc())) bits |= (1 << 4);
            else if (RANK(card0) == TOPc() && RANK(card1) == TOPc()) { bits |= (1 << 4); bits |= (1 << 3); }
            if (n >= 4 && (RANK(card0) == SEC() || RANK(card1) == SEC())) bits |= (1 << 1);
            if (n >= 5 && (RANK(card0) == THI() || RANK(card1) == THI())) bits |= (1 << 1);
            break;
        case TWOPAIR:
            pv += kTwoPair;
            pv += (TOPc() + 2) << 16; pv += (TOPc() + 2) << 12; pv += (SEC() + 2) << 8; pv += (SEC() + 2) << 4;
            if (n >= 5) pv += (THI() + 2) << 0;
            if ((RANK(card0) == TOPc()) != (RANK(card1) == TOPc())) bits |= (1 << 4);
            else if (RANK(card0) == TOPc() && RANK(card1) == TOPc()) { bits |= (1 << 4); bits |= (1 << 3); }
            if ((RANK(card0) == SEC()) != (RANK(card1) == SEC())) bits |= (1 << 2);
            else if (RANK(card0) == SEC() && RANK(card1) == SEC()) { bits |= (1 << 2); bits |= (1 << 1); }
            if (n >= 5 && (RANK(card0) == THI() || RANK(card1) == THI())) bits |= (1 << 0);
            break;
        case ONEPAIR:
            pv += kOnePair;
            pv += (TOPc() + 2) << 16; pv += (TOPc() + 2) << 12;
            if (n >= 3) pv += (SEC() + 2) << 8;
            if (n >= 4) pv += (THI() + 2) << 4;
            if (n >= 5) pv += (FOU() + 2) << 0;
            if ((RANK(card0) == RANK(TOPc())) != (RANK(card1) == RANK(TOPc()))) bits |= (1 << 4);
            else if (RANK(card0) == RANK(TOPc()) && RANK(card1) == RANK(TOPc())) { bits |= (1 << 4); bits |= (1 << 3); }
            if (n >= 3 && (RANK(card0) == SEC() || RANK(card1) == SEC())) bits |= (1 << 2);
            if (n >= 4 && (RANK(card0) == THI() || RANK(card1) == THI())) bits |= (1 << 1);
            if (n >= 5 && (RANK(card0) == FOU() || RANK(card1) == FOU())) bits |= (1 << 0);
            break;
        case NOPAIR:
        default:
            if (n >= 1) pv += (TOPc() + 2) << 16;
            if (n >= 2) pv += (SEC() + 2) << 12;
            if (n >= 3) pv += (THI() + 2) << 8;
            if (n >= 4) pv += (FOU() + 2) << 4;
            if (n >= 5) pv += (FIF() + 2) << 0;
            if (n >= 1 && (RANK(card0) == TOPc() || RANK(card1) == TOPc())) bits |= (1 << 4);
            if (n >= 2 && (RANK(card0) == SEC() || RANK(card1) == SEC())) bits |= (1 << 3);
            if (n >= 3 && (RANK(card0) == THI() || RANK(card1) == THI())) bits |= (1 << 2);
            if (n >= 3 && (RANK(card0) == FOU() || RANK(card1) == FOU())) bits |= (1 << 1);
            if (n >= 3 && (RANK(card0) == FIF() || RANK(card1) == FIF())) bits |= (1 << 0);
            break;
    }
    *pcb = bits;
    // l'original renvoie un double converti en int : hors plage (quinte flush) = « integer indefinite » x86 = INT_MIN
    if (pv >= 2147483648.0 || pv <= -2147483649.0 || pv != pv) return INT_MIN;
    return static_cast<int>(pv);
}

}  // namespace ps::oh
