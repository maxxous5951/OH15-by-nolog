"""Command line interface: ``openreplayshooter list`` and ``openreplayshooter shoot``."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence
from pathlib import Path

from openreplayshooter import __version__
from openreplayshooter.backends import AVAILABLE, default_backend_name, get_backend
from openreplayshooter.backends.base import CaptureBackend
from openreplayshooter.model import (
    BackendUnavailableError,
    CaptureError,
    FolderFullError,
    StopReason,
    WindowInfo,
)
from openreplayshooter.session import CaptureSession
from openreplayshooter.writer import FrameWriter

EXIT_OK = 0
EXIT_USAGE = 2
EXIT_WINDOW_CLOSED = 3
EXIT_FOLDER_FULL = 4


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="openreplayshooter",
        description="Capture a window at a fixed interval into frames for OHReplay/OpenScrape.",
    )
    parser.add_argument("--version", action="version", version=f"%(prog)s {__version__}")
    sub = parser.add_subparsers(dest="command", required=True)

    def add_backend(p: argparse.ArgumentParser) -> None:
        p.add_argument(
            "--backend",
            choices=AVAILABLE,
            default=default_backend_name(),
            help="capture backend (default: %(default)s)",
        )

    list_p = sub.add_parser("list", help="list capturable windows")
    add_backend(list_p)

    gui_p = sub.add_parser("gui", help="open the graphical interface (needs the 'gui' extra)")
    add_backend(gui_p)

    shoot = sub.add_parser("shoot", help="capture a window into a folder")
    add_backend(shoot)
    shoot.add_argument(
        "-w",
        "--window",
        required=True,
        help="window handle (0x1A2B or decimal) or a unique part of its title",
    )
    shoot.add_argument("-o", "--output", type=Path, required=True, help="output folder")
    shoot.add_argument(
        "-i", "--interval-ms", type=int, default=1000, help="delay between frames (default: 1000)"
    )
    shoot.add_argument(
        "-n",
        "--max-frames",
        type=int,
        default=1000,
        help="highest frame index + 1; existing frames are kept and resumed (default: 1000)",
    )
    shoot.add_argument(
        "--format",
        choices=("bmp", "png"),
        default="bmp",
        help="image format; OHReplay only reads bmp (default: bmp)",
    )
    shoot.add_argument(
        "--no-legacy-html",
        action="store_true",
        help="do not write the .htm file OHReplay uses for the window title",
    )
    shoot.add_argument(
        "--metadata", action="store_true", help="also write a frameNNNNNN.json per frame"
    )
    return parser


def resolve_window(backend: CaptureBackend, query: str) -> WindowInfo:
    """Pick one window from a handle or a title fragment, or explain why not."""
    windows = backend.list_windows()
    try:
        handle = int(query, 0)
    except ValueError:
        handle = None
    if handle is not None:
        for window in windows:
            if window.handle == handle:
                return window
        raise CaptureError(f"No capturable window with handle {query}. Try 'list'.")

    needle = query.casefold()
    matches = [w for w in windows if needle in w.title.casefold()]
    if len(matches) == 1:
        return matches[0]
    if not matches:
        raise CaptureError(f"No window title contains '{query}'. Try 'list'.")
    listing = "\n".join(f"  {w}" for w in matches)
    raise CaptureError(f"'{query}' matches {len(matches)} windows, be more precise:\n{listing}")


def _cmd_list(args: argparse.Namespace) -> int:
    backend = get_backend(args.backend)
    for window in backend.list_windows():
        print(window)
    return EXIT_OK


def _cmd_gui(args: argparse.Namespace) -> int:
    from openreplayshooter.gui.app import run  # importing it never needs PySide6

    return run(args.backend)


def _cmd_shoot(args: argparse.Namespace) -> int:
    if args.interval_ms < 0 or args.max_frames < 1:
        raise CaptureError("--interval-ms must be >= 0 and --max-frames >= 1.")
    backend = get_backend(args.backend)
    window = resolve_window(backend, args.window)
    writer = FrameWriter(
        args.output,
        image_format=args.format,
        legacy_html=not args.no_legacy_html,
        metadata=args.metadata,
    )
    session = CaptureSession(
        backend, window, writer, interval_ms=args.interval_ms, max_frames=args.max_frames
    )
    print(f"Capturing {window} -> {args.output}  (Ctrl+C to stop)")
    result = session.run(on_frame=lambda index, path: print(f"  {path.name}", flush=True))
    print(
        f"{result.frames_written} frame(s) written "
        f"(from index {result.first_index}), stopped: {result.reason}"
    )
    return EXIT_WINDOW_CLOSED if result.reason is StopReason.WINDOW_CLOSED else EXIT_OK


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        commands = {"list": _cmd_list, "shoot": _cmd_shoot, "gui": _cmd_gui}
        return commands[args.command](args)
    except FolderFullError as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_FOLDER_FULL
    except (BackendUnavailableError, CaptureError) as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_USAGE


if __name__ == "__main__":
    raise SystemExit(main())
