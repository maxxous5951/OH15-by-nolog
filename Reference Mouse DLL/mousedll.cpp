//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: mousedll.cpp : the mouse-DLL, with two modes in one DLL.
//
//   Fast  (default): the original reference behaviour (instant jump + click).
//   Human: the cursor moves along a curved path (cubic Bezier) with a
//          bell-shaped speed profile, a duration based on Fitts' law,
//          a low-pass-filtered hand tremor, an occasional overshoot with a
//          correction, a short pause before the click and a variable
//          click duration. If the user moves the mouse, we wait until it rests.
//
//   The mode and all parameters are sent by OpenHoldem:
//   ProcessMessage("mouse_settings", &MouseSettings).
//
//******************************************************************************

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WINVER
#define WINVER 0x0601
#endif

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#ifndef _WIN32_WINDOWS
#define _WIN32_WINDOWS 0x0410
#endif

#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif

#include <windows.h>
#include <math.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <random>
#include "mousedll.h"

#pragma comment(lib, "winmm.lib")   // timeBeginPeriod()

namespace {

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

MouseSettings DefaultSettings() {
  MouseSettings s;
  memset(&s, 0, sizeof(s));
  s.size = sizeof(MouseSettings);
  s.mode = kMouseModeFast;
  s.speed_percent = kMouseDefaultSpeedPercent;
  s.curvature_percent = kMouseDefaultCurvaturePercent;
  s.noise_percent = kMouseDefaultNoisePercent;
  s.overshoot_percent = kMouseDefaultOvershootPercent;
  s.pause_min_ms = kMouseDefaultPauseMinMs;
  s.pause_max_ms = kMouseDefaultPauseMaxMs;
  s.hold_min_ms = kMouseDefaultHoldMinMs;
  s.hold_max_ms = kMouseDefaultHoldMaxMs;
  s.yield_to_user = kMouseDefaultYieldToUser;
  s.user_timeout_s = kMouseDefaultUserTimeoutS;
  s.user_settle_ms = kMouseDefaultUserSettleMs;
  return s;
}

int Clamp(int value, int lo, int hi) { return std::max(lo, std::min(hi, value)); }

void Sanitize(MouseSettings* s) {
  s->size = sizeof(MouseSettings);
  s->mode = (s->mode == kMouseModeHuman) ? kMouseModeHuman : kMouseModeFast;
  s->speed_percent = Clamp(s->speed_percent, 20, 500);
  s->curvature_percent = Clamp(s->curvature_percent, 0, 300);
  s->noise_percent = Clamp(s->noise_percent, 0, 300);
  s->overshoot_percent = Clamp(s->overshoot_percent, 0, 100);
  s->pause_min_ms = Clamp(s->pause_min_ms, 0, 2000);
  s->pause_max_ms = Clamp(s->pause_max_ms, 0, 2000);
  if (s->pause_max_ms < s->pause_min_ms) s->pause_max_ms = s->pause_min_ms;
  s->hold_min_ms = Clamp(s->hold_min_ms, 10, 500);
  s->hold_max_ms = Clamp(s->hold_max_ms, 10, 500);
  if (s->hold_max_ms < s->hold_min_ms) s->hold_max_ms = s->hold_min_ms;
  s->yield_to_user = s->yield_to_user ? 1 : 0;
  s->user_timeout_s = Clamp(s->user_timeout_s, 1, 120);
  s->user_settle_ms = Clamp(s->user_settle_ms, 200, 5000);
}

std::mutex g_settings_mutex;
MouseSettings g_settings = DefaultSettings();

MouseSettings Snapshot() {
  std::lock_guard<std::mutex> lock(g_settings_mutex);
  return g_settings;
}

void StoreSettings(const MouseSettings* incoming) {
  if (incoming == NULL || incoming->size < (int)sizeof(int)) return;
  std::lock_guard<std::mutex> lock(g_settings_mutex);
  MouseSettings s = g_settings;   // fields missing in older senders keep their value
  memcpy(&s, incoming, std::min((size_t)incoming->size, sizeof(MouseSettings)));
  Sanitize(&s);
  g_settings = s;
}

// ---------------------------------------------------------------------------
// Random numbers (properly seeded, unlike rand())
// ---------------------------------------------------------------------------

std::mt19937& Rng() {
  static thread_local std::mt19937 rng = []() {
    std::random_device device;
    std::seed_seq seed{ device(), device(), (unsigned)GetTickCount(), (unsigned)GetCurrentThreadId() };
    return std::mt19937(seed);
  }();
  return rng;
}

double Uniform(double lo, double hi) {
  return std::uniform_real_distribution<double>(lo, hi)(Rng());
}

double Gauss(double mean, double sd) {
  if (sd <= 0) return mean;
  return std::normal_distribution<double>(mean, sd)(Rng());
}

// Human reaction times are right-skewed: many short values, a few long ones.
int SkewedDelay(int lo, int hi) {
  if (hi <= lo) return lo;
  return lo + (int)((hi - lo) * pow(Uniform(0.0, 1.0), 1.7) + 0.5);
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

typedef std::chrono::steady_clock Clock;

double MillisecondsSince(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}

// Sleep() has a resolution of ~15 ms by default, which makes movements saccadic.
struct TimerResolution {
  TimerResolution() { timeBeginPeriod(1); }
  ~TimerResolution() { timeEndPeriod(1); }
};

// ---------------------------------------------------------------------------
// Low level input
// ---------------------------------------------------------------------------

void FocusWindow(HWND hwnd) {
  SetFocus(hwnd);
  SetForegroundWindow(hwnd);
  SetActiveWindow(hwnd);
}

// Absolute move on the whole virtual desktop (works with several monitors).
void SendAbsoluteMove(int x, int y) {
  const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int vw = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
  const int vh = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
  INPUT input;
  ZeroMemory(&input, sizeof(input));
  input.type = INPUT_MOUSE;
  // Centre of the pixel, so that Windows maps it back to exactly (x, y)
  input.mi.dx = (LONG)Clamp((int)(((x - vx) + 0.5) * 65536.0 / vw), 0, 65535);
  input.mi.dy = (LONG)Clamp((int)(((y - vy) + 0.5) * 65536.0 / vh), 0, 65535);
  input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  SendInput(1, &input, sizeof(INPUT));
}

// Button event at the current cursor position
void SendButton(MouseButton button, bool down) {
  INPUT input;
  ZeroMemory(&input, sizeof(input));
  input.type = INPUT_MOUSE;
  switch (button) {
    case MouseLeft:   input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN   : MOUSEEVENTF_LEFTUP;   break;
    case MouseMiddle: input.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    case MouseRight:  input.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN  : MOUSEEVENTF_RIGHTUP;  break;
  }
  SendInput(1, &input, sizeof(INPUT));
}

POINT ClampToDesktop(POINT p) {
  const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int vw = std::max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
  const int vh = std::max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
  p.x = Clamp(p.x, vx, vx + vw - 1);
  p.y = Clamp(p.y, vy, vy + vh - 1);
  return p;
}

bool IsNear(POINT a, POINT b, int tolerance) {
  return abs(a.x - b.x) <= tolerance && abs(a.y - b.y) <= tolerance;
}

// ---------------------------------------------------------------------------
// Human movement
// ---------------------------------------------------------------------------

struct SegmentParams {
  double duration_ms;
  double curvature;     // multiplier, 1 = normal
  double noise_px;      // standard deviation of the hand tremor
  bool   watch_user;    // stop if somebody else moves the mouse
};

enum SegmentResult { kSegmentDone, kSegmentInterrupted };

// One smooth movement from "from" to "to".
// "seen" is the position where the cursor was seen the last time;
// it is used to detect that the user moved the mouse.
SegmentResult RunSegment(POINT from, POINT to, const SegmentParams& params, POINT* seen) {
  const double dx = to.x - from.x;
  const double dy = to.y - from.y;
  const double dist = sqrt(dx * dx + dy * dy);
  const double ux = dist > 0 ? dx / dist : 0;      // direction
  const double uy = dist > 0 ? dy / dist : 0;
  const double nx = -uy, ny = ux;                  // normal

  // Cubic Bezier: the control points are pushed sideways, proportionally to
  // the distance (a long move bends more than a short one).
  double amplitude = dist * 0.12 * params.curvature * std::max(0.0, Gauss(1.0, 0.35));
  amplitude = std::min(amplitude, 220.0);
  const double side = Uniform(0, 1) < 0.5 ? -1.0 : 1.0;
  const double offset1 = side * amplitude * Uniform(0.4, 1.0);
  const double offset2 = side * amplitude * Uniform(-0.2, 0.8);
  const double along1 = dist * Uniform(0.20, 0.40);
  const double along2 = dist * Uniform(0.60, 0.85);
  const double p0x = from.x, p0y = from.y;
  const double p1x = from.x + ux * along1 + nx * offset1;
  const double p1y = from.y + uy * along1 + ny * offset1;
  const double p2x = from.x + ux * along2 + nx * offset2;
  const double p2y = from.y + uy * along2 + ny * offset2;
  const double p3x = to.x, p3y = to.y;

  // Low-pass filtered tremor (a slow wobble, not white noise)
  const double kFilter = 0.85;
  const double innovation = params.noise_px * sqrt(1.0 - kFilter * kFilter);
  double tremor_x = 0, tremor_y = 0;

  const Clock::time_point begin = Clock::now();
  POINT last = *seen;
  for (;;) {
    if (params.watch_user) {
      POINT current;
      GetCursorPos(&current);
      if (!IsNear(current, last, 3)) {
        *seen = current;
        return kSegmentInterrupted;
      }
    }
    const double t = std::min(1.0, MillisecondsSince(begin) / std::max(1.0, params.duration_ms));
    // Minimum-jerk profile (bell-shaped speed), warped a little so that the
    // peak speed comes slightly early: fast start, longer braking.
    const double w = pow(t, 0.9);
    const double e = w * w * w * (10.0 - 15.0 * w + 6.0 * w * w);
    const double m = 1.0 - e;
    const double b0 = m * m * m, b1 = 3 * m * m * e, b2 = 3 * m * e * e, b3 = e * e * e;
    double x = b0 * p0x + b1 * p1x + b2 * p2x + b3 * p3x;
    double y = b0 * p0y + b1 * p1y + b2 * p2y + b3 * p3y;
    tremor_x = kFilter * tremor_x + Gauss(0, innovation);
    tremor_y = kFilter * tremor_y + Gauss(0, innovation);
    const double envelope = sin(3.14159265358979 * t);   // no tremor at start and end
    x += tremor_x * envelope;
    y += tremor_y * envelope;
    POINT p;
    p.x = (LONG)floor(x + 0.5);
    p.y = (LONG)floor(y + 0.5);
    if (t >= 1.0) p = to;
    p = ClampToDesktop(p);
    if (p.x != last.x || p.y != last.y) {
      SendAbsoluteMove(p.x, p.y);
      GetCursorPos(&last);   // what Windows really did (DPI, rounding)
      *seen = last;
    }
    if (t >= 1.0) break;
    Sleep((DWORD)Uniform(6.0, 11.0));
  }
  return kSegmentDone;
}

// The user moved the mouse: wait until it rests for a while. false = timeout.
bool WaitForUserToStop(const MouseSettings& s, POINT* seen) {
  const Clock::time_point begin = Clock::now();
  POINT previous;
  GetCursorPos(&previous);
  Clock::time_point last_change = Clock::now();
  for (;;) {
    Sleep(30);
    POINT current;
    GetCursorPos(&current);
    if (!IsNear(current, previous, 1)) {
      previous = current;
      last_change = Clock::now();
    }
    if (MillisecondsSince(last_change) >= s.user_settle_ms) {
      *seen = current;
      return true;
    }
    if (MillisecondsSince(begin) >= s.user_timeout_s * 1000.0) return false;
  }
}

// Fitts' law: T = a + b * log2(D / W + 1), with some natural variation.
double FittsDuration(double distance, double target_size, const MouseSettings& s, double speed_multiplier) {
  const double width = std::max(8.0, target_size);
  const double index_of_difficulty = log2(distance / width + 1.0);
  double t = 90.0 + 110.0 * index_of_difficulty;
  t *= std::max(0.75, std::min(1.35, Gauss(1.0, 0.12)));
  t *= 100.0 / s.speed_percent;
  t /= std::max(0.1, speed_multiplier);
  return std::max(50.0, std::min(3000.0, t));
}

// Moves the cursor to "target" like a human would.
//   target_size:     size of the thing to hit (pixels), makes small targets slower
//   allow_overshoot: may shoot past the target and correct (long moves only)
//   abort_on_user:   if the user interferes, give up instead of waiting
//                    (used while a mouse button is held down)
// Returns false if the user didn't let us finish (timeout) or we gave up.
bool MoveCursorHuman(POINT target, int target_size, bool allow_overshoot,
                     double speed_multiplier, double curvature_multiplier,
                     double noise_multiplier, bool abort_on_user) {
  const MouseSettings s = Snapshot();
  TimerResolution timer;
  target = ClampToDesktop(target);

  POINT seen;
  GetCursorPos(&seen);
  const double initial_dx = target.x - seen.x;
  const double initial_dy = target.y - seen.y;
  const double initial_distance = sqrt(initial_dx * initial_dx + initial_dy * initial_dy);

  // Overshoot: shoot a little past the target, then correct. Only for long moves.
  bool overshoot_pending = false;
  POINT overshoot_point = target;
  if (allow_overshoot && initial_distance > 150.0 && Uniform(0, 100) < s.overshoot_percent) {
    const double amount = Uniform(4.0, std::max(5.0, std::min(18.0, initial_distance * 0.04)));
    const double ux = initial_dx / initial_distance, uy = initial_dy / initial_distance;
    const double sideways = Gauss(0, 2.0);
    overshoot_point.x = (LONG)floor(target.x + ux * amount - uy * sideways + 0.5);
    overshoot_point.y = (LONG)floor(target.y + uy * amount + ux * sideways + 0.5);
    overshoot_point = ClampToDesktop(overshoot_point);
    overshoot_pending = !IsNear(overshoot_point, target, 2);
  }

  bool correcting = false;
  for (int attempt = 0; attempt < 20; ++attempt) {
    POINT start;
    GetCursorPos(&start);
    if (!IsNear(start, seen, 3) && s.yield_to_user) {
      // Somebody moved the mouse between two segments
      if (abort_on_user) return false;
      if (!WaitForUserToStop(s, &seen)) return false;
      GetCursorPos(&start);
      overshoot_pending = false;
      correcting = false;
    }
    seen = start;
    const POINT aim = overshoot_pending ? overshoot_point : target;
    if (IsNear(start, aim, 0)) {
      if (overshoot_pending) { overshoot_pending = false; correcting = true; continue; }
      return true;
    }
    const double dx = aim.x - start.x, dy = aim.y - start.y;
    const double distance = sqrt(dx * dx + dy * dy);

    SegmentParams params;
    params.watch_user = s.yield_to_user != 0;
    if (correcting) {
      // Small correction after an overshoot: short, almost straight
      params.duration_ms = Uniform(90.0, 170.0) * 100.0 / s.speed_percent;
      params.curvature = 0.2 * curvature_multiplier * s.curvature_percent / 100.0;
      params.noise_px = 0.3 * noise_multiplier * s.noise_percent / 100.0;
    } else {
      params.duration_ms = FittsDuration(distance, (double)target_size, s, speed_multiplier);
      params.curvature = curvature_multiplier * s.curvature_percent / 100.0;
      params.noise_px = 1.0 * noise_multiplier * s.noise_percent / 100.0;
    }

    if (RunSegment(start, aim, params, &seen) == kSegmentDone) {
      if (overshoot_pending) {
        overshoot_pending = false;
        correcting = true;
        Sleep(SkewedDelay(40, 110));   // "oops" - the brief moment before correcting
      } else {
        return true;
      }
      continue;
    }
    // Interrupted by the user
    if (abort_on_user) return false;
    if (!WaitForUserToStop(s, &seen)) return false;
    overshoot_pending = false;
    correcting = false;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Human click and drag
// ---------------------------------------------------------------------------

int HumanClick(HWND hwnd, RECT rect, MouseButton button, int clicks) {
  const MouseSettings s = Snapshot();
  clicks = Clamp(clicks, 1, 50);
  POINT target = RandomizeClickLocation(rect);
  ClientToScreen(hwnd, &target);
  const int size = std::max(6, std::min((int)(rect.right - rect.left), (int)(rect.bottom - rect.top)));

  // The right window has to be in front before we arrive
  FocusWindow(hwnd);

  bool arrived = false;
  for (int attempt = 0; attempt < 3 && !arrived; ++attempt) {
    if (!MoveCursorHuman(target, size, true, 1.0, 1.0, 1.0, false)) return (int)false;
    Sleep(SkewedDelay(s.pause_min_ms, s.pause_max_ms));
    POINT current;
    GetCursorPos(&current);
    // Somebody may have moved the mouse during the pause
    arrived = IsNear(current, target, 3) || !s.yield_to_user;
  }
  if (!arrived) return (int)false;

  for (int i = 0; i < clicks; ++i) {
    SendButton(button, true);
    Sleep(SkewedDelay(s.hold_min_ms, s.hold_max_ms));
    SendButton(button, false);
    if (i < clicks - 1) Sleep(SkewedDelay(55, 120));
  }
  return (int)true;
}

int HumanDrag(HWND hwnd, RECT rect, bool is_horizontal_drag) {
  const MouseSettings s = Snapshot();
  POINT start, end;
  if (is_horizontal_drag) {
    // left to right, along the middle of the region
    const int y = rect.top + (rect.bottom - rect.top) / 2 + (int)floor(Gauss(0, 1.0) + 0.5);
    start.x = rect.left;
    end.x = rect.right;
    start.y = end.y = Clamp(y, (int)rect.top, (int)rect.bottom);
  } else {
    // bottom to top, along the middle of the region
    const int x = rect.left + (rect.right - rect.left) / 2 + (int)floor(Gauss(0, 1.0) + 0.5);
    start.y = rect.bottom;
    end.y = rect.top;
    start.x = end.x = Clamp(x, (int)rect.left, (int)rect.right);
  }
  ClientToScreen(hwnd, &start);
  ClientToScreen(hwnd, &end);

  FocusWindow(hwnd);
  if (!MoveCursorHuman(start, 12, true, 1.0, 1.0, 1.0, false)) return (int)false;
  Sleep(SkewedDelay(s.pause_min_ms, s.pause_max_ms));

  SendButton(MouseLeft, true);
  Sleep(SkewedDelay(40, 90));
  // While the button is down we don't wait for the user, we give up (and release)
  const bool ok = MoveCursorHuman(end, 12, false, 0.8, 0.35, 0.6, true);
  Sleep(SkewedDelay(30, 80));
  SendButton(MouseLeft, false);   // always release the button
  return (int)ok;
}

// ---------------------------------------------------------------------------
// Fast mode: the original reference implementation, unchanged
// ---------------------------------------------------------------------------

int FastClick(const HWND hwnd, const RECT rect, const MouseButton button, const int clicks) {
  INPUT input[100] = { 0 };
  const int number_of_clicks = Clamp(clicks, 1, 50);

  POINT pt = RandomizeClickLocation(rect);
  double fScreenWidth = ::GetSystemMetrics(SM_CXSCREEN) - 1;
  double fScreenHeight = ::GetSystemMetrics(SM_CYSCREEN) - 1;

  // Translate click point to screen/mouse coords
  ClientToScreen(hwnd, &pt);
  double fx = pt.x * (65535.0f / fScreenWidth);
  double fy = pt.y * (65535.0f / fScreenHeight);

  // Set up the input structure
  for (int i = 0; i < number_of_clicks * 2; i += 2) {
    ZeroMemory(&input[i], sizeof(INPUT));
    input[i].type = INPUT_MOUSE;
    input[i].mi.dx = (LONG)fx;
    input[i].mi.dy = (LONG)fy;
    switch (button) {
      case MouseLeft:
        input[i].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN;
        break;
      case MouseMiddle:
        input[i].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_MIDDLEDOWN;
        break;
      case MouseRight:
        input[i].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_RIGHTDOWN;
        break;
    }
    ZeroMemory(&input[i + 1], sizeof(INPUT));
    input[i + 1].type = INPUT_MOUSE;
    input[i + 1].mi.dx = (LONG)fx;
    input[i + 1].mi.dy = (LONG)fy;
    switch (button) {
      case MouseLeft:
        input[i + 1].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTUP;
        break;
      case MouseMiddle:
        input[i + 1].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_MIDDLEUP;
        break;
      case MouseRight:
        input[i + 1].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_RIGHTUP;
        break;
    }
  }

  // Set focus to target window
  FocusWindow(hwnd);

  // Send input
  Sleep(100);
  SendInput(number_of_clicks * 2, input, sizeof(INPUT));
  Sleep(100);
  return (int)true;
}

int FastDrag(const HWND hwnd, const RECT rect, bool is_horizontal_drag) {
  INPUT input[3];
  POINT pt;
  double fx, fy;

  double fScreenWidth = ::GetSystemMetrics(SM_CXSCREEN) - 1;
  double fScreenHeight = ::GetSystemMetrics(SM_CYSCREEN) - 1;

  if (is_horizontal_drag) {
    // left click, drag to right, un-left click
    pt.x = rect.left;
    pt.y = rect.top + (rect.bottom - rect.top) / 2;
    ClientToScreen(hwnd, &pt);
    fx = pt.x * (65535.0f / fScreenWidth);
    fy = pt.y * (65535.0f / fScreenHeight);

    ZeroMemory(&input[0], sizeof(INPUT));
    input[0].type = INPUT_MOUSE;
    input[0].mi.dx = (LONG)fx;
    input[0].mi.dy = (LONG)fy;
    input[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN;

    pt.x = rect.right;
    pt.y = rect.top + (rect.bottom - rect.top) / 2;
    ClientToScreen(hwnd, &pt);
    fx = pt.x * (65535.0f / fScreenWidth);
    fy = pt.y * (65535.0f / fScreenHeight);

    ZeroMemory(&input[1], sizeof(INPUT));
    input[1].type = INPUT_MOUSE;
    input[1].mi.dx = (LONG)fx;
    input[1].mi.dy = (LONG)fy;
    input[1].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
  } else {
    // left click, drag to up, un-left click
    pt.x = rect.left + (rect.right - rect.left) / 2;
    pt.y = rect.bottom;
    ClientToScreen(hwnd, &pt);
    fx = pt.x * (65535.0f / fScreenWidth);
    fy = pt.y * (65535.0f / fScreenHeight);

    ZeroMemory(&input[0], sizeof(INPUT));
    input[0].type = INPUT_MOUSE;
    input[0].mi.dx = (LONG)fx;
    input[0].mi.dy = (LONG)fy;
    input[0].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN;

    pt.x = rect.left + (rect.right - rect.left) / 2;
    pt.y = rect.top;
    ClientToScreen(hwnd, &pt);
    fx = pt.x * (65535.0f / fScreenWidth);
    fy = pt.y * (65535.0f / fScreenHeight);

    ZeroMemory(&input[1], sizeof(INPUT));
    input[1].type = INPUT_MOUSE;
    input[1].mi.dx = (LONG)fx;
    input[1].mi.dy = (LONG)fy;
    input[1].mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
  }

  ZeroMemory(&input[2], sizeof(INPUT));
  input[2].type = INPUT_MOUSE;
  input[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;

  // Set focus to target window
  FocusWindow(hwnd);
  // Send input
  SendInput(3, input, sizeof(INPUT));
  return (int)true;
}

}  // namespace

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

MOUSEDLL_API int MouseClick(const HWND hwnd, const RECT rect, const MouseButton button, const int clicks) {
  if (Snapshot().mode == kMouseModeHuman) {
    return HumanClick(hwnd, rect, button, clicks);
  }
  return FastClick(hwnd, rect, button, clicks);
}

MOUSEDLL_API int MouseClickDrag(const HWND hwnd, const RECT rect, bool is_horizontal_drag) {
  if (Snapshot().mode == kMouseModeHuman) {
    return HumanDrag(hwnd, rect, is_horizontal_drag);
  }
  return FastDrag(hwnd, rect, is_horizontal_drag);
}

MOUSEDLL_API void ProcessMessage(const char *message, const void *param) {
  if (message == NULL) return;
  if (strcmp(message, "mouse_settings") == 0) {
    StoreSettings(static_cast<const MouseSettings*>(param));
  } else if (strcmp(message, "mouse_move_to") == 0) {
    if (param == NULL) return;
    const POINT target = *static_cast<const POINT*>(param);
    if (Snapshot().mode == kMouseModeHuman) {
      MoveCursorHuman(target, 24, true, 1.0, 1.0, 1.0, false);
    } else {
      SetCursorPos(target.x, target.y);
    }
  }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
  switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
      break;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Click location: normal distribution around the centre of the region
// ---------------------------------------------------------------------------

const POINT RandomizeClickLocation(const RECT rect) {
  POINT p = { 0 };
  // normal random distribution - much better than uniform!
  GetClickPoint(rect.left + (rect.right - rect.left) / 2,
                rect.top + (rect.bottom - rect.top) / 2,
                (rect.right - rect.left) / 2,
                (rect.bottom - rect.top) / 2,
                &p);
  return p;
}

const void GetClickPoint(const int x, const int y, const int rx, const int ry, POINT *p) {
  p->x = x + (int)(RandomNormalScaled(2 * rx, 0, 1) + 0.5) - (rx);
  p->y = y + (int)(RandomNormalScaled(2 * ry, 0, 1) + 0.5) - (ry);
}

// random number - 0 -> scale, with normal distribution
// ignore results outside 3.5 stds from the mean
const double RandomNormalScaled(const double scale, const double m, const double s) {
  double res = -99;
  while (res < -3.5 || res > 3.5) res = RandomNormal(m, s);
  return (res / 3.5 * s + 1) * (scale / 2.0);
}

const double RandomNormal(const double m, const double s) {
  return Gauss(m, s);
}
