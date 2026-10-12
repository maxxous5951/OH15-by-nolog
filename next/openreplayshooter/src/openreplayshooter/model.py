"""Small shared types."""

from __future__ import annotations

from dataclasses import dataclass
from enum import StrEnum


@dataclass(frozen=True, slots=True)
class WindowInfo:
    """A capturable top-level window."""

    handle: int
    title: str

    def __str__(self) -> str:
        return f"0x{self.handle:08X}  {self.title}"


class StopReason(StrEnum):
    """Why a capture session ended."""

    MAX_FRAMES = "max_frames"
    STOPPED = "stopped"
    INTERRUPTED = "interrupted"
    WINDOW_CLOSED = "window_closed"


@dataclass(frozen=True, slots=True)
class SessionResult:
    """Outcome of a capture session."""

    frames_written: int
    first_index: int
    reason: StopReason


class CaptureError(Exception):
    """Base class for capture problems reported to the user."""


class FolderFullError(CaptureError):
    """Every frame slot up to the requested maximum is already used."""


class BackendUnavailableError(CaptureError):
    """The requested capture backend cannot run on this machine."""
