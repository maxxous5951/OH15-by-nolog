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
	// Last used macro
	macro = OpenHoldemIni::ReadString(SettingsFile(), kSection, "macro", "RNbBPpPAdKsP").c_str();
}

void Registry::write_reg(void) 
{
	OpenHoldemIni::WriteString(SettingsFile(), kSection, "macro", macro.GetString());
}
