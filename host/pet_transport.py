"""M4 increment 2b: host-side transport (contract M4 §9.1/§9.3).

Framing: one NDJSON line per event (M4 §6.1): UTF-8, exactly one
trailing "\n". SerialSink writes framed lines to a serial port;
pyserial is imported lazily inside the real-serial path only, so
importing this module (and running every test) never requires it.
"""

import json

# Nominal baud for USB-Serial-JTAG CDC (M4 §9.4; CDC ignores baud).
PET_RX_BAUD = 115200


def ensure_line(obj_or_str):
    """Frame one event as an NDJSON line (bytes, single trailing "\n")."""
    if isinstance(obj_or_str, bytes):
        return obj_or_str.rstrip(b"\r\n") + b"\n"
    if isinstance(obj_or_str, str):
        return (obj_or_str.rstrip("\r\n") + "\n").encode("utf-8")
    if isinstance(obj_or_str, dict):
        # ensure_ascii=False: §6.1 allows only the \" \\ \/ \n \t escapes,
        # not \uXXXX, so non-ASCII goes out as raw UTF-8.
        return (json.dumps(obj_or_str, ensure_ascii=False, separators=(",", ":"))
                + "\n").encode("utf-8")
    raise TypeError(
        "ensure_line expects bytes, str or dict, got %s" % type(obj_or_str).__name__)


class SerialSink:
    """Callable sink writing framed lines to a serial port (M4 §9.3).

    serial_obj injects a serial-like object (write/close) for no-hardware
    tests; when omitted, pyserial is imported and the port opened lazily
    on first use.
    """

    def __init__(self, port, baud=PET_RX_BAUD, serial_obj=None):
        self.port = port
        self.baud = baud
        self._serial = serial_obj

    def _ensure_serial(self):
        if self._serial is None:
            # Lazy pyserial import: only the real-serial path needs it.
            import serial
            self._serial = serial.Serial(self.port, self.baud)
        return self._serial

    def __call__(self, obj_or_str):
        line = ensure_line(obj_or_str)
        self._ensure_serial().write(line)

    def close(self):
        if self._serial is not None:
            self._serial.close()
