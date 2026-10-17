"""M6 increment 2: serial daemon tests (contract M6 §8.3).

Covers BTN-05 host side: run() reads lines from an injected fake serial
(no real port, no pyserial), parses control lines via pet_devio, and
dispatches `back_window` to the injected focus callable. The CLI main()
is driven through an injected serial_factory.
"""

import contextlib
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import pet_daemon


class FakeSerial:
    """readline() yields the queued bytes then b"" forever; counts served."""

    def __init__(self, lines=()):
        self._lines = list(lines)
        self.served = 0
        self.closed = False

    def readline(self):
        self.served += 1
        if self._lines:
            return self._lines.pop(0)
        return b""

    def close(self):
        self.closed = True


class ExhaustingSerial(FakeSerial):
    """Like FakeSerial but raises KeyboardInterrupt when exhausted."""

    def readline(self):
        if self._lines:
            self.served += 1
            return self._lines.pop(0)
        raise KeyboardInterrupt


class RunDispatch(unittest.TestCase):
    def test_back_window_line_calls_focus_once(self):
        fake = FakeSerial([
            b"I (123) tag: boot\n",                    # log line -> ignored
            b'{"v":1,"cmd":"back_window"}\n',          # control line -> focus
            b"garbage without brace\n",                # junk -> ignored
        ])
        calls = []

        with contextlib.redirect_stderr(io.StringIO()) as err:
            rc = pet_daemon.run(
                serial_obj=fake,
                focus=lambda: calls.append(1) or True,
                stop_after=3,
            )

        self.assertEqual(rc, 0)
        self.assertEqual(len(calls), 1)
        self.assertEqual(fake.served, 3)
        self.assertIn("back_window", err.getvalue())

    def test_no_command_line_focus_not_called(self):
        fake = FakeSerial([
            b"I (100) tag: ready\n",
            b"plain log\n",
            b'{"v":1,"cmd":"other"}\n',
        ])
        calls = []

        with contextlib.redirect_stderr(io.StringIO()):
            rc = pet_daemon.run(
                serial_obj=fake,
                focus=lambda: calls.append(1) or True,
                stop_after=3,
            )

        self.assertEqual(rc, 0)
        self.assertEqual(calls, [])
        self.assertEqual(fake.served, 3)

    def test_timeout_lines_do_not_count_or_dispatch(self):
        fake = FakeSerial([b'{"v":1,"cmd":"back_window"}\n'])
        calls = []

        with contextlib.redirect_stderr(io.StringIO()):
            rc = pet_daemon.run(
                serial_obj=fake,
                focus=lambda: calls.append(1) or True,
                stop_after=1,
            )

        self.assertEqual(rc, 0)
        self.assertEqual(calls, [1])
        # stop_after reached; the fake served exactly one line
        self.assertEqual(fake.served, 1)

    def test_opened_serial_closed_injected_not(self):
        fake = FakeSerial([b"log\n"])
        with contextlib.redirect_stderr(io.StringIO()):
            pet_daemon.run(serial_obj=fake, focus=lambda: True, stop_after=1)
        self.assertFalse(fake.closed)


class MainCli(unittest.TestCase):
    def test_factory_wiring_port_baud_and_dispatch(self):
        captured = {}

        def factory(port, baud):
            captured["port"] = port
            captured["baud"] = baud
            return ExhaustingSerial([b'{"v":1,"cmd":"back_window"}\n'])

        with contextlib.redirect_stderr(io.StringIO()) as err:
            with self.assertRaises(KeyboardInterrupt):
                pet_daemon.main(
                    ["--port", "COM7", "--baud", "9600"],
                    serial_factory=factory,
                )

        self.assertEqual(captured, {"port": "COM7", "baud": 9600})
        # default focus ran (Linux guard -> False); dispatch path was reached
        self.assertIn("back_window", err.getvalue())

    def test_defaults(self):
        captured = {}

        def factory(port, baud):
            captured["port"] = port
            captured["baud"] = baud
            return ExhaustingSerial([])

        with contextlib.redirect_stderr(io.StringIO()):
            with self.assertRaises(KeyboardInterrupt):
                pet_daemon.main([], serial_factory=factory)

        self.assertEqual(captured, {"port": "COM5", "baud": 115200})


if __name__ == "__main__":
    unittest.main()
