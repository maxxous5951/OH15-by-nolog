#include "pokersim/prwin.hpp"
#include <cmath>
#include <vector>
#include "pokersim/handrank.hpp"

namespace ps::oh {
namespace {

inline int msvcRand(Rng& rng) { return static_cast<int>(rng.below(32768)); }   // RAND_MAX = 32767

inline uint32_t pokervalU(uint64_t mask, int n) {
    uint32_t hv = handValN(mask, n);
    int dummy = 0;
    return static_cast<uint32_t>(calcPokerval(hv, n, &dummy, kNoCardOH, kNoCardOH, mask));   // « unsigned int pokval = int » comme l'original
}

struct Dealer {
    const PrwinInput& in;
    Rng& rng;
    uint64_t used = 0, addl = 0;
    int ocard[2 * kMaxOpponents] = {0};      // persiste d'une itération à l'autre, comme l'original
    int totalWeight[10] = {0};
    int nCom = 0;

    int randomCard() {
        for (;;) {
            int c = msvcRand(rng) & 0x3F;
            if (c < 52 && !(used >> c & 1)) return c;
        }
    }
    void additionalCommon() {
        addl = 0;
        for (int i = 0; i < 5 - nCom; ++i) { int c = randomCard(); used |= peBit(c); addl |= peBit(c); }
    }
    void standardUpTo13(int nopp) {
        if (nopp < 1) nopp = 1;
        for (int i = 0; i < nopp * 2; i += 2) {
            uint64_t temp = used;
            do {
                used = temp;
                for (int j = 0; j < 2; ++j) { int c = randomCard(); used |= peBit(c); ocard[i + j] = c; }
                if (!in.willplay) break;
            } while (!isHandInWeightedRange(ocard[i], ocard[i + 1], in.willplay, in.wontplay, in.topclip, in.mustplay, rng));
        }
        additionalCommon();
    }
    void swapMoreThan13(int nopp) {
        int deck[52], n = 52;
        for (int i = 0; i < 52; ++i) deck[i] = i;
        while (n >= 1) {
            int sel = msvcRand(rng) % n;
            --n;
            if (sel != n) { int t = deck[sel]; deck[sel] = deck[n]; deck[n] = t; }
        }
        int x = 0;
        for (int i = 0; i < nopp * 2; ++i) {
            while (x < 52 && (used >> deck[x] & 1)) ++x;
            ocard[i] = deck[x++];
        }
        addl = 0;
        for (int i = 0; i < 5 - nCom; ++i) {
            while (x < 52 && (used >> deck[x] & 1)) ++x;
            addl |= peBit(deck[x++]);
        }
    }
    // renvoie 0 si succès, négatif si une plage ne contient que des cartes mortes
    int enhanced() {
        int k = 0;
        for (int chair = 0; chair < 10; ++chair) {
            if (chair == in.userchair) continue;
            if (!(in.playersPlayingBits & (1 << chair))) continue;
            int chairWeight = totalWeight[chair];
            const Prw1326Chair& pc = in.prw->chair[chair];
            if (pc.ignore || chairWeight <= 0) {
                for (int t = 0; t < 2; ++t) { int c = randomCard(); used |= peBit(c); ocard[k++] = c; }
                continue;
            }
            std::vector<char> dead(1326, 0);
            int deadCount = 0;
            bool found = false;
            while (!found) {
                int rw = static_cast<int>(rng.below(static_cast<uint32_t>(chairWeight)));
                for (int h = 0; h < pc.limit; ++h) {
                    if (!dead[h] && rw < pc.weight[h]) {
                        if ((used >> pc.rankhi[h] & 1) || (used >> pc.ranklo[h] & 1)) {
                            dead[h] = 1; ++deadCount; chairWeight -= pc.weight[h];
                            if (deadCount == pc.limit || chairWeight <= 0) return chair == 0 ? -10 : -chair;
                            break;
                        }
                        ocard[k++] = pc.rankhi[h]; ocard[k++] = pc.ranklo[h];
                        used |= peBit(ocard[k - 2]) | peBit(ocard[k - 1]);
                        found = true;
                        break;
                    }
                    if (!dead[h]) rw -= pc.weight[h];
                }
            }
        }
        additionalCommon();
        return 0;
    }
};

}  // namespace

bool isHandInWeightedRange(int card1, int card2, int willplay, int wontplay, int topclip, int mustplay, Rng& rng) {
    int suit1 = card1 / 13, suit2 = card2 / 13, r1 = card1 % 13, r2 = card2 % 13;
    if (r2 > r1) { int t = r1; r1 = r2; r2 = t; }
    int hrank = prwinPairRank(suit1 == suit2, r1, r2);
    if (hrank >= wontplay) return false;
    if (hrank <= mustplay) return true;
    if (hrank > topclip && hrank <= willplay) return true;
    if (hrank <= topclip) {
        int randomizedTopclip = topclip * (1 - msvcRand(rng));      // quasi toujours négatif : défaut d'OpenHoldem, conservé
        return hrank > randomizedTopclip;
    }
    int distance = wontplay - willplay;
    if (distance < 3) return true;
    int randomizedWillplay = willplay + msvcRand(rng) % distance;
    return hrank < randomizedWillplay;
}

PrwinResult computePrwin(const PrwinInput& in, Rng& rng) {
    PrwinResult r;
    const int required = in.iterations;
    if (required < 2 || in.nOpponents <= 0) return r;          // OpenHoldem : le thread ne calcule rien
    const bool enhanced = in.prw && in.prw->useme == 1326 && (!in.preflop || in.prw->preflop == 1326);
    int nopp = in.nOpponents > kMaxOpponents ? kMaxOpponents : in.nOpponents;
    int willplay = in.willplay, wontplay = in.wontplay;
    PrwinInput adj = in;
    if (willplay && willplay < 2 * nopp + 1) willplay = 2 * nopp + 1;     // AdjustPrwinVariablesIfNecessary
    if (wontplay < willplay) wontplay = willplay;
    adj.willplay = willplay; adj.wontplay = wontplay;
    Dealer dd{adj, rng};
    dd.nCom = in.nCom;
    if (enhanced) {
        for (int c = 0; c < 10; ++c) {
            if (c == in.userchair || !(in.playersPlayingBits & (1 << c))) continue;
            for (int h = 0; h < in.prw->chair[c].limit; ++h) dd.totalWeight[c] += in.prw->chair[c].weight[h];
        }
    }
    long win = 0, tie = 0, los = 0;
    for (int it = 1; it < required; ++it) {
        dd.used = in.plCards | in.comCards;
        if (enhanced) {
            int rc = dd.enhanced();
            if (rc < 0) { r.win = r.tie = r.lose = -1.0; return r; }
        } else if (nopp <= 13) dd.standardUpTo13(nopp);
        else dd.swapMoreThan13(nopp);
        uint64_t evalCards = in.plCards | in.comCards | dd.addl;
        uint32_t plVal = pokervalU(evalCards, 7);
        uint32_t oppMax = 0;
        bool lost = false;
        for (int i = 0; i < nopp; ++i) {
            uint64_t oc = in.comCards | dd.addl | peBit(dd.ocard[i * 2]) | peBit(dd.ocard[i * 2 + 1]);
            uint32_t ov = pokervalU(oc, 7);
            if (ov > plVal) { ++los; lost = true; break; }
            if (ov > oppMax) oppMax = ov;
        }
        if (!lost) { if (plVal > oppMax) ++win; else ++tie; }
    }
    r.win = win / static_cast<double>(required);    // OpenHoldem divise par `required` bien que la boucle fasse required-1 tours
    r.tie = tie / static_cast<double>(required);
    r.lose = los / static_cast<double>(required);
    return r;
}

NhandsResult computeNhands(uint64_t pl, uint64_t com, int nPl, int nCom, int nOpp) {
    NhandsResult out;
    const uint32_t plVal = pokervalU(pl | com, nPl + nCom);
    for (int i = 0; i < 51; ++i) {
        for (int j = i + 1; j < 52; ++j) {
            if ((pl >> i & 1) || (pl >> j & 1) || (com >> i & 1) || (com >> j & 1)) continue;
            uint32_t ov = pokervalU(com | peBit(i) | peBit(j), 2 + nCom);
            if (plVal > ov) ++out.lo; else if (plVal < ov) ++out.hi; else ++out.ti;
        }
    }
    int n = out.total();
    if (n > 0) {
        out.prwinnow = std::pow(static_cast<double>(out.lo) / n, nOpp);
        out.prlosnow = 1 - std::pow((static_cast<double>(out.lo) + out.ti) / n, nOpp);
    }
    return out;
}

}  // namespace ps::oh
