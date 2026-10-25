"""双通道事件 tests (T entry): poller dual sink (file + WiFi TCP), console
IP discovery in the bridge, supervisor child args.

TDD RED→GREEN per evidence/2026-10-24-双通道事件-T.md. All logic is
stdlib-only and testable without hardware: TCP is exercised against a
real loopback listener socket, the serial console is a fake object with
readline/write, and the poller TCP path takes an injected tcp_send.
No test touches COM5 or a real device.
"""

import contextlib
import io
import json
import os
import socket
import sqlite3
import sys
import tempfile
import threading
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "host"))

import file_to_serial  # noqa: E402
import oc_poll  # noqa: E402
import pet_supervisor  # noqa: E402

OC_SCHEMA = """
create table session (
    id text primary key, title text, directory text,
    time_updated real, parent_id text
);
create table message (
    session_id text, time_updated real, data text
);
"""

WIN_PY = "D:\\Espressif\\python_env\\idf5.5_py3.14_env\\Scripts\\python.exe"


def tempdir():
    return tempfile.mkdtemp(prefix="buddy-dual-test-")


def wfile(path, data, mode=None):
    if mode is None:
        mode = "wb" if isinstance(data, bytes) else "w"
    with open(path, mode) as f:
        f.write(data)


def rfile(path, mode="r"):
    with open(path, mode) as f:
        return f.read()


def make_db():
    """One session whose newest assistant message is busy -> task.start."""
    fd, db = tempfile.mkstemp(suffix=".db")
    os.close(fd)
    con = sqlite3.connect(db)
    con.executescript(OC_SCHEMA)
    con.execute("insert into session values ('s1','t','/p',1,null)")
    con.execute("insert into message values ('s1',1,?)",
                (json.dumps({"role": "assistant", "time": {"created": 1}}),))
    con.commit()
    con.close()
    return db


def loopback_listener():
    """Listening socket on 127.0.0.1 ephemeral port -> (sock, port)."""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(1)
    srv.settimeout(3.0)
    return srv, srv.getsockname()[1]


# ---------------------------------------------------------------------------
# oc_poll: TCP target resolution
# ---------------------------------------------------------------------------


class TestResolveTarget(unittest.TestCase):
    def test_literal_host_port_passthrough(self):
        self.assertEqual(
            oc_poll.resolve_tcp_target("192.168.1.9:9000", "/nonexistent.ip"),
            "192.168.1.9:9000")

    def test_auto_reads_first_nonempty_line(self):
        d = tempdir()
        ipf = os.path.join(d, "x.ip")
        wfile(ipf, "\n192.168.31.198\n")
        self.assertEqual(oc_poll.resolve_tcp_target("auto", ipf),
                         "192.168.31.198:8766")

    def test_auto_missing_ip_file_is_none(self):
        self.assertIsNone(
            oc_poll.resolve_tcp_target("auto", "/nonexistent.ip"))

    def test_auto_blank_file_is_none(self):
        d = tempdir()
        ipf = os.path.join(d, "x.ip")
        wfile(ipf, "\n \n")
        self.assertIsNone(oc_poll.resolve_tcp_target("auto", ipf))

    def test_no_tcp_flag_is_none_even_with_ip_file(self):
        d = tempdir()
        ipf = os.path.join(d, "x.ip")
        wfile(ipf, "10.0.0.5\n")
        self.assertIsNone(oc_poll.resolve_tcp_target(None, ipf))


# ---------------------------------------------------------------------------
# oc_poll: DualSink (file + best-effort TCP, injectable tcp_send)
# ---------------------------------------------------------------------------


