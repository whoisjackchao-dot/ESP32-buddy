"""M4 increment 2c (P1-2a): OpenCode SQLite read-only poller.

Polls the local OpenCode DB (read-only, spike-tested concurrent with a
running OpenCode, see evidence/M4/2026-10-17-OpenCode真源-spike.md) and
derives busy/idle/error from the newest assistant message; the host
records the edges and emits device event lines (M4 §6.1) via a
pet_transport sink:

- busy edge          -> task.start (title/project from latest session)
- error edge         -> task.error
- busy -> idle edge  -> task.done
- error -> idle edge -> agent.idle
- no edge            -> nothing (device.heartbeat is sent on a cadence)

pyserial is never imported at module level: the default sink is
pet_transport.SerialSink, which imports serial lazily on first use.
"""

import argparse
import json
import os
import sqlite3
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pet_transport
from pet_transport import SerialSink

DEFAULT_DB = os.path.expanduser("~/.local/share/opencode/opencode.db")
DEFAULT_PORT = "COM5"  # §9.3: device sits on COM5 (USB-Serial-JTAG)


def connect_ro(path=DEFAULT_DB):
    """Open the OpenCode DB read-only (safe next to a running OpenCode)."""
    return sqlite3.connect("file:%s?mode=ro" % path, uri=True)


def latest_session(con):
    """Newest top-level session as {"id","title","directory"}, or None."""
    row = con.execute(
        "select id,title,directory from session"
        " where parent_id is null order by time_updated desc limit 1"
    ).fetchone()
    if row is None:
        return None
    return {"id": row[0], "title": row[1], "directory": row[2]}


def latest_assistant(con, session_id):
    """Newest parseable assistant message dict for the session, or None.

    Some data rows are malformed JSON, so walk newest-first and skip
    rows that fail json.loads or are not an assistant object.
    """
    rows = con.execute(
        "select data from message where session_id=? order by time_updated desc",
        (session_id,),
    )
    for (raw,) in rows:
        try:
            msg = json.loads(raw)
        except (TypeError, ValueError):
            continue
        if isinstance(msg, dict) and msg.get("role") == "assistant":
            return msg
    return None


def derive_state(msg):
    """Map the newest assistant message to busy/error/idle (spike §3)."""
    if msg is None:
        return "idle"
    if "completed" not in msg.get("time", {}):
        return "busy"
    if msg.get("error"):
        return "error"
    return "idle"


class OpenCodeSource:
    """Edge detector over the OpenCode DB: poll() -> event dict or None."""

    def __init__(self, db_path=DEFAULT_DB, con=None):
        self.db_path = db_path
        self._con = con
        self.prev = None

    def _connect(self):
        if self._con is None:
            self._con = connect_ro(self.db_path)
        return self._con

    @staticmethod
    def _with_ctx(event, session):
        return {
            "version": 1,
            "event": event,
            "title": session["title"],
            "project": os.path.basename(session["directory"].rstrip("/")),
        }

    def _edge_event(self, state, session):
        """Event for a state change (called only when state != prev)."""
        if state == "busy":
            return self._with_ctx("task.start", session)
        if state == "error":
            return self._with_ctx("task.error", session)
        if self.prev == "busy":
            return self._with_ctx("task.done", session)
        if self.prev == "error":
            return {"version": 1, "event": "agent.idle"}
        return None

    def poll(self):
        con = self._connect()
        session = latest_session(con)
        if session is None:
            return None
        state = derive_state(latest_assistant(con, session["id"]))
        event = None
        if state != self.prev:
            event = self._edge_event(state, session)
        self.prev = state
        return event


def heartbeat_due(last_sent_ts, now_ts, interval=5.0):
    """Pure cadence check: never sent, or interval elapsed since last."""
    if last_sent_ts is None:
        return True
    return now_ts - last_sent_ts >= interval


def build_parser():
    parser = argparse.ArgumentParser(
        prog="opencode_source",
        description=("Poll the local OpenCode DB (read-only) and push"
                     " device event lines to the serial port."),
    )
    parser.add_argument("--db", default=DEFAULT_DB,
                        help="OpenCode SQLite path (default: %(default)s)")
    parser.add_argument("--port", default=DEFAULT_PORT,
                        help="serial port (default: %(default)s)")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="poll cadence seconds (default: %(default)s)")
    parser.add_argument("--heartbeat", type=float, default=5.0,
                        help="heartbeat cadence seconds (default: %(default)s)")
    parser.add_argument("--once", action="store_true",
                        help="poll once and exit")
    return parser


def main(argv=None, source_factory=None, sink_factory=None):
    args = build_parser().parse_args(argv)
    if source_factory is None:
        source_factory = OpenCodeSource
    if sink_factory is None:
        sink_factory = SerialSink
    src = source_factory(args.db)
    sink = sink_factory(args.port, pet_transport.PET_RX_BAUD)
    last_beat = None
    try:
        while True:
            event = src.poll()
            if event is not None:
                sink(event)
                print("sent %s" % event.get("event"), file=sys.stderr)
            elif heartbeat_due(last_beat, time.monotonic(), args.heartbeat):
                sink({"version": 1, "event": "device.heartbeat"})
                last_beat = time.monotonic()
            if args.once:
                break
            time.sleep(args.interval)
    finally:
        close = getattr(sink, "close", None)
        if close is not None:
            close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
