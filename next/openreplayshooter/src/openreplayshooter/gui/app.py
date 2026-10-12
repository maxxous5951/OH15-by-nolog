"""Entry point of the graphical interface."""

from __future__ import annotations

import argparse
import sys
from collections.abc import Sequence

from openreplayshooter.backends import AVAILABLE, default_backend_name, get_backend
from openreplayshooter.model import CaptureError


def run(backend_name: str | None = None) -> int:
    """Open the window and run the Qt event loop until it is closed."""
    try:
        from PySide6.QtWidgets import QApplication

        from openreplayshooter.gui.main_window import MainWindow
    except ImportError as error:
        raise CaptureError(
            'The graphical interface needs PySide6: pip install "openreplayshooter[gui]"'
        ) from error

    app = QApplication.instance() or QApplication(sys.argv[:1])
    app.setOrganizationName("OpenHoldem")
    app.setApplicationName("OpenReplayShooter")
    backend = get_backend(backend_name or default_backend_name())
    window = MainWindow(backend)
    window.show()
    return app.exec()


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="openreplayshooter-gui")
    parser.add_argument("--backend", choices=AVAILABLE, default=default_backend_name())
    args = parser.parse_args(argv)
    try:
        return run(args.backend)
    except CaptureError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
