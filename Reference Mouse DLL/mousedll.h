//******************************************************************************
//
// This file is part of the OpenHoldem project
//    Source code:           https://github.com/OpenHoldem/openholdembot/
//    Forums:                http://www.maxinmontreal.com/forums/index.php
//    Licensed under GPL v3: http://www.gnu.org/licenses/gpl.html
//
//******************************************************************************
//
// Purpose: Mouse-DLL with two modes in one single DLL:
//   * Fast  (default): the reference behaviour, instant jump to the target.
//   * Human: curved trajectory, variable speed, variable click timing.
//   The mode and all parameters are sent by OpenHoldem with
//   ProcessMessage("mouse_settings", &MouseSettings).
//
//******************************************************************************

#ifndef INC_MOUSEDLL_H
#define INC_MOUSEDLL_H

#ifdef MOUSEDLL_EXPORTS
#define MOUSEDLL_API __declspec(dllexport)
#else
#define MOUSEDLL_API __declspec(dllimport)
#endif

enum MouseButton { MouseLeft, MouseMiddle, MouseRight };

enum MouseMode { kMouseModeFast = 0, kMouseModeHuman = 1 };

// Sent with ProcessMessage("mouse_settings", &settings).
// "size" must be set to sizeof(MouseSettings), so that old and new versions
// of OpenHoldem and of the DLL stay compatible. All values are plain ints.
struct MouseSettings {
  int size;
  int mode;               // MouseMode
  int speed_percent;      // 100 = normal, 200 = twice as fast        (20..500)
  int curvature_percent;  // 0 = (almost) straight, 100 = normal      (0..300)
  int noise_percent;      // hand tremor, 0 = none, 100 = normal      (0..300)
  int overshoot_percent;  // probability to overshoot the target      (0..100)
  int pause_min_ms;       // pause between arrival and click          (0..2000)
  int pause_max_ms;
  int hold_min_ms;        // duration of the click (down -> up)       (10..500)
  int hold_max_ms;
  int yield_to_user;      // 1 = wait if the user moves the mouse
  int user_timeout_s;     // give up after that many seconds          (1..120)
  int user_settle_ms;     // mouse has to rest that long afterwards   (200..5000)
  int after_action;       // what to do with the cursor after an action sequence:
                          //   0 = leave it where it is
                          //   1 = go back to the neighbourhood of the old position (never the same pixel)
                          //   2 = go back to exactly the old position
  int return_radius_px;   // size of that neighbourhood               (20..600)
};

// Default values of the human mode (also used by the preferences)
const int kMouseDefaultSpeedPercent = 100;
const int kMouseDefaultCurvaturePercent = 100;
const int kMouseDefaultNoisePercent = 100;
const int kMouseDefaultOvershootPercent = 15;
const int kMouseDefaultPauseMinMs = 40;
const int kMouseDefaultPauseMaxMs = 150;
const int kMouseDefaultHoldMinMs = 50;
const int kMouseDefaultHoldMaxMs = 130;
const int kMouseDefaultYieldToUser = 1;
const int kMouseDefaultUserTimeoutS = 10;
const int kMouseDefaultUserSettleMs = 1000;
const int kMouseDefaultAfterAction = 1;
const int kMouseDefaultReturnRadiusPx = 120;

typedef int(*mouse_click_t)(const HWND hwnd, const RECT rect, const MouseButton button, const int clicks);
MOUSEDLL_API int MouseClick(const HWND hwnd, const RECT rect, const MouseButton button, const int clicks);

typedef int(*mouse_clickdrag_t)(const HWND hwnd, const RECT rect, bool is_horizontal_drag);
MOUSEDLL_API int MouseClickDrag(const HWND hwnd, const RECT rect, bool is_horizontal_drag);

// Messages:
//   "mouse_settings"  param = const MouseSettings*
//   "mouse_move_to"   param = const POINT* (screen coordinates),
//                     moves the cursor (humanly in human mode).
//   "mouse_rest"      param = const POINT* (position before the action sequence).
//                     Human mode: waits a moment, then moves the cursor according to
//                     MouseSettings.after_action (never back to the exact same pixel
//                     unless explicitly requested). Fast mode: SetCursorPos().
typedef void (*mouse_process_message_t)(const char *message, const void *param);
MOUSEDLL_API void ProcessMessage(const char *message, const void *param);

const POINT RandomizeClickLocation(const RECT rect);
const void GetClickPoint(const int x, const int y, const int rx, const int ry, POINT *p);
const double RandomNormalScaled(const double scale, const double m, const double s);
const double RandomNormal(const double m, const double s);

#endif //INC_MOUSEDLL_H
