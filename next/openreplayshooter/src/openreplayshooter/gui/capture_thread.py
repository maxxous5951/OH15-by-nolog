"""Runs a CaptureSession off the UI thread."""

from __future__ import annotations

from PySide6.QtCore import QObject, QThread, Signal

from openreplayshooter.session import CaptureSession


class CaptureThread(QThread):
    """Signals are emitted from the worker thread and delivered queued to the UI."""

    frame_written = Signal(int, str)  # frame index, image path
    completed = Signal(object)  # SessionResult
    failed = Signal(str)  # message, when the session ended on an error

    def __init__(self, session: CaptureSession, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._session = session

    def stop(self) -> None:
        """Ask the capture loop to end; it wakes up immediately from its wait."""
        self._session.stop()

    def run(self) -> None:
        try:
            result = self._session.run(
                on_frame=lambda i, path: self.frame_written.emit(i, str(path))
            )
        except Exception as error:
            self.failed.emit(str(error) or type(error).__name__)
        else:
            self.completed.emit(result)
