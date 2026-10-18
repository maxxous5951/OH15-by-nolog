// Cartes : 0..51, carte = rang*4 + couleur ; rang 0='2' ... 12='A' ; couleur 0..3 = c,d,h,s (ordre OpenHoldem).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "pokersim/rng.hpp"

namespace ps {

using Card = uint8_t;
constexpr Card kNoCard = 0xFF;

constexpr int rankOf(Card c) { return c >> 2; }
constexpr int suitOf(Card c) { return c & 3; }
constexpr Card makeCard(int rank, int suit) { return static_cast<Card>(rank * 4 + suit); }

inline char rankChar(int r) { return "23456789TJQKA"[r]; }
inline char suitChar(int s) { return "cdhs"[s]; }
inline std::string cardToString(Card c) { return c == kNoCard ? "??" : std::string{rankChar(rankOf(c)), suitChar(suitOf(c))}; }
// "As", "td"... ; renvoie kNoCard si invalide
Card cardFromString(const char* s);

// Paquet à tirage partiel (Fisher-Yates paresseux) : on ne mélange que ce qu'on pioche.
class Deck {
public:
    Deck() { reset(); }
    void reset() { for (int i = 0; i < 52; ++i) c_[i] = static_cast<Card>(i); n_ = 52; }
    // retire une carte précise du paquet (cartes connues)
    void remove(Card c) {
        for (int i = 0; i < n_; ++i) if (c_[i] == c) { c_[i] = c_[--n_]; return; }
    }
    Card draw(Rng& rng) {
        int i = static_cast<int>(rng.below(static_cast<uint32_t>(n_)));
        Card r = c_[i];
        c_[i] = c_[--n_];
        return r;
    }
    int size() const { return n_; }
    const Card* data() const { return c_.data(); }

private:
    std::array<Card, 52> c_;
    int n_;
};

}  // namespace ps
