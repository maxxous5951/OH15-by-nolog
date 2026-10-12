# OpenReplayShooter (rewrite)

Captures a window at a fixed interval and writes numbered frames that **OHReplay** and
**OpenScrape** already understand. It replaces the old WinForms tool in
`OpenReplayShooter/`, and also produces clean datasets for the future vision/AI work.

## Install

Python 3.11+ (developed on 3.13).

```
pip install -e ".[gui,dev]"      # gui = PySide6 (Qt 6), dev = pytest, ruff
```

The command line works without the `gui` extra.

## Graphical interface

```
openreplayshooter gui            # or: openreplayshooter-gui
```

Pick the window, the output folder and the settings, then press **Start**. The interface
shows progress and a live preview of the last frame, locks the settings while capturing,
and **Stop** ends the capture cleanly (frames already written are kept). Your choices are
remembered between runs. Use `--backend synthetic` to try it without any real window.

## Command line

```
openreplayshooter list
openreplayshooter shoot -w "Table 1" -o C:\TMP\hand1 -i 1000 -n 200
```

`-w` takes a window handle (`0x1A2B` or decimal) or a part of the title, which must be
unique. Stop with Ctrl+C: the frames already written are kept.

| Option | Default | Meaning |
|---|---|---|
| `-i, --interval-ms` | 1000 | Delay between frames. Deadlines are absolute, so it does not drift. |
| `-n, --max-frames` | 1000 | Highest frame index + 1 (same meaning as the old "Max. output files"). |
| `--format bmp\|png` | bmp | OHReplay only reads `bmp`. |
| `--no-legacy-html` | off | Skip the `.htm` file OHReplay uses for the window title. |
| `--metadata` | off | Also write `frameNNNNNN.json` (title, size, UTC timestamp). |
| `--backend` | `printwindow` on Windows | `synthetic` renders test frames without any window. |

Resuming: if the folder already holds frames `0..k-1`, capture continues at `k`.
A frame counts as complete once its `.htm` exists (written after the image), so an
interrupted write is simply redone.

Exit codes: `0` ok, `2` usage error, `3` the window closed during capture, `4` folder full.

## Output format

Unchanged from the old tool, so existing workflows keep working:

```
frame000000.bmp   24-bit uncompressed bitmap of the window's client area
frame000000.htm   line 1 = window title, then a tiny HTML wrapper
```

## What changed from the old tool

- Client area captured with `PrintWindow(PW_CLIENTONLY | PW_RENDERFULLCONTENT)` instead of
  `BitBlt` plus border/title-bar arithmetic, and the process is DPI aware.
- The window is only restored when it is minimized (the old tool forced "restore" on every
  frame, which also un-maximized windows).
- Stopping is cooperative (`threading.Event`) instead of `Thread.Abort`, which no longer
  exists in modern .NET.
- Files are written atomically; cloaked (hidden UWP) windows are not listed.

## Known limits

- The Windows backend is **not yet validated on a real Windows machine**: development
  happened on Linux, where only the `synthetic` backend runs. First check on Windows:
  `openreplayshooter list`, then `openreplayshooter shoot -w <window> -o out -n 3`.
- `PrintWindow` can return a black image for windows drawn directly by the GPU. A
  Windows Graphics Capture backend is the planned fix; the backend interface
  (`backends/base.py`) is designed so it plugs in without touching the rest.
- The interface was tested with Qt's offscreen platform and screenshots, not yet on a real
  Windows desktop.

## Develop

```
pytest                       # GUI tests run offscreen; skipped if PySide6 is missing
ruff check . && ruff format --check .
```

Licensed under GPL v3, like OpenHoldem.
