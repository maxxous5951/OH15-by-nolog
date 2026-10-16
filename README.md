# OpenHoldem 15 (OH15 by nolog)

A fork of [OpenHoldem 14.0.2](https://github.com/OpenHoldem/openholdembot) with a modernised user interface,
an **Advisor mode**, a configurable **human mouse**, extended hand-history tooling and a Spin & Go simulator.

> Branch `advisor-mode` is the main development branch of this fork.

OpenHoldem is licensed under the GPL v3. The original project, documentation and forums:
<https://github.com/OpenHoldem/openholdembot> / <http://www.maxinmontreal.com/forums/index.php>

**Use it responsibly.** Automating real-money play is forbidden by the terms of most online poker sites.
Use bots on play-money tables, for research and for testing your own profiles, and respect the rules of the
site you play on.

---

## What is new compared to OpenHoldem 14

### User interface ("UI 2026")
- Modern, frameless main window with light, dark and system themes, custom menus, toolbar, status indicators
  and a GDI+ table view (`ModernUI`).
- **One preferences dialog** with themed pages, replacing the ~20 property pages of OH14.
- Improved **formula editor**: theme support, find/replace bar, auto-completion, horizontal scrolling and
  word wrap (Alt+Z).
- Modern hand-list editor and dialogs (About, Problem Solver).
- Settings are stored in **ini files** instead of the Windows registry.
- Version 15.0, built with Visual Studio 2022 (toolset v143).

### Advisor mode
- A small window follows the table and shows the best action according to your profile, next to the action
  that was actually played. It can be docked in any corner of the table.
- It uses the real autoplayer pipeline, including the "as if played" logging, but every click is simulated.

### Mouse: fast and human in one DLL
- A single `mouse.dll` contains both mice:
  - **Fast** (default): the original reference behaviour, instant jump and click.
  - **Human**: curved path (cubic Bezier), bell-shaped speed profile, duration based on Fitts' law,
    low-pass filtered hand tremor, optional overshoot with correction, variable pause before the click and
    variable click duration, and it waits if you move the mouse yourself.
- New **Mouse** page in the preferences with every parameter and a "Reset to defaults" button.
- After an action the cursor can stay where it is, return to the *neighbourhood* of its old position
  (never the same pixel, the default in human mode) or return exactly (the old behaviour).
- Settings are sent to the DLL with `ProcessMessage("mouse_settings", &MouseSettings)`; see `mousedll.h`.
- Fixes in the reference DLL: `MouseClickDrag` signature, a properly seeded random generator, multi-monitor
  support and bounded buffers.

### Window capture and privacy
- Optional **PrintWindow capture** in OpenHoldem, OpenScrape and OpenReplayShooter, which also works when the
  poker table is covered by other windows.
- **Screen-capture protection** (`SetWindowDisplayAffinity`): hides OpenHoldem windows from screenshots and
  screen sharing without hiding them from you.

### Hand history
- Extended hand-history generator: tournaments, folds deduced from stacks, reconstructed betting rounds,
  multiway pots with late stacks, and fixes for heads-up hands with instant folds.
- Optional **action profile**: every bot action is logged together with the formula line that triggered it.
- Analysis tools: profit/loss per formula line (scripts) and a Qt6 GUI for the statistics.
- Hand-history replay tests in `RegressionTests`.



### OCR
- Bundled OCR libraries (Tesseract, Leptonica, OpenCV) and `tessdata`, as a base for OCR-based table reading.



## Building

1. Install Visual Studio 2022 with the *Desktop development with C++* workload and the MFC component.
2. Open the OpenHoldem solution and build `Release | Win32`.
3. `mouse.dll` is built by the **Reference Mouse DLL** project and copied next to `OpenHoldem.exe`.

## Choosing the mouse

`Preferences -> Mouse`

| Setting | Meaning |
| --- | --- |
| Mode | Fast (default) or Human. The settings below only apply to the human mouse |
| Speed, curvature, hand tremor | 100 % is the default; lower or higher values change the movement |
| Overshoot probability | Chance to overshoot the target on long moves |
| Pause before the click / click duration | Random values between a minimum and a maximum |
| If you move the mouse | Wait until it rests, give up after a timeout |
| After an action | Leave the mouse, go back to the neighbourhood, or go back exactly. Needs *Restore mouse position and window focus* on the Autoplayer page |