class TestDualSink(unittest.TestCase):
    def test_writes_file_and_calls_injected_tcp_send(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        got = []
        sink = oc_poll.DualSink(out, tcp="10.1.2.3:8766",
                                tcp_send=lambda t, l: got.append((t, l)))
        sink({"version": 1, "event": "device.heartbeat"})
        sink.close()
        self.assertEqual(got,
                         [("10.1.2.3:8766", b'{"version":1,"event":"device.heartbeat"}\n')])
        self.assertEqual(rfile(out, "rb"),
                         b'{"version":1,"event":"device.heartbeat"}\n')

    def test_tcp_failure_does_not_raise_file_still_written(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")

        def boom(target, line):
            raise RuntimeError("connection refused")

        sink = oc_poll.DualSink(out, tcp="10.1.2.3:8766", tcp_send=boom)
        sink({"event": "task.start"})  # must not raise
        sink.close()
        self.assertEqual(rfile(out, "rb"), b'{"event":"task.start"}\n')

    def test_no_target_skips_send(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        got = []
        sink = oc_poll.DualSink(out, tcp=None,
                                tcp_send=lambda t, l: got.append((t, l)))
        sink({"event": "task.start"})
        sink.close()
        self.assertEqual(got, [])
        self.assertEqual(rfile(out, "rb"), b'{"event":"task.start"}\n')

    def test_auto_missing_ip_file_skips_send(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        got = []
        sink = oc_poll.DualSink(out, tcp="auto",
                                ip_file=os.path.join(d, "missing.ip"),
                                tcp_send=lambda t, l: got.append((t, l)))
        sink({"event": "task.start"})
        sink.close()
        self.assertEqual(got, [])
        self.assertEqual(rfile(out, "rb"), b'{"event":"task.start"}\n')

    def test_auto_with_ip_file_calls_send(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        ipf = os.path.join(d, "x.ip")
        wfile(ipf, "192.168.31.198\n")
        got = []
        sink = oc_poll.DualSink(out, tcp="auto", ip_file=ipf,
                                tcp_send=lambda t, l: got.append((t, l)))
        sink({"event": "task.start"})
        sink.close()
        self.assertEqual(got, [("192.168.31.198:8766", b'{"event":"task.start"}\n')])

    def test_default_send_over_real_loopback(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        srv, port = loopback_listener()
        try:
            sink = oc_poll.DualSink(out, tcp="127.0.0.1:%d" % port)
            sink({"event": "task.start"})
            sink.close()
            conn, _ = srv.accept()
            conn.settimeout(3.0)
            data = conn.recv(4096)
            conn.close()
        finally:
            srv.close()
        self.assertEqual(data, b'{"event":"task.start"}\n')
        self.assertEqual(rfile(out, "rb"), b'{"event":"task.start"}\n')

    def test_failed_default_send_warns_rate_limited(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        now = [100.0]
        sink = oc_poll.DualSink(out, tcp="127.0.0.1:1",  # closed port
                                clock=lambda: now[0])
        buf = io.StringIO()
        with contextlib.redirect_stderr(buf):
            sink({"event": "a"})  # refused -> warn
            now[0] += 1.0
            sink({"event": "b"})  # within window -> silent
            now[0] += 60.0
            sink({"event": "c"})  # window passed -> warn again
        sink.close()
        warns = [ln for ln in buf.getvalue().splitlines() if "tcp" in ln]
        self.assertEqual(len(warns), 2)
        self.assertIn("127.0.0.1:1", warns[0])

    def test_file_write_error_propagates(self):
        # USB path stays first-class: a real file failure is not swallowed.
        d = tempdir()
        blocker = os.path.join(d, "adir")
        os.mkdir(blocker)  # open(<dir>, "a") raises IsADirectoryError
        self.assertRaises(OSError, oc_poll.DualSink, blocker)


# ---------------------------------------------------------------------------
# oc_poll: main() dual path via CLI flags
# ---------------------------------------------------------------------------


class TestOcPollMainDual(unittest.TestCase):
    def _once(self, argv_extra):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        db = make_db()
        with contextlib.redirect_stderr(io.StringIO()):
            rc = oc_poll.main(["--db", db, "--out", out, "--once"]
                              + argv_extra)
        self.assertEqual(rc, 0)
        return out, rfile(out, "rb")

    def test_ip_file_defaults_to_out_plus_ip(self):
        args = oc_poll.build_parser().parse_args(["--out", "/x/y.log"])
        self.assertIsNone(args.ip_file)  # main applies <out>.ip
        self.assertIsNone(args.tcp)

    def test_main_once_literal_tcp_reaches_loopback(self):
        srv, port = loopback_listener()
        try:
            out, data = self._once(["--tcp", "127.0.0.1:%d" % port])
            conn, _ = srv.accept()
            conn.settimeout(3.0)
            wire = conn.recv(4096)
            conn.close()
        finally:
            srv.close()
        self.assertEqual(data, wire)
        self.assertIn(b'"event":"task.start"', wire)

    def test_main_once_auto_uses_ip_file(self):
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            srv.bind(("127.0.0.1", 8766))  # device default TCP port
        except OSError:
            srv.close()
            self.skipTest("tcp port 8766 busy")
        srv.listen(1)
        srv.settimeout(3.0)
        try:
            d = tempdir()
            wfile(os.path.join(d, "oc_events.log.ip"), "127.0.0.1\n")
            db = make_db()
            out = os.path.join(d, "oc_events.log")
            with contextlib.redirect_stderr(io.StringIO()):
                rc = oc_poll.main(["--db", db, "--out", out, "--once",
                                   "--tcp", "auto",
                                   "--ip-file", out + ".ip"])
            self.assertEqual(rc, 0)
            conn, _ = srv.accept()
            conn.settimeout(3.0)
            wire = conn.recv(4096)
            conn.close()
            self.assertIn(b'"event":"task.start"', wire)
            self.assertEqual(rfile(out, "rb"), wire)
        finally:
            srv.close()

    def test_main_once_without_tcp_writes_file_only(self):
        d = tempdir()
        wfile(os.path.join(d, "oc_events.log.ip"), "10.255.255.1\n")
        srv, port = loopback_listener()
        try:
            srv.settimeout(0.5)
            out, data = self._once(["--ip-file",
                                    os.path.join(d, "oc_events.log.ip")])
            self.assertRaises(socket.timeout, srv.accept)
        finally:
            srv.close()
        self.assertIn(b'"event":"task.start"', data)


# ---------------------------------------------------------------------------
# file_to_serial: console reader discovers the wifi IP
# ---------------------------------------------------------------------------


class FakeConsole:
    """Fake serial with queued console lines; feeds the reader thread."""

    def __init__(self, lines, stop_event):
        self._lines = list(lines)
        self._stop = stop_event
        self.written = []

    def readline(self):
        if self._lines:
            return self._lines.pop(0)
        self._stop.set()  # nothing left: let the reader exit
        return b""

    def write(self, b):
        self.written.append(b)

    def close(self):
        pass


class TestConsoleReader(unittest.TestCase):
    def test_reader_writes_ip_file_from_wifi_line(self):
        d = tempdir()
        ipf = os.path.join(d, "x.ip")
        stop = threading.Event()
        ser = FakeConsole([b"boot ok\r\n",
                           b"net: wifi connected ip=10.0.0.5\r\n",
                           b"other line\r\n"], stop)
        file_to_serial.console_reader(ser, ipf, stop)
        self.assertEqual(rfile(ipf).strip(), "10.0.0.5")
        leftovers = [n for n in os.listdir(d) if n.endswith(".tmp")]
        self.assertEqual(leftovers, [])  # atomic: tmp+rename, no tmp left

    def test_reader_ignores_other_lines(self):
        d = tempdir()
        ipf = os.path.join(d, "x.ip")
        stop = threading.Event()
        ser = FakeConsole([b"boot ok\r\n", b"net: wifi connecting...\r\n"], stop)
        file_to_serial.console_reader(ser, ipf, stop)
        self.assertFalse(os.path.exists(ipf))

    def test_reader_rewrites_on_new_ip(self):
        d = tempdir()
        ipf = os.path.join(d, "x.ip")
        stop = threading.Event()
        ser = FakeConsole([b"net: wifi connected ip=10.0.0.5\r\n",
                           b"net: wifi connected ip=10.0.0.9\r\n"], stop)
        file_to_serial.console_reader(ser, ipf, stop)
        self.assertEqual(rfile(ipf).strip(), "10.0.0.9")

    def test_main_starts_reader_and_keeps_write_path(self):
        d = tempdir()
        log = os.path.join(d, "x.log")
        ipf = os.path.join(d, "dev.ip")
        wfile(log, b'{"event":"a"}\n')  # history: skipped
        ser = FakeConsole([], threading.Event())
        state = {"fed": False}

        def fake_sleep(s):
            if not state["fed"]:
                state["fed"] = True
                ser._lines.append(b"net: wifi connected ip=10.0.0.5\r\n")
                wfile(log, b'{"event":"b"}\n', mode="ab")

        rc = file_to_serial.main(
            ["--log", log, "--ip-file", ipf, "--interval", "0.01"],
            serial_factory=lambda p: ser,
            sleep_fn=fake_sleep,
            stop_after=0.3)
        self.assertEqual(rc, 0)
        self.assertEqual(rfile(ipf).strip(), "10.0.0.5")
        self.assertIn(b'{"event":"b"}\n', ser.written)

    def test_main_ip_file_defaults_to_log_plus_ip(self):
        args = file_to_serial.build_parser().parse_args(["--log", "/x/y.log"])
        self.assertIsNone(args.ip_file)  # main applies <log>.ip


# ---------------------------------------------------------------------------
# pet_supervisor: build_children dual-transport args
# ---------------------------------------------------------------------------


class TestBuildChildrenDualTransport(unittest.TestCase):
    def test_poller_gets_tcp_auto_and_ip_file(self):
        kids = pet_supervisor.build_children(
            out="/mnt/d/buddy_flash/oc_events.log",
            win_python=WIN_PY, host_dir="/repo/host")
        self.assertEqual(kids[0][4:8], ["--tcp", "auto", "--ip-file",
                                        "/mnt/d/buddy_flash/oc_events.log.ip"])

    def test_bridge_gets_ip_file(self):
        kids = pet_supervisor.build_children(
            out="/mnt/d/buddy_flash/oc_events.log",
            win_python=WIN_PY, host_dir="/repo/host")
        self.assertIn("--ip-file 'D:\\buddy_flash\\oc_events.log.ip'",
                      kids[1][3])


if __name__ == "__main__":
    unittest.main()
