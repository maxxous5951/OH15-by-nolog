#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <vector>
#include "gui_platform_win32.hpp"

namespace psgui {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

}  // namespace

std::string Win32Platform::openFile(const char* title, const char* filterName, const char* patterns) {
    std::wstring t = widen(title), fn = widen(filterName), pat = widen(patterns);
    std::wstring filter = fn + L'\0' + pat + L'\0' + L"Tous les fichiers (*.*)" + L'\0' + L"*.*" + L'\0';
    std::vector<wchar_t> buf(32768, L'\0');
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = reinterpret_cast<HWND>(owner_);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrTitle = t.c_str();
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) return {};
    return narrow(buf.data());
}

std::string Win32Platform::saveFile(const char* title, const char* defaultName, const char* ext) {
    std::wstring t = widen(title), ex = widen(ext);
    std::wstring filter = L"Fichiers ." + ex + L'\0' + L"*." + ex + L'\0' + L"Tous les fichiers (*.*)" + L'\0' + L"*.*" + L'\0';
    std::vector<wchar_t> buf(32768, L'\0');
    std::wstring def = widen(defaultName);
    wcsncpy_s(buf.data(), buf.size(), def.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = reinterpret_cast<HWND>(owner_);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = static_cast<DWORD>(buf.size());
    ofn.lpstrTitle = t.c_str();
    ofn.lpstrDefExt = ex.c_str();
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    if (!GetSaveFileNameW(&ofn)) return {};
    return narrow(buf.data());
}

std::string Win32Platform::pickFolder(const char* title) {
    std::string result;
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return result;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    std::wstring t = widen(title);
    dlg->SetTitle(t.c_str());
    if (SUCCEEDED(dlg->Show(reinterpret_cast<HWND>(owner_)))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) { result = narrow(path); CoTaskMemFree(path); }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

void Win32Platform::openPath(const std::string& path) {
    std::wstring w = widen(path);
    ShellExecuteW(reinterpret_cast<HWND>(owner_), L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string Win32Platform::exeDir() {
    wchar_t buf[MAX_PATH * 4] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    return narrow(pos == std::wstring::npos ? L"." : p.substr(0, pos));
}

}  // namespace psgui
