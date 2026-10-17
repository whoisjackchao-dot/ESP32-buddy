"""USB-Serial-JTAG daemon: device control lines → host actions (M6 §8.3).

Reads lines from the device serial port, parses control lines via
pet_devio (§8.1) and dispatches: ``back_window`` focuses the OpenCode
window (win_focus). Log lines are ignored. Local serial only, no auth
(§8.3).
"""

import argparse
import sys

import pet_devio

DEFAULT_PORT = "COM5"
DEFAULT_BAUD = 115200


def run(port=DEFAULT_PORT, serial_obj=None, focus=None, stop_after=None) -> int:
    """Consume device lines; dispatch control commands.

    stop_after caps the number of non-timeout lines processed (tests);
    None runs forever. Closes only a serial this function opened.
    """
    own_serial = False
    if serial_obj is None:
        import serial  # lazy: pyserial only needed for the real port

        serial_obj = serial.Serial(port, DEFAULT_BAUD, timeout=1)
        own_serial = True
    if focus is None:
        import win_focus

        focus = win_focus.focus_opencode_window

    processed = 0
    try:
        while stop_after is None or processed < stop_after:
            raw = serial_obj.readline()
            if raw in (b"", ""):
                continue
            processed += 1
            if isinstance(raw, str):
                text = raw
            else:
                text = raw.decode("utf-8", errors="replace")
            cmd = pet_devio.parse_device_line(text)
            if cmd and cmd.get("cmd") == "back_window":
                ok = bool(focus())
                print(
                    "[pet_daemon] back_window -> focus %s" % ("ok" if ok else "FAILED"),
                    file=sys.stderr,
                )
    finally:
        if own_serial:
            serial_obj.close()
    return 0


def main(argv=None, serial_factory=None) -> int:
    """CLI: --port (default COM5), --baud (default 115200)."""
    parser = argparse.ArgumentParser(
        prog="pet_daemon",
        description="ESP32-buddy device control-line daemon (M6 B08)",
    )
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    args = parser.parse_args(argv)

    if serial_factory is not None:
        serial_obj = serial_factory(args.port, args.baud)
    else:
        import serial  # lazy: pyserial only needed for the real port

        serial_obj = serial.Serial(args.port, args.baud, timeout=1)
    try:
        return run(serial_obj=serial_obj)
    finally:
        serial_obj.close()


if __name__ == "__main__":
    sys.exit(main())
