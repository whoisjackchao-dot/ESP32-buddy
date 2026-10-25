"""T entry: OpenCode event poller writing NDJSON to a log file.

Wraps opencode_source.main (same poll/edge/heartbeat machinery) but the
sink is a FileSink appending one NDJSON line per event (M4 §6.1 framing,
via pet_transport.ensure_line) instead of a serial port. The Windows
bridge (file_to_serial.py) tails that file into COM5, so the poller
itself never needs pyserial.

Dual transport (--tcp/--ip-file): the same NDJSON line is additionally
sent best-effort to the device over WiFi TCP. Target resolution:
--tcp HOST:PORT literal; --tcp auto reads the device IP from <ip-file>
(written by the bridge's console reader) and uses port 8766; no --tcp
skips TCP entirely (USB path untouched). A TCP failure only logs a
rate-limited warning - it never crashes the poller nor delays it more
than ~2 s, and dedup is the device's job (same line on both paths).

The sink stays injectable for tests: main(argv, sink_factory) forwards
to opencode_source.main, whose sink_factory contract is
`sink_factory(port, baud) -> callable(obj_dict)`; DualSink's tcp_send
is injectable the same way.
"""

import argparse
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pet_transport
import opencode_source

# Windows-side bridge tails this via D:\buddy_flash (mounted at /mnt/d).
DEFAULT_OUT = "/mnt/d/buddy_flash/oc_events.log"
DEFAULT_TCP_PORT = 8766   # device listens here (NDJSON, one line per conn)
TCP_TIMEOUT = 1.0         # connect/send cap; worst case ~2 s per line
WARN_INTERVAL = 30.0      # rate limit for TCP-failure warnings


class FileSink:
    """Callable sink appending NDJSON event lines to a file, flush per line."""

    def __init__(self, path):
        self.path = path
        parent = os.path.dirname(path)
        if parent:
            os.makedirs(parent, exist_ok=True)
        self._f = open(path, "a", encoding="utf-8")

    def write_line(self, line):
        """Append one already-framed NDJSON line (bytes or str)."""
        self._f.write(line.decode("utf-8")
                      if isinstance(line, bytes) else line)
        self._f.flush()

    def __call__(self, obj_or_str):
        self.write_line(pet_transport.ensure_line(obj_or_str))

    def close(self):
        self._f.close()


def first_nonempty_line(path):
    """First non-empty line of path, or None (missing/unreadable -> None)."""
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    return line
    except OSError:
        pass
    return None


def resolve_tcp_target(tcp_arg, ip_file):
    """TCP target host:port, or None (send nothing).

    tcp_arg is the --tcp value: "HOST:PORT" literal, "auto" (resolve the
    host from ip_file's first non-empty line, port 8766), or None/absent
    (TCP off, USB only).
    """
    if not tcp_arg:
        return None
    if tcp_arg != "auto":
        return tcp_arg
    ip = first_nonempty_line(ip_file) if ip_file else None
    return "%s:%d" % (ip, DEFAULT_TCP_PORT) if ip else None


def tcp_send_line(target, line, timeout=TCP_TIMEOUT):
    """One connect/send/close of line to target ("host:port").

    Raises OSError (or ValueError on a malformed target) on failure.
    """
    host, _, port = target.rpartition(":")
    with socket.create_connection((host, int(port)), timeout=timeout) as s:
        s.settimeout(timeout)
        s.sendall(line)


class DualSink:
    """FileSink (USB path) + best-effort TCP fan-out of the same line.

    tcp_send injects the wire function for tests as tcp_send(target,
    framed_line_bytes); the default is tcp_send_line. Any send failure
    is swallowed with a rate-limited stderr warning (injectable clock),
    after the file write, so the USB path is never disturbed.
    """

    def __init__(self, out_path, tcp=None, ip_file=None, tcp_send=None,
                 clock=time.monotonic):
        self._file = FileSink(out_path)
        self._tcp = tcp
        self._ip_file = ip_file
        self._tcp_send = tcp_send
        self._clock = clock
        self._next_warn = 0.0

    def resolve_target(self):
        return resolve_tcp_target(self._tcp, self._ip_file)

    def __call__(self, obj_or_str):
        line = pet_transport.ensure_line(obj_or_str)
        self._file.write_line(line)
        target = self.resolve_target()
        if target is None:
            return
        try:
            if self._tcp_send is not None:
                self._tcp_send(target, line)
            else:
                tcp_send_line(target, line)
        except Exception as exc:  # best-effort: never crash the poller
            now = self._clock()
            if now >= self._next_warn:
                print("oc_poll: tcp send to %s failed: %s" % (target, exc),
                      file=sys.stderr, flush=True)
                self._next_warn = now + WARN_INTERVAL

    def close(self):
        self._file.close()


def make_sink_factory(out_path, tcp=None, ip_file=None, tcp_send=None):
    """sink_factory(port, baud) -> DualSink(out_path, ...); port/baud ignored."""
    def factory(port, baud):
        return DualSink(out_path, tcp=tcp, ip_file=ip_file, tcp_send=tcp_send)
    return factory


def build_parser():
    parser = argparse.ArgumentParser(
        prog="oc_poll",
        description=("Poll the OpenCode DB and append device event lines"
                     " to a log file (tailed by file_to_serial.py),"
                     " optionally also sending each line over WiFi TCP."),
    )
    parser.add_argument("--out", default=DEFAULT_OUT,
                        help="NDJSON event log (default: %(default)s)")
    parser.add_argument("--db", default=opencode_source.DEFAULT_DB,
                        help="OpenCode SQLite path (default: %(default)s)")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="poll cadence seconds (default: %(default)s)")
    parser.add_argument("--heartbeat", type=float, default=5.0,
                        help="heartbeat cadence seconds (default: %(default)s)")
    parser.add_argument("--tcp", default=None, metavar="HOST:PORT|auto",
                        help="also send each line to the device over TCP;"
                             " 'auto' resolves the host from --ip-file"
                             " (port %d)" % DEFAULT_TCP_PORT)
    parser.add_argument("--ip-file", default=None,
                        help="device IP file for --tcp auto"
                             " (default: <out>.ip)")
    parser.add_argument("--once", action="store_true",
                        help="poll once and exit")
    return parser


def main(argv=None, sink_factory=None):
    args = build_parser().parse_args(argv)
    if sink_factory is None:
        sink_factory = make_sink_factory(
            args.out, tcp=args.tcp,
            ip_file=args.ip_file if args.ip_file else args.out + ".ip")
    src_argv = ["--db", args.db,
                "--interval", str(args.interval),
                "--heartbeat", str(args.heartbeat)]
    if args.once:
        src_argv.append("--once")
    return opencode_source.main(src_argv, sink_factory=sink_factory)


if __name__ == "__main__":
    sys.exit(main())
