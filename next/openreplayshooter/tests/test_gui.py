import pytest

pytest.importorskip("PySide6")
pytest.importorskip("pytestqt")

from PySide6.QtCore import QSettings, Qt

from fakes import ScriptedBackend
from openreplayshooter.backends.synthetic import SyntheticBackend
from openreplayshooter.cli import main
from openreplayshooter.gui.main_window import MainWindow
from openreplayshooter.model import WindowInfo
from openreplayshooter.writer import FrameWriter

WAIT_MS = 10_000


def new_window(qtbot, tmp_path, backend=None, name="settings.ini"):
    settings = QSettings(str(tmp_path / name), QSettings.Format.IniFormat)
    window = MainWindow(backend or SyntheticBackend(), settings)
    qtbot.addWidget(window)
    window.output_edit.setText(str(tmp_path / "out"))
    window.interval_spin.setValue(50)
    return window


def click(qtbot, button):
    qtbot.mouseClick(button, Qt.MouseButton.LeftButton)


def wait_idle(qtbot, window):
    qtbot.waitUntil(lambda: not window.is_capturing(), timeout=WAIT_MS)


@pytest.fixture
def window(qtbot, tmp_path):
    return new_window(qtbot, tmp_path)


def test_lists_the_available_windows(window):
    assert window.window_combo.count() == 1
    assert "Synthetic Table" in window.window_combo.itemText(0)
    assert window.selected_window().title == "Synthetic Table"
    assert window.start_button.text() == "Start"


def test_capture_writes_frames_then_returns_to_idle(qtbot, window, tmp_path):
    window.max_spin.setValue(3)

    click(qtbot, window.start_button)
    wait_idle(qtbot, window)

    assert len(list((tmp_path / "out").glob("frame*.htm"))) == 3
    assert window.progress.value() == window.progress.maximum() == 3
    assert "Done: 3 frame(s) saved." in window.status_label.text()
    assert window.start_button.text() == "Start"
    assert window.output_edit.isEnabled()
    assert not window.preview.pixmap().isNull()


def test_controls_are_locked_while_capturing_and_stop_works(qtbot, window):
    window.interval_spin.setValue(10_000)  # one frame, then a long wait to interrupt
    window.max_spin.setValue(50)

    click(qtbot, window.start_button)
    assert window.is_capturing()
    assert window.start_button.text() == "Stop"
    assert not window.window_combo.isEnabled()
    assert not window.interval_spin.isEnabled()

    qtbot.waitUntil(lambda: window.progress.value() >= 1, timeout=WAIT_MS)
    click(qtbot, window.start_button)
    wait_idle(qtbot, window)

    assert "Stopped: 1 frame(s) saved." in window.status_label.text()
    assert window.interval_spin.isEnabled()


def test_resumes_after_existing_frames(qtbot, window, tmp_path):
    window.max_spin.setValue(2)
    click(qtbot, window.start_button)
    wait_idle(qtbot, window)

    window.max_spin.setValue(5)
    click(qtbot, window.start_button)
    wait_idle(qtbot, window)

    assert window.progress.maximum() == 3  # frames 2, 3 and 4 remained
    assert (tmp_path / "out" / "frame000004.bmp").exists()


def test_full_folder_is_reported_without_starting(qtbot, window, tmp_path):
    from PIL import Image

    writer = FrameWriter(tmp_path / "out")
    for index in range(2):
        writer.write(index, Image.new("RGB", (4, 4)), "t")
    window.max_spin.setValue(2)

    click(qtbot, window.start_button)

    assert not window.is_capturing()
    assert "already exist" in window.status_label.text()


def test_start_without_a_window_explains_what_to_do(qtbot, tmp_path):
    backend = SyntheticBackend()
    backend.closed = True  # lists no window
    window = new_window(qtbot, tmp_path, backend)

    click(qtbot, window.start_button)

    assert window.selected_window() is None
    assert not window.is_capturing()
    assert "Select a window" in window.status_label.text()


def test_start_without_an_output_folder_is_refused(qtbot, window):
    window.output_edit.setText("   ")
    click(qtbot, window.start_button)
    assert not window.is_capturing()
    assert "output folder" in window.status_label.text()


def test_refresh_keeps_the_selection(window):
    backend = ScriptedBackend([WindowInfo(10, "Alpha"), WindowInfo(11, "Beta")])
    window._backend = backend
    window.refresh_windows()
    window.window_combo.setCurrentIndex(1)

    window.refresh_windows()

    assert window.selected_window().title == "Beta"


def test_closed_target_window_is_reported(qtbot, tmp_path):
    target = WindowInfo(7, "Short Lived")
    backend = ScriptedBackend([target], close_after=2)
    window = new_window(qtbot, tmp_path, backend)
    window.max_spin.setValue(10)

    click(qtbot, window.start_button)
    wait_idle(qtbot, window)

    assert "closed: 2 frame(s) saved." in window.status_label.text()


def test_backend_error_is_shown_not_raised(qtbot, tmp_path):
    class Broken(SyntheticBackend):
        def grab(self, window):
            raise OSError("PrintWindow failed")

    window = new_window(qtbot, tmp_path, Broken())
    click(qtbot, window.start_button)
    wait_idle(qtbot, window)

    assert "Capture failed: PrintWindow failed" in window.status_label.text()
    assert window.start_button.text() == "Start"


def test_png_option_shows_the_ohreplay_warning_and_is_used(qtbot, window, tmp_path):
    window.show()
    assert not window.format_hint.isVisible()
    window.format_combo.setCurrentIndex(window.format_combo.findData("png"))
    assert window.format_hint.isVisible()

    window.max_spin.setValue(1)
    window.metadata_check.setChecked(True)
    click(qtbot, window.start_button)
    wait_idle(qtbot, window)

    assert (tmp_path / "out" / "frame000000.png").exists()
    assert (tmp_path / "out" / "frame000000.json").exists()


def test_settings_survive_a_restart(qtbot, tmp_path):
    first = new_window(qtbot, tmp_path)
    first.interval_spin.setValue(2500)
    first.max_spin.setValue(77)
    first.format_combo.setCurrentIndex(first.format_combo.findData("png"))
    first.legacy_check.setChecked(False)
    first.metadata_check.setChecked(True)
    first.close()

    second = MainWindow(
        SyntheticBackend(), QSettings(str(tmp_path / "settings.ini"), QSettings.Format.IniFormat)
    )
    qtbot.addWidget(second)

    assert second.output_edit.text() == str(tmp_path / "out")
    assert second.interval_spin.value() == 2500
    assert second.max_spin.value() == 77
    assert second.format_combo.currentData() == "png"
    assert not second.legacy_check.isChecked()
    assert second.metadata_check.isChecked()
    assert second.selected_window().title == "Synthetic Table"


def test_closing_during_capture_stops_cleanly(qtbot, window):
    window.interval_spin.setValue(10_000)
    window.max_spin.setValue(50)
    click(qtbot, window.start_button)
    qtbot.waitUntil(lambda: window.progress.value() >= 1, timeout=WAIT_MS)

    window.close()

    qtbot.waitUntil(lambda: not window.is_capturing(), timeout=WAIT_MS)


def test_gui_subcommand_opens_the_interface(monkeypatch):
    seen = []
    monkeypatch.setattr("openreplayshooter.gui.app.run", lambda backend: seen.append(backend) or 7)
    assert main(["gui", "--backend", "synthetic"]) == 7
    assert seen == ["synthetic"]
