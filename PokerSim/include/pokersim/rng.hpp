// Générateur pseudo-aléatoire rapide et reproductible (xoshiro256**), identique sur tous les OS/compilateurs.
#pragma once
#include <cstdint>

namespace ps {

inline uint64_t splitmix64(uint64_t& x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

class Rng {
public:
    explicit Rng(uint64_t seed = 1) { reseed(seed); }
    void reseed(uint64_t seed) {
        uint64_t x = seed;
        for (auto& v : s_) v = splitmix64(x);
    }
    uint64_t next() {
        const uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0]; s_[3] ^= s_[1]; s_[1] ^= s_[2]; s_[0] ^= s_[3];
        s_[2] ^= t; s_[3] = rotl(s_[3], 45);
        return result;
    }
    // entier uniforme dans [0, n) (méthode de Lemire, sans biais)
    uint32_t below(uint32_t n) {
        uint64_t x = next() >> 32;
        uint64_t m = x * n;
        uint32_t l = static_cast<uint32_t>(m);
        if (l < n) {
            uint32_t t = (0u - n) % n;
            while (l < t) { x = next() >> 32; m = x * n; l = static_cast<uint32_t>(m); }
        }
        return static_cast<uint32_t>(m >> 32);
    }
    int range(int lo, int hi) { return lo + static_cast<int>(below(static_cast<uint32_t>(hi - lo + 1))); }
    double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); }

private:
    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }
    uint64_t s_[4];
};

}  // namespace ps
