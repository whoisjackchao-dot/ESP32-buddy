"""T entry: OpenCode event poller writing NDJSON to a log file.

Wraps opencode_source.main (same poll/edge/heartbeat machinery) but the
sink is a FileSink appending one NDJSON line per event (M4 §6.1 framing,
via pet_transport.ensure_line) instead of a serial port. The Windows
bridge (file_to_serial.py) tails that file into COM5, so the poller
itself never needs pyserial.

The sink stays injectable for tests: main(argv, sink_factory) forwards
to opencode_source.main, whose sink_factory contract is
`sink_factory(port, baud) -> callable(obj_dict)`.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pet_transport
import opencode_source

# Windows-side bridge tails this via D:\buddy_flash (mounted at /mnt/d).
DEFAULT_OUT = "/mnt/d/buddy_flash/oc_events.log"


class FileSink:
    """Callable sink appending NDJSON event lines to a file, flush per line."""

    def __init__(self, path):
        self.path = path
        parent = os.path.dirname(path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        self._f = open(path, "a", encoding="utf-8")

    def __call__(self, obj_or_str):
        self._f.write(pet_transport.ensure_line(obj_or_str).decode("utf-8"))
        self._f.flush()

    def close(self):
        self._f.close()


def make_sink_factory(out_path):
    """sink_factory(port, baud) -> FileSink(out_path); port/baud ignored."""
    def factory(port, baud):
        return FileSink(out_path)
    return factory


def build_parser():
    parser = argparse.ArgumentParser(
        prog="oc_poll",
        description=("Poll the OpenCode DB and append device event lines"
                     " to a log file (tailed by file_to_serial.py)."),
    )
    parser.add_argument("--out", default=DEFAULT_OUT,
                        help="NDJSON event log (default: %(default)s)")
    parser.add_argument("--db", default=opencode_source.DEFAULT_DB,
                        help="OpenCode SQLite path (default: %(default)s)")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="poll cadence seconds (default: %(default)s)")
    parser.add_argument("--heartbeat", type=float, default=5.0,
                        help="heartbeat cadence seconds (default: %(default)s)")
    parser.add_argument("--once", action="store_true",
                        help="poll once and exit")
    return parser


def main(argv=None, sink_factory=None):
    args = build_parser().parse_args(argv)
    if sink_factory is None:
        sink_factory = make_sink_factory(args.out)
    src_argv = ["--db", args.db,
                "--interval", str(args.interval),
                "--heartbeat", str(args.heartbeat)]
    if args.once:
        src_argv.append("--once")
    return opencode_source.main(src_argv, sink_factory=sink_factory)


if __name__ == "__main__":
    sys.exit(main())
