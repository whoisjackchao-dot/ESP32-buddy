"""Bring the OpenCode terminal window to the foreground (M6 §8.3).

Windows-only via ctypes at call time; the module imports cleanly on any
platform so host tests can run on Linux (no ctypes.windll at import
time). Target: window class ``CASCADIA_HOSTING_WINDOW_CLASS`` (Windows
Terminal) whose title starts with ``OC``.
"""

import sys
import time

TARGET_CLASS = "CASCADIA_HOSTING_WINDOW_CLASS"
TITLE_PREFIX = "OC"
SW_RESTORE = 9
FOCUS_SETTLE_SECONDS = 0.2


def _find_target_hwnd(user32):
    import ctypes
    from ctypes import wintypes

    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetClassNameW.restype = ctypes.c_int
    user32.GetWindowTextLengthW.argtypes = [wintypes.HWND]
    user32.GetWindowTextLengthW.restype = ctypes.c_int
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.restype = ctypes.c_int

    EnumWindowsProc = ctypes.WINFUNCTYPE(
        wintypes.BOOL, wintypes.HWND, wintypes.LPARAM
    )
    user32.EnumWindows.argtypes = [EnumWindowsProc, wintypes.LPARAM]
    user32.EnumWindows.restype = wintypes.BOOL
    found = []

    def _on_window(hwnd, _lparam):
        buf = ctypes.create_unicode_buffer(256)
        if user32.GetClassNameW(hwnd, buf, 256) == 0:
            return True
        if buf.value != TARGET_CLASS:
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n <= 0:
            return True
        title = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, title, n + 1)
        if title.value.startswith(TITLE_PREFIX):
            found.append(hwnd)
            return False
        return True

    user32.EnumWindows(EnumWindowsProc(_on_window), 0)
    return found[0] if found else None


def _activate(user32, kernel32, hwnd) -> bool:
    import ctypes
    import time
    from ctypes import wintypes

    user32.ShowWindow(hwnd, SW_RESTORE)
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.1)
    if user32.GetForegroundWindow() == hwnd:
        return True

    this_thread = kernel32.GetCurrentThreadId()
    fg_hwnd = user32.GetForegroundWindow()
    fg_thread = user32.GetWindowThreadProcessId(fg_hwnd, None) if fg_hwnd else 0
    if fg_thread and fg_thread != this_thread:
        if user32.AttachThreadInput(this_thread, fg_thread, True):
            user32.ShowWindow(hwnd, SW_RESTORE)
            user32.SetForegroundWindow(hwnd)
            user32.AttachThreadInput(this_thread, fg_thread, False)
            time.sleep(0.1)
            if user32.GetForegroundWindow() == hwnd:
                return True

    VK_MENU = 0x12
    KEYEVENTF_KEYUP = 0x0002
    user32.keybd_event(VK_MENU, 0, 0, 0)
    user32.keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0)
    user32.SetForegroundWindow(hwnd)
    time.sleep(FOCUS_SETTLE_SECONDS)
    return user32.GetForegroundWindow() == hwnd


def focus_opencode_window() -> bool:
    """Activate the OpenCode window; True only if it ends up foreground."""
    if sys.platform != "win32":
        return False
    try:
        import ctypes
        from ctypes import wintypes

        user32 = ctypes.windll.user32
        kernel32 = ctypes.windll.kernel32

        user32.GetForegroundWindow.argtypes = []
        user32.GetForegroundWindow.restype = wintypes.HWND
        user32.SetForegroundWindow.argtypes = [wintypes.HWND]
        user32.SetForegroundWindow.restype = wintypes.BOOL
        user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
        user32.ShowWindow.restype = wintypes.BOOL
        user32.AttachThreadInput.argtypes = [
            wintypes.DWORD, wintypes.DWORD, wintypes.BOOL,
        ]
        user32.AttachThreadInput.restype = wintypes.BOOL
        user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, wintypes.LPDWORD]
        user32.GetWindowThreadProcessId.restype = wintypes.DWORD
        user32.keybd_event.argtypes = [
            ctypes.c_ubyte, ctypes.c_ubyte, ctypes.c_uint, ctypes.c_size_t,
        ]
        user32.keybd_event.restype = None
        kernel32.GetCurrentThreadId.argtypes = []
        kernel32.GetCurrentThreadId.restype = wintypes.DWORD

        hwnd = _find_target_hwnd(user32)
        if hwnd is None:
            return False
        return _activate(user32, kernel32, hwnd)
    except Exception as exc:
        print("[win_focus] %r" % (exc,), file=sys.stderr)
        return False
