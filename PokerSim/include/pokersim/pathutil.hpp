// Chemins UTF-8 <-> std::filesystem::path. Sous Windows, std::string est interprété avec la page de code ANSI par
// std::ifstream / std::filesystem : un dossier accentué casserait. L'interface et les profils utilisent donc UTF-8 partout.
#pragma once
#include <filesystem>
#include <string>

namespace ps {

inline std::filesystem::path pathFromUtf8(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
inline std::string pathToUtf8(const std::filesystem::path& p) {
    std::u8string u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}

}  // namespace ps
