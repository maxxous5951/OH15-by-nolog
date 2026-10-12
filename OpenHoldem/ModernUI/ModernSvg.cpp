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
//
//******************************************************************************

#include "ModernSvg.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace modernui {

namespace {

const double kPi = 3.14159265358979323846;

class Scanner {
 public:
  explicit Scanner(const char* s) : p_(s) {}
  void SkipSeparators() {
    while (*p_ == ' ' || *p_ == ',' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r') ++p_;
  }
  bool AtEnd() { SkipSeparators(); return *p_ == '\0'; }
  bool AtCommand() {
    SkipSeparators();
    return (*p_ >= 'A' && *p_ <= 'Z') || (*p_ >= 'a' && *p_ <= 'z');
  }
  char ReadCommand() { SkipSeparators(); return *p_++; }
  bool HasNumber() {
    SkipSeparators();
    return (*p_ >= '0' && *p_ <= '9') || *p_ == '-' || *p_ == '+' || *p_ == '.';
  }
  bool ReadNumber(double* v) {
    SkipSeparators();
    const char* start = p_;
    if (*p_ == '-' || *p_ == '+') ++p_;
    bool digits = false;
    while (*p_ >= '0' && *p_ <= '9') { ++p_; digits = true; }
    if (*p_ == '.') {
      ++p_;
      while (*p_ >= '0' && *p_ <= '9') { ++p_; digits = true; }
    }
    if (!digits) { p_ = start; return false; }
    if (*p_ == 'e' || *p_ == 'E') {
      const char* save = p_;
      ++p_;
      if (*p_ == '-' || *p_ == '+') ++p_;
      if (*p_ >= '0' && *p_ <= '9') {
        while (*p_ >= '0' && *p_ <= '9') ++p_;
      } else {
        p_ = save;
      }
    }
    *v = std::strtod(start, nullptr);
    return true;
  }
  // Arc flags may be written without separators ("a2 2 0 011 1"), i.e. as a single digit.
  bool ReadFlag(int* flag) {
    SkipSeparators();
    if (*p_ == '0' || *p_ == '1') { *flag = *p_ - '0'; ++p_; return true; }
    return false;
  }
 private:
  const char* p_;
};

void AddMove(std::vector<PathSeg>* o, double x, double y) {
  PathSeg s; s.type = PathSeg::Move;
  s.x[0] = (float)x; s.y[0] = (float)y; s.x[1] = s.x[2] = s.y[1] = s.y[2] = 0;
  o->push_back(s);
}
void AddLine(std::vector<PathSeg>* o, double x, double y) {
  PathSeg s; s.type = PathSeg::Line;
  s.x[0] = (float)x; s.y[0] = (float)y; s.x[1] = s.x[2] = s.y[1] = s.y[2] = 0;
  o->push_back(s);
}
void AddCubic(std::vector<PathSeg>* o, double x1, double y1, double x2, double y2, double x, double y) {
  PathSeg s; s.type = PathSeg::Cubic;
  s.x[0] = (float)x1; s.y[0] = (float)y1;
  s.x[1] = (float)x2; s.y[1] = (float)y2;
  s.x[2] = (float)x;  s.y[2] = (float)y;
  o->push_back(s);
}
void AddClose(std::vector<PathSeg>* o) {
  PathSeg s; s.type = PathSeg::Close;
  s.x[0] = s.x[1] = s.x[2] = s.y[0] = s.y[1] = s.y[2] = 0;
  o->push_back(s);
}

// SVG 1.1 implementation notes, F.6.5 / F.6.6: endpoint -> center parameterization.
void AddArc(std::vector<PathSeg>* o, double x0, double y0, double rx, double ry,
            double phi_deg, int large_arc, int sweep, double x, double y) {
  if (x0 == x && y0 == y) return;           // zero-length arc: omitted
  rx = std::fabs(rx); ry = std::fabs(ry);
  if (rx == 0 || ry == 0) { AddLine(o, x, y); return; }
  const double phi = phi_deg * kPi / 180.0;
  const double cphi = std::cos(phi), sphi = std::sin(phi);
  const double dx2 = (x0 - x) / 2.0, dy2 = (y0 - y) / 2.0;
  const double x1p =  cphi * dx2 + sphi * dy2;
  const double y1p = -sphi * dx2 + cphi * dy2;
  // Scale radii up if they are too small.
  double lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
  if (lambda > 1.0) { double s = std::sqrt(lambda); rx *= s; ry *= s; }
  const double rx2 = rx * rx, ry2 = ry * ry;
  double num = rx2 * ry2 - rx2 * y1p * y1p - ry2 * x1p * x1p;
  double den = rx2 * y1p * y1p + ry2 * x1p * x1p;
  double coef = (den == 0) ? 0 : std::sqrt(std::fmax(0.0, num / den));
  if (large_arc == sweep) coef = -coef;
  const double cxp =  coef * (rx * y1p / ry);
  const double cyp = -coef * (ry * x1p / rx);
  const double cx = cphi * cxp - sphi * cyp + (x0 + x) / 2.0;
  const double cy = sphi * cxp + cphi * cyp + (y0 + y) / 2.0;
  const double theta1 = std::atan2((y1p - cyp) / ry, (x1p - cxp) / rx);
  double theta2 = std::atan2((-y1p - cyp) / ry, (-x1p - cxp) / rx);
  double dtheta = theta2 - theta1;
  if (sweep && dtheta < 0) dtheta += 2 * kPi;
  else if (!sweep && dtheta > 0) dtheta -= 2 * kPi;
  // Split into segments of at most 90 degrees.
  int n = (int)std::ceil(std::fabs(dtheta) / (kPi / 2.0) - 1e-9);
  if (n < 1) n = 1;
  const double delta = dtheta / n;
  const double t = (4.0 / 3.0) * std::tan(delta / 4.0);
  double a = theta1;
  for (int i = 0; i < n; ++i) {
    const double a2 = a + delta;
    const double ca = std::cos(a), sa = std::sin(a), cb = std::cos(a2), sb = std::sin(a2);
    // Points on the unit circle, then scale/rotate/translate.
    double p1x = ca - t * sa, p1y = sa + t * ca;
    double p2x = cb + t * sb, p2y = sb - t * cb;
    double p3x = cb,          p3y = sb;
    auto tx = [&](double ux, double uy) { return cphi * rx * ux - sphi * ry * uy + cx; };
    auto ty = [&](double ux, double uy) { return sphi * rx * ux + cphi * ry * uy + cy; };
    double ex = (i == n - 1) ? x : tx(p3x, p3y);   // avoid drift at the end
    double ey = (i == n - 1) ? y : ty(p3x, p3y);
    AddCubic(o, tx(p1x, p1y), ty(p1x, p1y), tx(p2x, p2y), ty(p2x, p2y), ex, ey);
    a = a2;
  }
}

}  // namespace

bool ParseSvgPath(const char* d, std::vector<PathSeg>* out) {
  if (d == nullptr || out == nullptr) return false;
  Scanner sc(d);
  double cx = 0, cy = 0;            // current point
  double sx = 0, sy = 0;            // start of current subpath
  double lcx = 0, lcy = 0;          // last cubic control point (for S/s)
  double lqx = 0, lqy = 0;          // last quad control point (for T/t)
  char prev = 0;                    // previous command (to detect smooth curves)
  char cmd = 0;
  while (!sc.AtEnd()) {
    if (sc.AtCommand()) {
      cmd = sc.ReadCommand();
    } else if (cmd == 0) {
      return false;
    } else if (cmd == 'M') { cmd = 'L'; }     // implicit lineto after moveto
    else if (cmd == 'm') { cmd = 'l'; }
    const bool rel = (cmd >= 'a' && cmd <= 'z');
    const char up = (char)(rel ? cmd - 'a' + 'A' : cmd);
    double v[7];
    auto num = [&](int count) {
      for (int i = 0; i < count; ++i) if (!sc.ReadNumber(&v[i])) return false;
      return true;
    };
    switch (up) {
      case 'Z':
        AddClose(out);
        cx = sx; cy = sy;
        prev = 'Z';
        // Z takes no arguments; next token must be a command or end.
        continue;
      case 'M': {
        if (!num(2)) return false;
        double x = v[0] + (rel ? cx : 0), y = v[1] + (rel ? cy : 0);
        AddMove(out, x, y); cx = sx = x; cy = sy = y;
        break;
      }
      case 'L': {
        if (!num(2)) return false;
        double x = v[0] + (rel ? cx : 0), y = v[1] + (rel ? cy : 0);
        AddLine(out, x, y); cx = x; cy = y;
        break;
      }
      case 'H': {
        if (!num(1)) return false;
        double x = v[0] + (rel ? cx : 0);
        AddLine(out, x, cy); cx = x;
        break;
      }
      case 'V': {
        if (!num(1)) return false;
        double y = v[0] + (rel ? cy : 0);
        AddLine(out, cx, y); cy = y;
        break;
      }
      case 'C': {
        if (!num(6)) return false;
        double ox = rel ? cx : 0, oy = rel ? cy : 0;
        AddCubic(out, v[0] + ox, v[1] + oy, v[2] + ox, v[3] + oy, v[4] + ox, v[5] + oy);
        lcx = v[2] + ox; lcy = v[3] + oy; cx = v[4] + ox; cy = v[5] + oy;
        break;
      }
      case 'S': {
        if (!num(4)) return false;
        double ox = rel ? cx : 0, oy = rel ? cy : 0;
        double c1x = cx, c1y = cy;
        if (prev == 'C' || prev == 'S') { c1x = 2 * cx - lcx; c1y = 2 * cy - lcy; }
        AddCubic(out, c1x, c1y, v[0] + ox, v[1] + oy, v[2] + ox, v[3] + oy);
        lcx = v[0] + ox; lcy = v[1] + oy; cx = v[2] + ox; cy = v[3] + oy;
        break;
      }
      case 'Q': {
        if (!num(4)) return false;
        double ox = rel ? cx : 0, oy = rel ? cy : 0;
        double qx = v[0] + ox, qy = v[1] + oy, x = v[2] + ox, y = v[3] + oy;
        AddCubic(out, cx + 2.0 / 3.0 * (qx - cx), cy + 2.0 / 3.0 * (qy - cy),
                 x + 2.0 / 3.0 * (qx - x), y + 2.0 / 3.0 * (qy - y), x, y);
        lqx = qx; lqy = qy; cx = x; cy = y;
        break;
      }
      case 'T': {
        if (!num(2)) return false;
        double ox = rel ? cx : 0, oy = rel ? cy : 0;
        double qx = cx, qy = cy;
        if (prev == 'Q' || prev == 'T') { qx = 2 * cx - lqx; qy = 2 * cy - lqy; }
        double x = v[0] + ox, y = v[1] + oy;
        AddCubic(out, cx + 2.0 / 3.0 * (qx - cx), cy + 2.0 / 3.0 * (qy - cy),
                 x + 2.0 / 3.0 * (qx - x), y + 2.0 / 3.0 * (qy - y), x, y);
        lqx = qx; lqy = qy; cx = x; cy = y;
        break;
      }
      case 'A': {
        double rx, ry, rot, x, y; int fa, fs;
        if (!sc.ReadNumber(&rx) || !sc.ReadNumber(&ry) || !sc.ReadNumber(&rot)) return false;
        if (!sc.ReadFlag(&fa) || !sc.ReadFlag(&fs)) return false;
        if (!sc.ReadNumber(&x) || !sc.ReadNumber(&y)) return false;
        if (rel) { x += cx; y += cy; }
        AddArc(out, cx, cy, rx, ry, rot, fa, fs, x, y);
        cx = x; cy = y;
        break;
      }
      default:
        return false;
    }
    prev = up;
  }
  return true;
}

}  // namespace modernui
