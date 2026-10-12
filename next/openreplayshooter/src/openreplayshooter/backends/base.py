"""The contract every capture backend fulfils."""

from __future__ import annotations

from typing import Protocol

from PIL import Image

from openreplayshooter.model import WindowInfo


class CaptureBackend(Protocol):
    """Finds windows and grabs their client area as an image."""

    name: str

    def list_windows(self) -> list[WindowInfo]:
        """Visible, titled top-level windows the backend can capture."""
        ...

    def is_alive(self, window: WindowInfo) -> bool:
        """False once the window has been closed or hidden for good."""
        ...

    def grab(self, window: WindowInfo) -> Image.Image:
        """Capture the window's client area (no title bar, no borders)."""
        ...
