"""Pure parsing of device→host control lines (contract M6 §8.1).

The device shares one TX stream between ESP-IDF logs and control
commands: a line starting with '{' is a JSON control command
(``{"v":1,"cmd":"..."}\n``, UTF-8); any other line is a log the host
ignores. No I/O here -- host-testable by design (§8.3).
"""

import json


def parse_device_line(line: str) -> dict | None:
    """Parse one device line; return the command dict, or None.

    A control line is a JSON object with integer ``v == 1`` (bool
    rejected) and a non-empty string ``cmd``. Log lines, blanks and
    garbage all yield None.
    """
    text = line.strip()
    if not text.startswith("{"):
        return None
    try:
        obj = json.loads(text)
    except ValueError:
        return None
    if not isinstance(obj, dict):
        return None
    v = obj.get("v")
    if isinstance(v, bool) or not isinstance(v, int) or v != 1:
        return None
    cmd = obj.get("cmd")
    if not isinstance(cmd, str) or not cmd:
        return None
    return obj
