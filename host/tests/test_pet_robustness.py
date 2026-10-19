"""Fault-injection / malformed-input regression tests (B2).

These tests do not require a device or a serial port. They pin the host
side's behavior on inputs that have historically been able to reach the
device line format: invalid UTF-8, control characters, oversized values,
nested JSON, malformed SQLite rows, and junk device lines.
"""

import os
import sqlite3
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import opencode_source
import pet_devio
import pet_ingest
import pet_transport


class IngestFuzz(unittest.TestCase):
    def test_invalid_utf8_never_reaches_device(self):
        r = pet_ingest.ingest_request(
            "POST", "/v1/events", "application/json", b'{"version":1,"event":"task.done","title":"\xff"}')
        self.assertEqual((r.ok, r.code, r.http_status), (False, "JSON", 400))

    def test_nul_and_control_chars_rejected(self):
        for code in (0x00, 0x01, 0x1f, 0x7f):
            body = ('{"version":1,"event":"task.done","title":"a\\u%04xb"}'
                    % code).encode("utf-8")
            r = pet_ingest.ingest_request(
                "POST", "/v1/events", "application/json", body)
            self.assertEqual((r.ok, r.code), (False, "TYPE"), hex(code))

    def test_raw_control_byte_is_invalid_json(self):
        r = pet_ingest.ingest_request(
            "POST", "/v1/events", "application/json",
            b'{"version":1,"event":"task.done","title":"a\x00b"}')
        self.assertEqual((r.ok, r.code), (False, "JSON"))

    def test_huge_elapsed_rejected(self):
        r = pet_ingest.ingest_request(
            "POST", "/v1/events", "application/json",
            b'{"version":1,"event":"task.start","elapsed_s":1000000001}')
        self.assertEqual((r.ok, r.code), (False, "TYPE"))

    def test_deeply_nested_unknown_key_does_not_crash(self):
        nested = "[" * 200 + "]" * 200
        body = ('{"version":1,"event":"task.done","x":%s}' % nested).encode("utf-8")
        r = pet_ingest.ingest_request("POST", "/v1/events", "application/json", body)
        # Unknown keys are ignored; depth is not the host's concern.
        self.assertTrue(r.ok)

    def test_nan_title_rejected_as_type(self):
        r = pet_ingest.ingest_request(
            "POST", "/v1/events", "application/json",
            b'{"version":1,"event":"task.done","title":NaN}')
        self.assertEqual((r.ok, r.code), (False, "TYPE"))

    def test_tasks_item_wrong_type_rejected(self):
        r = pet_ingest.ingest_request(
            "POST", "/v1/events", "application/json",
            b'{"version":1,"event":"task.start","tasks":[{"title":7}]}')
        self.assertEqual((r.ok, r.code), (False, "TYPE"))


class DeviceLineFuzz(unittest.TestCase):
    def test_junk_lines_are_ignored_not_crashing(self):
        lines = [
            "",
            "plain log line",
            "{",
            "[1,2,3]",
            '{"v":1}',
            '{"v":1,"cmd":""}',
            '{"v":true,"cmd":"back_window"}',
            '{"v":1,"cmd":123}',
            '{"v":1,"cmd":"x"}' + "a" * 4096,
        ]
        for line in lines:
            self.assertIsNone(pet_devio.parse_device_line(line), line[:40])

    def test_valid_control_line(self):
        self.assertEqual(
            pet_devio.parse_device_line('{"v":1,"cmd":"back_window"}\n'),
            {"v": 1, "cmd": "back_window"})


class TransportFuzz(unittest.TestCase):
    def test_invalid_type_raises(self):
        for value in (123, ["x"], None):
            with self.assertRaises(TypeError):
                pet_transport.ensure_line(value)

    def test_control_char_dict_is_framed_but_entry_would_reject(self):
        # ensure_line is a low-level framer; the entry layer is responsible
        # for rejecting control characters before framing.
        line = pet_transport.ensure_line({"title": "a\x01b"})
        self.assertIn(b"\\u0001", line)
        self.assertTrue(line.endswith(b"\n"))


class OpenCodeFaults(unittest.TestCase):
    def test_missing_db_raises_operational_error(self):
        with self.assertRaises(sqlite3.OperationalError):
            opencode_source.connect_ro("/nonexistent/opencode.db")

    def test_malformed_rows_are_skipped(self):
        fd, path = tempfile.mkstemp(suffix=".db")
        os.close(fd)
        con = sqlite3.connect(path)
        try:
            con.executescript(
                "create table session (id text primary key, title text,"
                " directory text, time_updated real, parent_id text);"
                "create table message (session_id text, time_updated real, data text);")
            con.execute("insert into session values ('s','T','/p',1,null)")
            con.execute("insert into message values ('s',2,'{not json')")
            con.execute("insert into message values ('s',3,'{\"role\":\"assistant\",\"time\":{\"created\":1}}')")
            con.commit()
            self.assertEqual(
                opencode_source.latest_assistant(con, "s"),
                {"role": "assistant", "time": {"created": 1}})
        finally:
            con.close()
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
