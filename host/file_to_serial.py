"""T entry: Windows-side bridge, log file -> serial port (repo copy of
D:\\buddy_flash\\file_to_serial.py).

Tails an NDJSON event log produced by oc_poll.py and writes every new
byte to the serial port, so a poller crash/restart on the WSL side never
touches the device directly. Position tracking survives truncation and
rotation: if the file shrinks below the stored position, reading resets
to 0.

stdlib-only; pyserial is imported lazily only on the real-serial path,
so importing this module and running the tests never requires it.
"""

import argparse
import os
import sys
import time

# Nominal baud only: USB-Serial-JTAG CDC ignores it (M4 §9.4).
BAUD = 115200


def tail(path, pos):
    """New bytes at/after pos as (data, new_pos).

    Missing file -> no bytes, position unchanged. Truncation or rotation
    (size < pos) -> reset position to 0 and read what is there now.
    """
    try:
        size = os.path.getsize(path)
    except OSError:
        return b"", pos
    if size < pos:
        pos = 0
    if size <= pos:
        return b"", pos
    with open(path, "rb") as f:
        f.seek(pos)
        data = f.read()
    return data, pos + len(data)


def _open_serial(port):
    import serial
    return serial.Serial(port, BAUD)


def build_parser():
    parser = argparse.ArgumentParser(
        prog="file_to_serial",
        description="Tail an NDJSON event log and write new bytes to serial.",
    )
    parser.add_argument("--log", required=True,
                        help="NDJSON event log to tail")
    parser.add_argument("--port", default="COM5",
                        help="serial port (default: %(default)s)")
    parser.add_argument("--interval", type=float, default=0.5,
                        help="tail cadence seconds (default: %(default)s)")
    parser.add_argument("--stop-file", default="",
                        help="exit when this file appears (supervisor shutdown)")
    return parser


def main(argv=None, serial_factory=None, sleep_fn=time.sleep,
         stop_after=None, clock=time.monotonic):
    args = build_parser().parse_args(argv)
    print("[file_to_serial] pid=%d log=%s port=%s interval=%ss"
          % (os.getpid(), args.log, args.port, args.interval), flush=True)
    # Skip history: only bytes arriving after startup go to the device.
    try:
        pos = os.path.getsize(args.log)
    except OSError:
        pos = 0
    ser = None
    if serial_factory is None:
        serial_factory = _open_serial
    start = clock()
    try:
        while stop_after is None or clock() - start < stop_after:
            if args.stop_file and os.path.exists(args.stop_file):
                break  # cooperative shutdown (Windows child cannot be killed via SIGTERM)
            data, pos = tail(args.log, pos)
            if data:
                if ser is None:
                    ser = serial_factory(args.port)
                ser.write(data)
            sleep_fn(args.interval)
    except KeyboardInterrupt:
        pass  # clean SIGINT exit
    finally:
        if ser is not None:
            close = getattr(ser, "close", None)
            if close is not None:
                close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
