//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Tiny SVG path-data parser for the modern UI (icons).
//   Pure standard C++ (no Win32, no GDI+), so that it can be unit-tested
//   on any platform. All arcs are converted to cubic Bezier curves.
//
//******************************************************************************

#ifndef INC_MODERNUI_SVG_H
#define INC_MODERNUI_SVG_H

#include <vector>

namespace modernui {

struct PathSeg {
  enum Type { Move, Line, Cubic, Close };
  Type  type;
  // Move / Line: end point in (x[0], y[0]).
  // Cubic: control point 1 = (x[0], y[0]), control point 2 = (x[1], y[1]),
  //        end point = (x[2], y[2]).
  float x[3];
  float y[3];
};

// Parses SVG path data ("M8 3H7a2 2 0 0 0-2 2...") and appends the
// resulting segments to "out". Supports M m L l H h V v C c S s Q q T t A a Z z.
// Returns false on malformed input (segments parsed so far are kept).
bool ParseSvgPath(const char* d, std::vector<PathSeg>* out);

}  // namespace modernui

#endif  // INC_MODERNUI_SVG_H
