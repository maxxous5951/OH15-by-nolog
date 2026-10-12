"""A fake window that renders numbered test frames.

Works on every platform. Handy to check the pipeline end to end (output folder,
OHReplay compatibility, resuming) without any real window, and as the backend
used by the test suite.
"""

from __future__ import annotations

from PIL import Image, ImageDraw

from openreplayshooter.model import WindowInfo

SYNTHETIC_WINDOW = WindowInfo(handle=1, title="Synthetic Table")


class SyntheticBackend:
    name = "synthetic"

    def __init__(self, size: tuple[int, int] = (320, 200)) -> None:
        self.size = size
        self.grabs = 0
        self.closed = False

    def list_windows(self) -> list[WindowInfo]:
        return [] if self.closed else [SYNTHETIC_WINDOW]

    def is_alive(self, window: WindowInfo) -> bool:
        return not self.closed and window.handle == SYNTHETIC_WINDOW.handle

    def grab(self, window: WindowInfo) -> Image.Image:
        self.grabs += 1
        shade = (self.grabs * 23) % 200 + 30
        image = Image.new("RGB", self.size, (20, shade, 60))
        ImageDraw.Draw(image).text((10, 10), f"grab #{self.grabs}", fill=(255, 255, 255))
        return image
