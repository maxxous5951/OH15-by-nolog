"""Backend registry. Backends load lazily so a missing platform never breaks imports."""

from __future__ import annotations

import sys

from openreplayshooter.backends.base import CaptureBackend
from openreplayshooter.model import BackendUnavailableError

AVAILABLE = ("printwindow", "synthetic")


def default_backend_name() -> str:
    return "printwindow" if sys.platform == "win32" else "synthetic"


def get_backend(name: str) -> CaptureBackend:
    if name == "synthetic":
        from openreplayshooter.backends.synthetic import SyntheticBackend

        return SyntheticBackend()
    if name == "printwindow":
        if sys.platform != "win32":
            raise BackendUnavailableError("The 'printwindow' backend only runs on Windows.")
        from openreplayshooter.backends.win32_printwindow import PrintWindowBackend

        return PrintWindowBackend()
    raise BackendUnavailableError(f"Unknown backend '{name}'. Choose from: {', '.join(AVAILABLE)}.")
