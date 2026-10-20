// Dialogues Windows natifs (Unicode) pour l'interface PokerSim.
#pragma once
#include <string>
#include "gui_app.hpp"

struct HWND__;
namespace psgui {

class Win32Platform : public Platform {
public:
    explicit Win32Platform(HWND__* owner) : owner_(owner) {}
    std::string openFile(const char* title, const char* filterName, const char* patterns) override;
    std::string saveFile(const char* title, const char* defaultName, const char* ext) override;
    std::string pickFolder(const char* title) override;
    std::string exeDir() override;
private:
    HWND__* owner_;
};

}  // namespace psgui
