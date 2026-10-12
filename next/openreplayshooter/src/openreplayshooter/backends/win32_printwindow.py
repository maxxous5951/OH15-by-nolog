"""Windows backend based on ``PrintWindow`` (pure ctypes, no extra dependency).

Compared with the old ``BitBlt`` on the window DC:

* ``PW_CLIENTONLY`` returns the client area directly, so no border/title-bar
  arithmetic that breaks with themes or DPI scaling;
* ``PW_RENDERFULLCONTENT`` asks DWM for the composed content, so partially
  covered windows still capture correctly;
* the process is made DPI aware, so sizes are real pixels at any scaling.

Limit: windows drawn straight by the GPU can come back black with this API.
A Windows Graphics Capture backend is the planned answer for those.
"""

from __future__ import annotations

import ctypes
import sys
from ctypes import wintypes

from PIL import Image

from openreplayshooter.model import BackendUnavailableError, WindowInfo

if sys.platform != "win32":  # pragma: no cover - guarded at import by the registry
    raise BackendUnavailableError("The 'printwindow' backend only runs on Windows.")

PW_CLIENTONLY = 0x1
PW_RENDERFULLCONTENT = 0x2
SW_RESTORE = 9
DWMWA_CLOAKED = 14
DIB_RGB_COLORS = 0
BI_RGB = 0

_user32 = ctypes.WinDLL("user32", use_last_error=True)
_gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
_dwmapi = ctypes.WinDLL("dwmapi", use_last_error=True)

_HANDLE = wintypes.HANDLE
_WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


class _BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = (
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    )


class _BITMAPINFO(ctypes.Structure):
    _fields_ = (("bmiHeader", _BITMAPINFOHEADER), ("bmiColors", wintypes.DWORD * 3))


def _declare(func, restype, *argtypes):
    # Explicit prototypes: without them 64-bit handles are truncated to 32 bits.
    func.restype = restype
    func.argtypes = argtypes


_declare(_user32.EnumWindows, wintypes.BOOL, _WNDENUMPROC, wintypes.LPARAM)
_declare(_user32.IsWindow, wintypes.BOOL, wintypes.HWND)
_declare(_user32.IsWindowVisible, wintypes.BOOL, wintypes.HWND)
_declare(_user32.IsIconic, wintypes.BOOL, wintypes.HWND)
_declare(_user32.ShowWindow, wintypes.BOOL, wintypes.HWND, ctypes.c_int)
_declare(_user32.GetShellWindow, wintypes.HWND)
_declare(_user32.GetWindowTextLengthW, ctypes.c_int, wintypes.HWND)
_declare(_user32.GetWindowTextW, ctypes.c_int, wintypes.HWND, wintypes.LPWSTR, ctypes.c_int)
_declare(_user32.GetClientRect, wintypes.BOOL, wintypes.HWND, ctypes.POINTER(wintypes.RECT))
_declare(_user32.GetDC, wintypes.HDC, wintypes.HWND)
_declare(_user32.ReleaseDC, ctypes.c_int, wintypes.HWND, wintypes.HDC)
_declare(_user32.PrintWindow, wintypes.BOOL, wintypes.HWND, wintypes.HDC, wintypes.UINT)
_declare(_gdi32.CreateCompatibleDC, wintypes.HDC, wintypes.HDC)
_declare(_gdi32.CreateCompatibleBitmap, _HANDLE, wintypes.HDC, ctypes.c_int, ctypes.c_int)
_declare(_gdi32.SelectObject, _HANDLE, wintypes.HDC, _HANDLE)
_declare(_gdi32.DeleteObject, wintypes.BOOL, _HANDLE)
_declare(_gdi32.DeleteDC, wintypes.BOOL, wintypes.HDC)
_declare(
    _gdi32.GetDIBits,
    ctypes.c_int,
    wintypes.HDC,
    _HANDLE,
    wintypes.UINT,
    wintypes.UINT,
    ctypes.c_void_p,
    ctypes.POINTER(_BITMAPINFO),
    wintypes.UINT,
)
_declare(
    _dwmapi.DwmGetWindowAttribute,
    ctypes.c_long,
    wintypes.HWND,
    wintypes.DWORD,
    ctypes.c_void_p,
    wintypes.DWORD,
)


