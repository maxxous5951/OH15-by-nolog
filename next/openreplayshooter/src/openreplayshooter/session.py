"""The capture loop."""

from __future__ import annotations

import threading
import time
from collections.abc import Callable
from pathlib import Path

from openreplayshooter.backends.base import CaptureBackend
from openreplayshooter.model import (
    FolderFullError,
    SessionResult,
    StopReason,
    WindowInfo,
)
from openreplayshooter.writer import FrameWriter

FrameCallback = Callable[[int, Path], None]


class CaptureSession:
    """Capture ``window`` every ``interval_ms`` until a limit or a stop request.

    ``max_frames`` caps the frame *index*, like the old tool's "Max. output files":
    a folder already holding frames 0..N-1 resumes at N and stops at ``max_frames``.
    """

    def __init__(
        self,
        backend: CaptureBackend,
        window: WindowInfo,
        writer: FrameWriter,
        *,
        interval_ms: int = 1000,
        max_frames: int = 1000,
    ) -> None:
        if interval_ms < 0:
            raise ValueError("interval_ms must be >= 0")
        if max_frames < 1:
            raise ValueError("max_frames must be >= 1")
        self.backend = backend
        self.window = window
        self.writer = writer
        self.interval = interval_ms / 1000
        self.max_frames = max_frames
        self._stop = threading.Event()

    def stop(self) -> None:
        """Ask the loop to end. Safe to call from another thread or a signal handler."""
        self._stop.set()

    def run(self, on_frame: FrameCallback | None = None) -> SessionResult:
        first = self.writer.first_free_index(self.max_frames)
        if first is None:
            raise FolderFullError(
                f"Frames 0..{self.max_frames - 1} already exist in {self.writer.output_dir}."
            )

        written = 0
        reason = StopReason.MAX_FRAMES
        # Absolute deadlines: the interval does not drift with capture/write time.
        deadline = time.monotonic()
        try:
            for index in range(first, self.max_frames):
                if self._stop.is_set():
                    reason = StopReason.STOPPED
                    break
                if not self.backend.is_alive(self.window):
                    reason = StopReason.WINDOW_CLOSED
                    break
                image = self.backend.grab(self.window)
                path = self.writer.write(index, image, self.window.title)
                written += 1
                if on_frame is not None:
                    on_frame(index, path)
                if index + 1 >= self.max_frames:
                    break  # last frame written: no pointless wait

                deadline += self.interval
                pause = deadline - time.monotonic()
                if pause > 0:
                    if self._stop.wait(pause):
                        reason = StopReason.STOPPED
                        break
                else:
                    deadline = time.monotonic()  # fell behind: do not burst to catch up
        except KeyboardInterrupt:
            reason = StopReason.INTERRUPTED
        return SessionResult(frames_written=written, first_index=first, reason=reason)
