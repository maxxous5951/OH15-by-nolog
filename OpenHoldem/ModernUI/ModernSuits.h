//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Vector shapes of the four card suits (24x24 grid, filled).
//   Plain data, no dependencies.
//
//******************************************************************************

#ifndef INC_MODERNUI_SUITS_H
#define INC_MODERNUI_SUITS_H

namespace modernui {

static const char* const kSuitHeart[] = {
  "M12 21.5C5.5 16 2 12.6 2 8.4C2 5.4 4.3 3.2 7 3.2C9 3.2 10.9 4.4 12 6.3"
  "C13.1 4.4 15 3.2 17 3.2C19.7 3.2 22 5.4 22 8.4C22 12.6 18.5 16 12 21.5Z",
};
static const char* const kSuitDiamond[] = {
  "M12 1.5L20 12L12 22.5L4 12Z",
};
static const char* const kSuitSpade[] = {
  "M12 1.8C12 1.8 3 8.6 3 14.3C3 17.1 5.1 19 7.7 19C9.4 19 10.9 18.2 11.7 16.9"
  "C11.6 19.2 10.6 21 8.6 22.2L15.4 22.2C13.4 21 12.4 19.2 12.3 16.9"
  "C13.1 18.2 14.6 19 16.3 19C18.9 19 21 17.1 21 14.3C21 8.6 12 1.8 12 1.8Z",
};
static const char* const kSuitClub[] = {
  "M7.4 7.4a4.6 4.6 0 1 0 9.2 0a4.6 4.6 0 1 0 -9.2 0z",
  "M1.6 14.6a4.6 4.6 0 1 0 9.2 0a4.6 4.6 0 1 0 -9.2 0z",
  "M13.2 14.6a4.6 4.6 0 1 0 9.2 0a4.6 4.6 0 1 0 -9.2 0z",
  "M10.8 12.5L13.2 12.5L13.2 16.5C13.2 19 14.2 20.8 16 22.2L8 22.2C9.8 20.8 10.8 19 10.8 16.5Z",
};

}  // namespace modernui

#endif  // INC_MODERNUI_SUITS_H
