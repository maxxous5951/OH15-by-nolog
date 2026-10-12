"""The main window."""

from __future__ import annotations

from pathlib import Path

from PySide6.QtCore import QSettings, Qt
from PySide6.QtGui import QCloseEvent, QPixmap, QResizeEvent
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QProgressBar,
    QPushButton,
    QSizePolicy,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from openreplayshooter import __version__
from openreplayshooter.backends.base import CaptureBackend
from openreplayshooter.gui.capture_thread import CaptureThread
from openreplayshooter.model import SessionResult, StopReason, WindowInfo
from openreplayshooter.session import CaptureSession
from openreplayshooter.writer import FrameWriter, frame_stem

_ERROR_STYLE = "color: #d64541;"  # readable on both light and dark themes
_STOP_TEXT = {
    StopReason.MAX_FRAMES: "Done",
    StopReason.STOPPED: "Stopped",
    StopReason.INTERRUPTED: "Stopped",
    StopReason.WINDOW_CLOSED: "The captured window was closed",
}


class MainWindow(QMainWindow):
    def __init__(self, backend: CaptureBackend, settings: QSettings | None = None) -> None:
        super().__init__()
        self._backend = backend
        self._settings = settings or QSettings("OpenHoldem", "OpenReplayShooter")
        self._thread: CaptureThread | None = None
        self._preview_source = QPixmap()
        self._last_window_title = ""

        self.setWindowTitle(f"OpenReplayShooter {__version__}")
        self._build_ui()
        self._load_settings()
        self.refresh_windows()
        self._set_capturing(False)
        self._show_status("Ready.")

    # ------------------------------------------------------------------ layout

    def _build_ui(self) -> None:
        self.window_combo = QComboBox()
        self.window_combo.setMinimumContentsLength(40)
        self.window_combo.setSizeAdjustPolicy(
            QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
        )
        self.refresh_button = QPushButton("Refresh")
        self.refresh_button.setToolTip("Look for windows again")
        self.refresh_button.clicked.connect(self.refresh_windows)

        self.output_edit = QLineEdit()
        self.output_edit.setPlaceholderText("Folder that will receive frame000000.bmp, ...")
        self.browse_button = QPushButton("Browse…")
        self.browse_button.clicked.connect(self._browse)

        window_row = QHBoxLayout()
        window_row.addWidget(self.window_combo, 1)
        window_row.addWidget(self.refresh_button)
        output_row = QHBoxLayout()
        output_row.addWidget(self.output_edit, 1)
        output_row.addWidget(self.browse_button)

        source_box = QGroupBox("Source")
        source_form = QFormLayout(source_box)
        source_form.addRow("Window", window_row)
        source_form.addRow("Output folder", output_row)

        self.interval_spin = QSpinBox()
        self.interval_spin.setRange(50, 60_000)
        self.interval_spin.setSingleStep(100)
        self.interval_spin.setSuffix(" ms")
        self.interval_spin.setValue(1000)
        self.max_spin = QSpinBox()
        self.max_spin.setRange(1, 100_000)
        self.max_spin.setValue(1000)
        self.max_spin.setToolTip(
            "Highest frame number + 1. Frames already in the folder are kept and "
            "capture resumes after them."
        )
        self.format_combo = QComboBox()
        self.format_combo.addItem("BMP (read by OHReplay)", "bmp")
        self.format_combo.addItem("PNG (smaller files)", "png")
        self.format_combo.currentIndexChanged.connect(self._update_format_hint)
        self.format_hint = QLabel("OHReplay only opens BMP frames.")
        self.format_hint.setStyleSheet(_ERROR_STYLE)
        self.legacy_check = QCheckBox("Write the .htm title file (needed by OHReplay)")
        self.metadata_check = QCheckBox("Write a .json file per frame (title, size, time)")

        settings_box = QGroupBox("Settings")
        settings_form = QFormLayout(settings_box)
        settings_form.addRow("Delay between frames", self.interval_spin)
        settings_form.addRow("Max. frames", self.max_spin)
        settings_form.addRow("Image format", self.format_combo)
        settings_form.addRow("", self.format_hint)
        settings_form.addRow(self.legacy_check)
        settings_form.addRow(self.metadata_check)

        self.preview = QLabel("The last captured frame appears here.")
        self.preview.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.preview.setMinimumSize(360, 220)
        # Ignored: the pixmap must never dictate the size of the window.
        self.preview.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Ignored)
        self.preview.setFrameShape(QLabel.Shape.StyledPanel)
        preview_box = QGroupBox("Preview")
        QVBoxLayout(preview_box).addWidget(self.preview)

        self.progress = QProgressBar()
        self.progress.setTextVisible(True)
        self.progress.setFormat("%v / %m")
        self.status_label = QLabel()
        self.status_label.setWordWrap(True)
        self.start_button = QPushButton("Start")
        self.start_button.setDefault(True)
        self.start_button.setMinimumWidth(110)
        self.start_button.clicked.connect(self._toggle_capture)

        bottom_row = QHBoxLayout()
        bottom_row.addWidget(self.progress, 1)
        bottom_row.addWidget(self.start_button)

        content = QWidget()
        layout = QVBoxLayout(content)
        layout.addWidget(source_box)
        layout.addWidget(settings_box)
        layout.addWidget(preview_box, 1)
        layout.addLayout(bottom_row)
        layout.addWidget(self.status_label)
        self.setCentralWidget(content)
        self.resize(640, 700)

    # ---------------------------------------------------------------- settings

    def _load_settings(self) -> None:
        s = self._settings
        default_folder = str(Path.home() / "OpenReplayShooter")
        self.output_edit.setText(s.value("output_dir", default_folder, type=str))
        self.interval_spin.setValue(s.value("interval_ms", 1000, type=int))
        self.max_spin.setValue(s.value("max_frames", 1000, type=int))
        index = self.format_combo.findData(s.value("image_format", "bmp", type=str))
        self.format_combo.setCurrentIndex(max(index, 0))
        self.legacy_check.setChecked(s.value("legacy_html", True, type=bool))
        self.metadata_check.setChecked(s.value("metadata", False, type=bool))
        self._last_window_title = s.value("window_title", "", type=str)
        self._update_format_hint()

    def _save_settings(self) -> None:
        s = self._settings
        s.setValue("output_dir", self.output_edit.text().strip())
        s.setValue("interval_ms", self.interval_spin.value())
        s.setValue("max_frames", self.max_spin.value())
        s.setValue("image_format", self.format_combo.currentData())
        s.setValue("legacy_html", self.legacy_check.isChecked())
        s.setValue("metadata", self.metadata_check.isChecked())
        window = self.selected_window()
        if window is not None:
            s.setValue("window_title", window.title)
        s.sync()

    # ----------------------------------------------------------------- windows

    def selected_window(self) -> WindowInfo | None:
        return self.window_combo.currentData()

    def refresh_windows(self) -> None:
        """Rebuild the window list, keeping the previous choice when it still exists."""
        previous = self.selected_window()
        # Only the Windows backend shares a handle namespace with Qt (both are HWNDs).
        own_handle = int(self.winId()) if self._backend.name == "printwindow" else None
        windows = sorted(
            (w for w in self._backend.list_windows() if w.handle != own_handle),
            key=lambda w: w.title.casefold(),
        )
        self.window_combo.clear()
        for window in windows:
            self.window_combo.addItem(str(window), window)

        wanted = previous.title if previous else self._last_window_title
        for row in range(self.window_combo.count()):
            if self.window_combo.itemData(row).title == wanted:
                self.window_combo.setCurrentIndex(row)
                break

    def _browse(self) -> None:
        start = self.output_edit.text().strip() or str(Path.home())
        folder = QFileDialog.getExistingDirectory(self, "Choose the output folder", start)
        if folder:
            self.output_edit.setText(folder)

    def _update_format_hint(self) -> None:
        self.format_hint.setVisible(self.format_combo.currentData() != "bmp")

    # ----------------------------------------------------------------- capture

    def is_capturing(self) -> bool:
        return self._thread is not None

    def _toggle_capture(self) -> None:
        if self.is_capturing():
            self._stop_capture()
        else:
            self._start_capture()

    def _start_capture(self) -> None:
        window = self.selected_window()
        if window is None:
            self._show_error("Select a window to capture (use Refresh if it is not listed).")
            return
        folder_text = self.output_edit.text().strip()
        if not folder_text:
            self._show_error("Choose an output folder.")
            return

        writer = FrameWriter(
            Path(folder_text),
            image_format=self.format_combo.currentData(),
            legacy_html=self.legacy_check.isChecked(),
            metadata=self.metadata_check.isChecked(),
        )
        max_frames = self.max_spin.value()
        first = writer.first_free_index(max_frames)
        if first is None:
            self._show_error(
                f"Frames 0..{max_frames - 1} already exist in this folder. "
                "Raise Max. frames or choose another folder."
            )
            return

        session = CaptureSession(
            self._backend,
            window,
            writer,
            interval_ms=self.interval_spin.value(),
            max_frames=max_frames,
        )
        self.progress.setRange(0, max_frames - first)
        self.progress.setValue(0)
        thread = CaptureThread(session, self)
        thread.frame_written.connect(self._on_frame)
        thread.completed.connect(self._on_completed)
        thread.failed.connect(self._on_failed)
        thread.finished.connect(self._on_thread_finished)
        self._thread = thread
        self._set_capturing(True)
        self._show_status(f"Capturing “{window.title}”…")
        thread.start()

    def _stop_capture(self) -> None:
        if self._thread is not None:
            self.start_button.setEnabled(False)
            self._show_status("Stopping…")
            self._thread.stop()

    def _on_frame(self, index: int, path: str) -> None:
        self.progress.setValue(self.progress.value() + 1)
        self._show_status(f"Saved {frame_stem(index)}")
        pixmap = QPixmap(path)
        if not pixmap.isNull():
            self._preview_source = pixmap
            self._refresh_preview()

    def _on_completed(self, result: SessionResult) -> None:
        self._show_status(f"{_STOP_TEXT[result.reason]}: {result.frames_written} frame(s) saved.")

    def _on_failed(self, message: str) -> None:
        self._show_error(f"Capture failed: {message}")

    def _on_thread_finished(self) -> None:
        thread, self._thread = self._thread, None
        if thread is not None:
            thread.deleteLater()
        self._set_capturing(False)

    def _set_capturing(self, capturing: bool) -> None:
        for widget in (
            self.window_combo,
            self.refresh_button,
            self.output_edit,
            self.browse_button,
            self.interval_spin,
            self.max_spin,
            self.format_combo,
            self.legacy_check,
            self.metadata_check,
        ):
            widget.setEnabled(not capturing)
        self.start_button.setEnabled(True)
        self.start_button.setText("Stop" if capturing else "Start")

    # ------------------------------------------------------------------ status

    def _show_status(self, text: str) -> None:
        self.status_label.setStyleSheet("")
        self.status_label.setText(text)

    def _show_error(self, text: str) -> None:
        self.status_label.setStyleSheet(_ERROR_STYLE)
        self.status_label.setText(text)

    # ------------------------------------------------------------------ events

    def _refresh_preview(self) -> None:
        if self._preview_source.isNull():
            return
        self.preview.setPixmap(
            self._preview_source.scaled(
                self.preview.contentsRect().size(),  # inside the frame, not under it
                Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation,
            )
        )

    def resizeEvent(self, event: QResizeEvent) -> None:
        super().resizeEvent(event)
        self._refresh_preview()

    def closeEvent(self, event: QCloseEvent) -> None:
        if self._thread is not None:
            self._thread.stop()
            self._thread.wait(5000)
        self._save_settings()
        super().closeEvent(event)
