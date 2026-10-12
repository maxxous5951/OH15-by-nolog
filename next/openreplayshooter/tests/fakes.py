"""Test doubles shared by the suites."""

from __future__ import annotations

from PIL import Image

from openreplayshooter.model import WindowInfo


class ScriptedBackend:
    """Backend with a fixed window list and optional failure scripting."""

    name = "scripted"

    def __init__(
        self,
        windows: list[WindowInfo],
        *,
        close_after: int | None = None,
        interrupt_at: int | None = None,
    ) -> None:
        self.windows = windows
        self.close_after = close_after
        self.interrupt_at = interrupt_at
        self.grabs = 0

    def list_windows(self) -> list[WindowInfo]:
        return list(self.windows)

    def is_alive(self, window: WindowInfo) -> bool:
        return self.close_after is None or self.grabs < self.close_after

    def grab(self, window: WindowInfo) -> Image.Image:
        self.grabs += 1
        if self.interrupt_at is not None and self.grabs == self.interrupt_at:
            raise KeyboardInterrupt
        return Image.new("RGB", (40, 30), (self.grabs % 256, 0, 0))
