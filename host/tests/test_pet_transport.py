"""M4 increment 2b: host-side transport/sender tests (contract M4 §9.1/§9.3).

Covers E2E-02 (sender side): NDJSON framing via ensure_line and the
SerialSink write path with an injected FakeSerial, so no real port is
opened and pyserial is never required. The pet_sender CLI is driven via
an injected open_sink factory (no hardware, no serial import).
"""

import contextlib
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import pet_sender
import pet_transport


class FakeSerial:
    """Stands in for serial.Serial: collects writes, never touches hardware."""

    def __init__(self):
        self.written = []
        self.closed = False

    def write(self, data):
        self.written.append(bytes(data))
        return len(data)

    def close(self):
        self.closed = True


class EnsureLineStr(unittest.TestCase):
    def test_plain_gets_one_newline(self):
        self.assertEqual(pet_transport.ensure_line('{"a":1}'), b'{"a":1}\n')

    def test_trailing_newline_not_duplicated(self):
        self.assertEqual(pet_transport.ensure_line('{"a":1}\n'), b'{"a":1}\n')

    def test_crlf_normalized_to_single_lf(self):
        self.assertEqual(pet_transport.ensure_line('{"a":1}\r\n'), b'{"a":1}\n')

    def test_multiple_trailing_newlines_collapse_to_one(self):
        self.assertEqual(pet_transport.ensure_line('{"a":1}\n\r\n'), b'{"a":1}\n')

    def test_str_encoded_utf8(self):
        self.assertEqual(
            pet_transport.ensure_line('{"t":"中"}'),
            '{"t":"中"}\n'.encode("utf-8"),
        )


class EnsureLineDict(unittest.TestCase):
    def test_compact_json_plus_newline(self):
        line = pet_transport.ensure_line(
            {"version": 1, "event": "task.done", "title": "Hello"})
        self.assertEqual(line, b'{"version":1,"event":"task.done","title":"Hello"}\n')

    def test_non_ascii_raw_not_escaped(self):
        line = pet_transport.ensure_line(
            {"version": 1, "event": "task.done", "title": "中文"})
        self.assertIn("中文".encode("utf-8"), line)
        self.assertNotIn(b"\\u", line)


class EnsureLineType(unittest.TestCase):
    def test_int_raises_typeerror(self):
        with self.assertRaises(TypeError):
            pet_transport.ensure_line(123)

    def test_list_raises_typeerror(self):
        with self.assertRaises(TypeError):
            pet_transport.ensure_line(["nope"])


class SerialSinkWrites(unittest.TestCase):
    def test_writes_exactly_ensure_line_bytes(self):
        fake = FakeSerial()
        sink = pet_transport.SerialSink("COM5", serial_obj=fake)
        for x in ({"version": 1, "event": "agent.idle"},
                  '{"version":1,"event":"task.done"}'):
            sink(x)
            self.assertEqual(fake.written[-1], pet_transport.ensure_line(x))

    def test_two_sends_append_two_lines(self):
        fake = FakeSerial()
        sink = pet_transport.SerialSink("COM5", serial_obj=fake)
        sink({"version": 1, "event": "task.done"})
        sink('{"version":1,"event":"agent.idle"}')
        self.assertEqual(fake.written, [
            b'{"version":1,"event":"task.done"}\n',
            b'{"version":1,"event":"agent.idle"}\n',
        ])

    def test_close_closes_serial(self):
        fake = FakeSerial()
        sink = pet_transport.SerialSink("COM5", serial_obj=fake)
        sink.close()
        self.assertTrue(fake.closed)

    def test_init_does_not_import_serial(self):
        # Laziness contract (§9.3): constructing a sink without serial_obj
        # must not require pyserial; the import happens only on first use.
        sys.modules.pop("serial", None)
        sink = pet_transport.SerialSink("COM5", baud=115200)
        self.assertIsNotNone(sink)
        self.assertNotIn("serial", sys.modules)


class PetSenderCli(unittest.TestCase):
    def test_main_sends_valid_json_arg(self):
        fake = FakeSerial()
        made = []

        def open_sink(port, baud):
            made.append((port, baud))
            return pet_transport.SerialSink(port, baud, serial_obj=fake)

        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            rc = pet_sender.main(
                ["--port", "COM7", "--baud", "9600",
                 '{"version":1,"event":"task.done","title":"Hello"}'],
                open_sink=open_sink,
            )
        self.assertEqual(rc, 0)
        self.assertEqual(made, [("COM7", 9600)])
        self.assertEqual(
            fake.written,
            [b'{"version":1,"event":"task.done","title":"Hello"}\n'])
        self.assertIn("sent", err.getvalue())


if __name__ == "__main__":
    unittest.main()
