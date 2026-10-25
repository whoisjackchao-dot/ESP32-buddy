"""Windows SendInput helpers for the voice flow (best-effort).

The PTT flow copies the transcript to the clipboard, then focuses the OpenCode
window and presses Ctrl+V (draft insert). A later KEY1 short press is turned
into Enter (submit) by the serial bridge.

On non-Windows (tests/WSL) every call returns False without raising.
"""

import sys

_VK_CONTROL = 0x11
_VK_V = 0x56
_VK_RETURN = 0x0D
_KEYEVENTF_KEYUP = 0x0002


def _user32():
    if sys.platform != "win32":
        return None
    import ctypes

    return ctypes.windll.user32


def _key(vk: int, up: bool) -> None:
    u = _user32()
    if u is None:
        return
    u.keybd_event(vk, 0, _KEYEVENTF_KEYUP if up else 0, 0)


def send_ctrl_v() -> bool:
    """Paste into the focused window."""
    if _user32() is None:
        return False
    try:
        _key(_VK_CONTROL, False)
        _key(_VK_V, False)
        _key(_VK_V, True)
        _key(_VK_CONTROL, True)
        return True
    except Exception:
        return False


def send_enter() -> bool:
    """Submit the focused window's input."""
    if _user32() is None:
        return False
    try:
        _key(_VK_RETURN, False)
        _key(_VK_RETURN, True)
        return True
    except Exception:
        return False
