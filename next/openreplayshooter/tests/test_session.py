import time

import pytest

from fakes import ScriptedBackend
from openreplayshooter.backends.synthetic import SYNTHETIC_WINDOW, SyntheticBackend
from openreplayshooter.model import FolderFullError, StopReason, WindowInfo
from openreplayshooter.session import CaptureSession
from openreplayshooter.writer import FrameWriter


def make_session(tmp_path, backend=None, **kwargs):
    backend = backend or SyntheticBackend()
    kwargs.setdefault("interval_ms", 0)
    return CaptureSession(backend, SYNTHETIC_WINDOW, FrameWriter(tmp_path), **kwargs)


def test_stops_at_max_frames(tmp_path):
    result = make_session(tmp_path, max_frames=4).run()

    assert (result.frames_written, result.first_index) == (4, 0)
    assert result.reason is StopReason.MAX_FRAMES
    assert sorted(p.name for p in tmp_path.glob("*.htm")) == [
        "frame000000.htm",
        "frame000001.htm",
        "frame000002.htm",
        "frame000003.htm",
    ]


def test_resumes_after_existing_frames(tmp_path):
    make_session(tmp_path, max_frames=2).run()
    result = make_session(tmp_path, max_frames=5).run()

    assert (result.frames_written, result.first_index) == (3, 2)
    assert (tmp_path / "frame000004.bmp").exists()


def test_full_folder_raises(tmp_path):
    make_session(tmp_path, max_frames=2).run()
    with pytest.raises(FolderFullError):
        make_session(tmp_path, max_frames=2).run()


def test_window_closing_ends_the_session_but_keeps_frames(tmp_path):
    window = WindowInfo(5, "Gone Soon")
    backend = ScriptedBackend([window], close_after=2)
    session = CaptureSession(backend, window, FrameWriter(tmp_path), interval_ms=0, max_frames=10)

    result = session.run()

    assert result.reason is StopReason.WINDOW_CLOSED
    assert result.frames_written == 2
    assert (tmp_path / "frame000001.htm").exists()


def test_stop_requested_from_the_callback(tmp_path):
    session = make_session(tmp_path, max_frames=50)
    result = session.run(on_frame=lambda index, _path: session.stop() if index == 2 else None)

    assert result.reason is StopReason.STOPPED
    assert result.frames_written == 3


def test_stop_interrupts_the_wait_between_frames(tmp_path):
    session = make_session(tmp_path, interval_ms=60_000, max_frames=5)
    started = time.monotonic()
    result = session.run(on_frame=lambda *_: session.stop())

    assert result.reason is StopReason.STOPPED
    assert result.frames_written == 1
    assert time.monotonic() - started < 5  # did not sit out the 60 s interval


def test_keyboard_interrupt_is_reported_and_earlier_frames_are_kept(tmp_path):
    window = WindowInfo(9, "Ctrl-C me")
    backend = ScriptedBackend([window], interrupt_at=3)
    session = CaptureSession(backend, window, FrameWriter(tmp_path), interval_ms=0, max_frames=10)

    result = session.run()

    assert result.reason is StopReason.INTERRUPTED
    assert result.frames_written == 2


def test_interval_is_respected_without_a_trailing_wait(tmp_path):
    started = time.monotonic()
    make_session(tmp_path, interval_ms=80, max_frames=3).run()
    elapsed = time.monotonic() - started

    assert elapsed >= 0.15  # two gaps between three frames
    assert elapsed < 0.6  # but no third gap after the last frame


def test_on_frame_receives_index_and_path(tmp_path):
    seen = []
    make_session(tmp_path, max_frames=2).run(on_frame=lambda i, p: seen.append((i, p.name)))
    assert seen == [(0, "frame000000.bmp"), (1, "frame000001.bmp")]


@pytest.mark.parametrize("kwargs", [{"interval_ms": -1}, {"max_frames": 0}])
def test_invalid_arguments_are_rejected(tmp_path, kwargs):
    with pytest.raises(ValueError, match="must be"):
        make_session(tmp_path, **kwargs)
