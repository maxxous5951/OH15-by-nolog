//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose:
//
//******************************************************************************


#include "stdafx.h"

#include "registry.h"
#include "..\Shared\IniSettings\IniSettings.h"
#include "stdio.h"
#include "stdlib.h"

// The settings are stored in an ini-file now (%APPDATA%\OpenHoldem\OHReplay.ini)
// instead of the registry. The class name stayed to keep the callers unchanged.
static const char* kSection = "OHReplay";

static std::string SettingsFile() {
	return OpenHoldemIni::IniFilePath("OHReplay");
}

Registry::Registry(void) 
{
}

void Registry::read_reg(void) 
{
	const std::string file = SettingsFile();
	// Main window location
	main_x = OpenHoldemIni::ReadInt(file, kSection, "main_x", 0);
	main_y = OpenHoldemIni::ReadInt(file, kSection, "main_y", 0);
}

void Registry::write_reg(void) 
{
	const std::string file = SettingsFile();
	// Main window location
	OpenHoldemIni::WriteInt(file, kSection, "main_x", main_x);
	OpenHoldemIni::WriteInt(file, kSection, "main_y", main_y);
}
