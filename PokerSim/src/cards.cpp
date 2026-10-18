#include "pokersim/cards.hpp"
#include <cctype>
#include <cstring>

namespace ps {

Card cardFromString(const char* s) {
    if (!s || !s[0] || !s[1]) return kNoCard;
    const char* ranks = "23456789TJQKA";
    const char* suits = "cdhs";
    const char* r = std::strchr(ranks, std::toupper(static_cast<unsigned char>(s[0])));
    const char* u = std::strchr(suits, std::tolower(static_cast<unsigned char>(s[1])));
    if (!r || !u || !*r || !*u) return kNoCard;
    return makeCard(static_cast<int>(r - ranks), static_cast<int>(u - suits));
}

}  // namespace ps