def _make_dpi_aware() -> None:
    """Ask for real pixels. Tries the newest API first, falls back on older ones."""
    try:
        _user32.SetProcessDpiAwarenessContext.argtypes = [ctypes.c_void_p]
        if _user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4)):  # per-monitor v2
            return
    except (AttributeError, OSError):
        pass
    try:
        ctypes.WinDLL("shcore").SetProcessDpiAwareness(2)
    except (AttributeError, OSError):
        _user32.SetProcessDPIAware()


def _title_of(hwnd: int) -> str:
    length = _user32.GetWindowTextLengthW(hwnd)
    if length == 0:
        return ""
    buffer = ctypes.create_unicode_buffer(length + 1)
    _user32.GetWindowTextW(hwnd, buffer, length + 1)
    return buffer.value


def _is_cloaked(hwnd: int) -> bool:
    """True for windows DWM keeps visible-but-hidden (suspended UWP apps, other desktops)."""
    cloaked = wintypes.DWORD(0)
    result = _dwmapi.DwmGetWindowAttribute(
        hwnd, DWMWA_CLOAKED, ctypes.byref(cloaked), ctypes.sizeof(cloaked)
    )
    return result == 0 and cloaked.value != 0


class PrintWindowBackend:
    name = "printwindow"

    def __init__(self) -> None:
        _make_dpi_aware()

    def list_windows(self) -> list[WindowInfo]:
        shell = _user32.GetShellWindow()
        found: list[WindowInfo] = []

        @_WNDENUMPROC
        def visit(hwnd: int, _lparam: int) -> bool:
            if hwnd == shell or not _user32.IsWindowVisible(hwnd) or _is_cloaked(hwnd):
                return True
            title = _title_of(hwnd)
            if title:
                found.append(WindowInfo(handle=int(hwnd), title=title))
            return True

        _user32.EnumWindows(visit, 0)
        return found

    def is_alive(self, window: WindowInfo) -> bool:
        return bool(_user32.IsWindow(window.handle))

    def grab(self, window: WindowInfo) -> Image.Image:
        hwnd = window.handle
        if _user32.IsIconic(hwnd):
            # A minimized window has no content to print. Same behaviour as the old
            # tool, but only when needed instead of forcing "restore" on every frame.
            _user32.ShowWindow(hwnd, SW_RESTORE)

        rect = wintypes.RECT()
        if not _user32.GetClientRect(hwnd, ctypes.byref(rect)):
            raise OSError(ctypes.get_last_error(), "GetClientRect failed")
        width, height = rect.right - rect.left, rect.bottom - rect.top
        if width <= 0 or height <= 0:
            raise OSError(f"Window has an empty client area ({width}x{height})")

        screen_dc = _user32.GetDC(None)
        memory_dc = _gdi32.CreateCompatibleDC(screen_dc)
        bitmap = _gdi32.CreateCompatibleBitmap(screen_dc, width, height)
        previous = _gdi32.SelectObject(memory_dc, bitmap)
        try:
            if not _user32.PrintWindow(hwnd, memory_dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT):
                raise OSError(ctypes.get_last_error(), "PrintWindow failed")

            info = _BITMAPINFO()
            info.bmiHeader.biSize = ctypes.sizeof(_BITMAPINFOHEADER)
            info.bmiHeader.biWidth = width
            info.bmiHeader.biHeight = -height  # negative = top-down rows
            info.bmiHeader.biPlanes = 1
            info.bmiHeader.biBitCount = 32
            info.bmiHeader.biCompression = BI_RGB
            pixels = ctypes.create_string_buffer(width * height * 4)
            # The bitmap must not be selected into a DC while GetDIBits reads it.
            _gdi32.SelectObject(memory_dc, previous)
            copied = _gdi32.GetDIBits(
                memory_dc, bitmap, 0, height, pixels, ctypes.byref(info), DIB_RGB_COLORS
            )
            if copied != height:
                raise OSError("GetDIBits returned an incomplete bitmap")
            return Image.frombuffer("RGB", (width, height), pixels, "raw", "BGRX", 0, 1).copy()
        finally:
            # A bitmap still selected into a DC cannot be deleted, so deselect first.
            _gdi32.SelectObject(memory_dc, previous)
            _gdi32.DeleteObject(bitmap)
            _gdi32.DeleteDC(memory_dc)
            _user32.ReleaseDC(None, screen_dc)
