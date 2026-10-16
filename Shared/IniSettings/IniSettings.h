//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Settings of the tools (OpenScrape, OHReplay, ManualMode, ...)
//   in an ini-file instead of the registry. Header-only.
//
//   Location: %APPDATA%\OpenHoldem\<Tool>.ini
//   (not the directory of the executable: OpenHoldem would mistake
//    every *.ini there for its own configuration)
//
//******************************************************************************

#ifndef INC_INISETTINGS_H
#define INC_INISETTINGS_H

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <string>

namespace OpenHoldemIni {

inline std::string SettingsDirectory() {
  char buffer[MAX_PATH] = { 0 };
  std::string directory;
  DWORD length = ::GetEnvironmentVariableA("APPDATA", buffer, MAX_PATH);
  if (length > 0 && length < MAX_PATH) {
    directory = std::string(buffer) + "\\OpenHoldem";
  } else {
    // No user profile: directory of the executable
    ::GetModuleFileNameA(NULL, buffer, MAX_PATH);
    char* last_backslash = strrchr(buffer, '\\');
    if (last_backslash != NULL) *last_backslash = '\0';
    directory = buffer;
  }
  ::CreateDirectoryA(directory.c_str(), NULL);   // fails harmlessly if it exists
  return directory;
}

// Complete path of the ini-file of a tool, e.g. IniFilePath("OpenScrape")
inline std::string IniFilePath(const char* tool_name) {
  return SettingsDirectory() + "\\" + tool_name + ".ini";
}

inline int ReadInt(const std::string& file, const char* section, const char* key, int default_value) {
  return (int)::GetPrivateProfileIntA(section, key, default_value, file.c_str());
}

inline std::string ReadString(const std::string& file, const char* section, const char* key,
                              const char* default_value) {
  char buffer[1024] = { 0 };
  ::GetPrivateProfileStringA(section, key, default_value, buffer, sizeof(buffer), file.c_str());
  return std::string(buffer);
}

inline void WriteInt(const std::string& file, const char* section, const char* key, int value) {
  char text[32];
  sprintf_s(text, sizeof(text), "%d", value);
  ::WritePrivateProfileStringA(section, key, text, file.c_str());
}

inline void WriteString(const std::string& file, const char* section, const char* key, const char* value) {
  ::WritePrivateProfileStringA(section, key, value, file.c_str());
}

}  // namespace OpenHoldemIni

#endif  // INC_INISETTINGS_H
