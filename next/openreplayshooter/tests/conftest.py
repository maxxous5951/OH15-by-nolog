import os

# Qt must run without a display in tests and CI. Set before any QApplication exists.
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
