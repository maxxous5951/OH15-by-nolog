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
#include "DialogTableMap.h"

// The settings are stored in an ini-file now (%APPDATA%\OpenHoldem\OpenScrape.ini)
// instead of the registry. The class name stayed to keep the callers unchanged.
static const char* kSection = "OpenScrape";

static std::string SettingsFile() {
	return OpenHoldemIni::IniFilePath("OpenScrape");
}

Registry::Registry(void) 
{
}

void Registry::read_reg(void) 
{
	const std::string file = SettingsFile();

	// Defaults
	tablemap_x  = 0;
	tablemap_y  = 0;
	tablemap_dx = 600;
	tablemap_dy = 750;

	main_x  = tablemap_dx;
	main_y  = 0;
	main_dx = 400;
	main_dy = 300;

	// No longer any need to get...   
	//   * Main window location and size
	//   * tablemap window location and size
	// The defaults are fine.

	// graphical hash point window location and size
	grhash_x  = OpenHoldemIni::ReadInt(file, kSection, "grhash_x", 0);
	grhash_y  = OpenHoldemIni::ReadInt(file, kSection, "grhash_y", 0);
	grhash_dx = OpenHoldemIni::ReadInt(file, kSection, "grhash_dx", 380);
	grhash_dy = OpenHoldemIni::ReadInt(file, kSection, "grhash_dy", 330);

	// Region grouping
	region_grouping = OpenHoldemIni::ReadInt(file, kSection, "region_grouping", BY_TYPE);

	// Window capture and screenshot-protection
	capture_method    = OpenHoldemIni::ReadInt(file, kSection, "capture_method", kCaptureMethodOriginal);
	block_screenshots = OpenHoldemIni::ReadInt(file, kSection, "block_screenshots", 0);
}

void Registry::write_reg(void) 
{
	const std::string file = SettingsFile();

	// graphical hash point window location and size
	OpenHoldemIni::WriteInt(file, kSection, "grhash_x", grhash_x);
	OpenHoldemIni::WriteInt(file, kSection, "grhash_y", grhash_y);
	OpenHoldemIni::WriteInt(file, kSection, "grhash_dx", grhash_dx);
	OpenHoldemIni::WriteInt(file, kSection, "grhash_dy", grhash_dy);

	// Region grouping
	OpenHoldemIni::WriteInt(file, kSection, "region_grouping", region_grouping);

	// Window capture and screenshot-protection
	OpenHoldemIni::WriteInt(file, kSection, "capture_method", capture_method);
	OpenHoldemIni::WriteInt(file, kSection, "block_screenshots", block_screenshots);
}
