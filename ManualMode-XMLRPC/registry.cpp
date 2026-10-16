
#include "stdafx.h"

#include "registry.h"
#include "..\Shared\IniSettings\IniSettings.h"
#include "stdio.h"
#include "stdlib.h"
#include "debug.h"


// The settings are stored in an ini-file now (%APPDATA%\OpenHoldem\ManualMode.ini)
// instead of the registry. The class name stayed to keep the callers unchanged.
static const char* kSection = "ManualMode";

static std::string SettingsFile() {
	return OpenHoldemIni::IniFilePath("ManualMode");
}

Registry::Registry(void) 
{
}

void Registry::read_reg(void) 
{
	const std::string file = SettingsFile();
	// Window location
	manual_x = OpenHoldemIni::ReadInt(file, kSection, "manual_x", 0);
	manual_y = OpenHoldemIni::ReadInt(file, kSection, "manual_y", 0);
	// Last used macro
	macro = OpenHoldemIni::ReadString(file, kSection, "macro", "RPPPPPPPPPPbB").c_str();
	// Popup location
	unobstructivePopup = (OpenHoldemIni::ReadInt(file, kSection, "unobstructivePopup", 1) != 0);
}

void Registry::write_reg(void) 
{
	const std::string file = SettingsFile();
	OpenHoldemIni::WriteInt(file, kSection, "manual_x", manual_x);
	OpenHoldemIni::WriteInt(file, kSection, "manual_y", manual_y);
	OpenHoldemIni::WriteString(file, kSection, "macro", macro.GetString());
	OpenHoldemIni::WriteInt(file, kSection, "unobstructivePopup", unobstructivePopup ? 1 : 0);
}
