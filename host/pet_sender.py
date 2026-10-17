"""M4 increment 2b: sender CLI (contract M4 §9.3).

Reads one JSON event from argv (or one JSON object per line from stdin
with --stdin), frames it as a device NDJSON line and writes it to the
serial port (default COM5, USB-Serial-JTAG CDC). One ack line per send
on stderr. open_sink is injectable for no-hardware tests.
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pet_transport
from pet_transport import SerialSink, ensure_line

DEFAULT_PORT = "COM5"  # §9.3: device sits on COM5 (USB-Serial-JTAG)


def build_parser():
    parser = argparse.ArgumentParser(
        prog="pet_sender",
        description="Send one JSON event (or --stdin lines) to the device serial port.",
    )
    parser.add_argument("--port", default=DEFAULT_PORT,
                        help="serial port (default: %(default)s)")
    parser.add_argument("--baud", type=int, default=pet_transport.PET_RX_BAUD,
                        help="baud rate (default: %(default)s; CDC ignores it)")
    parser.add_argument("--stdin", action="store_true",
                        help="read one JSON object per line from stdin")
    parser.add_argument("event", nargs="?", help="JSON event object as a string")
    return parser


def _send(sink, raw, label):
    """Validate, frame and send one raw JSON text. Returns 0 or 1."""
    try:
        obj = json.loads(raw)
    except ValueError as exc:
        print("%s: invalid JSON: %s" % (label, exc), file=sys.stderr)
        return 1
    if not isinstance(obj, dict):
        print("%s: not a JSON object" % label, file=sys.stderr)
        return 1
    line = ensure_line(obj)
    sink(line)
    print("sent %d bytes" % len(line), file=sys.stderr)
    return 0


def main(argv=None, open_sink=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.stdin and args.event is not None:
        parser.error("give either a JSON argument or --stdin, not both")
    if not args.stdin and args.event is None:
        parser.error("give a JSON argument or --stdin")

    if open_sink is None:
        # Real path: SerialSink(port, baud); pyserial is imported lazily
        # inside it on first use, so --help / argument errors never need it.
        open_sink = SerialSink
    sink = open_sink(args.port, args.baud)
    rc = 0
    try:
        if args.stdin:
            for i, raw in enumerate(sys.stdin, 1):
                raw = raw.strip()
                if not raw:
                    continue
                rc |= _send(sink, raw, "line %d" % i)
        else:
            rc = _send(sink, args.event, "event")
    finally:
        sink.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
