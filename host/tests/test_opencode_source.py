"""M4 increment 2c (P1-2a): OpenCode SQLite read-only poller tests.

Covers E2E-01 (host side, per evidence/M4/2026-10-17-OpenCode真源-spike.md):
state derivation from the newest assistant message, top-level session
pick, malformed-JSON tolerance, the busy/idle/error edge machine,
heartbeat cadence, and the --once CLI driven via injected source/sink
factories. Every test runs against a temp SQLite DB; the real OpenCode
DB is never opened and pyserial is never imported.
"""

import contextlib
import json
import os
import sqlite3
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import opencode_source

SCHEMA = """
create table session (
    id text primary key,
    title text,
    directory text,
    time_updated real,
    parent_id text
);
create table message (
    session_id text,
    time_updated real,
    data text
);
"""

ASSISTANT_BUSY = {"role": "assistant", "time": {"created": 1}}
ASSISTANT_DONE = {"role": "assistant", "time": {"created": 1, "completed": 2}}
ASSISTANT_ERR = {"role": "assistant", "time": {"created": 1, "completed": 2},
                 "error": {"code": "x", "message": "boom"}}


@contextlib.contextmanager
def temp_db():
    """Yield (connection, path) of a throwaway DB with the minimal schema."""
    fd, path = tempfile.mkstemp(suffix=".db")
    os.close(fd)
    con = sqlite3.connect(path)
    try:
        con.executescript(SCHEMA)
        yield con, path
    finally:
        con.close()
        os.unlink(path)


def add_session(con, sid, title, directory, updated, parent=None):
    con.execute("insert into session values (?,?,?,?,?)",
                (sid, title, directory, updated, parent))
    con.commit()


def add_message(con, sid, updated, payload):
    raw = payload if isinstance(payload, str) else json.dumps(payload)
    con.execute("insert into message values (?,?,?)", (sid, updated, raw))
    con.commit()


class DeriveState(unittest.TestCase):
    def test_no_msg_is_idle(self):
        self.assertEqual(opencode_source.derive_state(None), "idle")

    def test_incomplete_time_is_busy(self):
        self.assertEqual(opencode_source.derive_state(ASSISTANT_BUSY), "busy")

    def test_completed_without_error_is_idle(self):
        self.assertEqual(opencode_source.derive_state(ASSISTANT_DONE), "idle")

    def test_completed_with_error_is_error(self):
        self.assertEqual(opencode_source.derive_state(ASSISTANT_ERR), "error")


class LatestSession(unittest.TestCase):
    def test_newest_top_level_wins_child_ignored(self):
        with temp_db() as (con, _):
            add_session(con, "s1", "Old", "/a/b", 10)
            add_session(con, "s2", "New", "/a/c", 20)
            add_session(con, "s3", "Child", "/a/d", 30, parent="s1")
            self.assertEqual(
                opencode_source.latest_session(con),
                {"id": "s2", "title": "New", "directory": "/a/c"})

    def test_no_session_returns_none(self):
        with temp_db() as (con, _):
            self.assertIsNone(opencode_source.latest_session(con))


class LatestAssistant(unittest.TestCase):
    def test_user_and_malformed_rows_skipped(self):
        with temp_db() as (con, _):
            add_message(con, "s1", 10, ASSISTANT_BUSY)
            add_message(con, "s1", 11, "{not json")  # malformed JSON
            add_message(con, "s1", 12, {"role": "user", "time": {}})
            self.assertEqual(
                opencode_source.latest_assistant(con, "s1"), ASSISTANT_BUSY)

    def test_no_assistant_returns_none(self):
        with temp_db() as (con, _):
            add_message(con, "s1", 10, {"role": "user", "time": {}})
            self.assertIsNone(opencode_source.latest_assistant(con, "s1"))


class PollTransitions(unittest.TestCase):
    def test_busy_done_error_idle_edge_sequence(self):
        with temp_db() as (con, _):
            add_session(con, "s1", "Fix login", "/home/me/ESP32-buddy/", 100)
            add_message(con, "s1", 1, ASSISTANT_BUSY)
            src = opencode_source.OpenCodeSource("unused.db", con=con)
            # busy edge -> task.start (project strips the trailing "/")
            self.assertEqual(src.poll(), {
                "version": 1, "event": "task.start",
                "title": "Fix login", "project": "ESP32-buddy"})
            # still busy -> no event
            self.assertIsNone(src.poll())
            # busy -> completed -> task.done
            add_message(con, "s1", 2, ASSISTANT_DONE)
            self.assertEqual(src.poll(), {
                "version": 1, "event": "task.done",
                "title": "Fix login", "project": "ESP32-buddy"})
            # still idle -> no event
            self.assertIsNone(src.poll())
            # idle -> error -> task.error
            add_message(con, "s1", 3, ASSISTANT_ERR)
            self.assertEqual(src.poll(), {
                "version": 1, "event": "task.error",
                "title": "Fix login", "project": "ESP32-buddy"})
            # error -> completed -> agent.idle (no title/project)
            add_message(con, "s1", 4, ASSISTANT_DONE)
            self.assertEqual(src.poll(), {"version": 1, "event": "agent.idle"})

    def test_no_session_polls_none(self):
        with temp_db() as (con, _):
            src = opencode_source.OpenCodeSource("unused.db", con=con)
            self.assertIsNone(src.poll())


class HeartbeatDue(unittest.TestCase):
    def test_never_sent_is_due(self):
        self.assertTrue(opencode_source.heartbeat_due(None, 100))

    def test_within_interval_not_due(self):
        self.assertFalse(opencode_source.heartbeat_due(100, 103, 5))

    def test_interval_elapsed_is_due(self):
        self.assertTrue(opencode_source.heartbeat_due(100, 106, 5))


class MainOnce(unittest.TestCase):
    def test_once_polls_sends_line_returns_zero(self):
        with temp_db() as (con, path):
            add_session(con, "s1", "Fix login", "/home/me/ESP32-buddy/", 100)
            add_message(con, "s1", 1, ASSISTANT_BUSY)
            lines = []
            seen = {}

            def source_factory(db):
                seen["db"] = db
                return opencode_source.OpenCodeSource(db, con=con)

            def sink_factory(port, baud):
                seen["port"] = port
                return lines.append

            rc = opencode_source.main(
                ["--once", "--db", path, "--port", "COM7"],
                source_factory=source_factory, sink_factory=sink_factory)

            self.assertEqual(rc, 0)
            self.assertEqual(seen, {"db": path, "port": "COM7"})
            self.assertEqual(lines, [{
                "version": 1, "event": "task.start",
                "title": "Fix login", "project": "ESP32-buddy"}])


if __name__ == "__main__":
    unittest.main()
