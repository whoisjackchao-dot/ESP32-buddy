"""WiFi control channel (device -> host) tests (T entry).

TDD RED→GREEN per evidence/2026-10-25-WiFi控制通道-T.md. The device
additionally mirrors its control lines (voice.text / send) over WiFi TCP
to the host's LAN IP:port 8767; the host bridge runs a control listener
that applies the same handlers as the USB console reader, deduping
identical lines within 500 ms (both transports deliver the same line).

All logic here is stdlib-only and testable without hardware: the
listener runs against a real loopback socket, handlers/lan-ip detection
are injected, and clocks are fake. The device side (pet_net.c host.addr
handling, NVS namespace pethost, control-line sender task) is C and not
host-testable — verified by inspection only (see evidence, §未验证).
No test touches COM5 or a real device.
"""

import inspect
import json
import os
import socket
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


def tempdir():
    return tempfile.mkdtemp(prefix="buddy-ctl-test-")


def wfile(path, data, mode=None):
    if mode is None:
        mode = "wb" if isinstance(data, bytes) else "w"
    with open(path, mode) as f:
        f.write(data)


def rfile(path, mode="r"):
    with open(path, mode) as f:
        return f.read()


def loopback_listener():
    """Listening socket on 127.0.0.1 ephemeral port -> (sock, port)."""
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", 0))
    srv.listen(4)
    srv.settimeout(3.0)
    return srv, srv.getsockname()[1]


def send_lines(port, *lines):
    """One client connection: write every line (bytes, \\n terminated), close."""
    with socket.create_connection(("127.0.0.1", port), timeout=3.0) as s:
        s.sendall(b"".join(lines))


# ---------------------------------------------------------------------------
# oc_poll: host.addr announcement building
# ---------------------------------------------------------------------------


class TestBuildHostaddrLine(unittest.TestCase):
    def test_line_bytes_exact(self):
        self.assertEqual(
            oc_poll.build_hostaddr_line("192.168.31.89"),
            b'{"version":1,"cmd":"host.addr","host":"192.168.31.89","port":8767}\n')

    def test_custom_port(self):
        line = oc_poll.build_hostaddr_line("10.0.0.5", 9000)
        self.assertEqual(json.loads(line)["port"], 9000)
        self.assertEqual(json.loads(line)["cmd"], "host.addr")

    def test_missing_ip_is_none(self):
        self.assertIsNone(oc_poll.build_hostaddr_line(None))

    def test_blank_ip_is_none(self):
        self.assertIsNone(oc_poll.build_hostaddr_line(""))

    def test_line_is_single_ndjson_frame(self):
        line = oc_poll.build_hostaddr_line("192.168.1.9")
        self.assertTrue(line.endswith(b"\n"))
        self.assertNotIn(b"\n", line[:-1])
        obj = json.loads(line)
        self.assertEqual(obj["version"], 1)
        self.assertEqual(obj["host"], "192.168.1.9")


class TestDualSinkAnnounce(unittest.TestCase):
    def _sink(self, out, hostaddr_ip=None, control_port=8767,
              tcp="192.168.31.89:8766", tcp_send=None, clock=time.monotonic):
        if hostaddr_ip is not None:
            wfile(out + ".hostaddr", hostaddr_ip + "\n")
        return oc_poll.DualSink(out, tcp=tcp, tcp_send=tcp_send,
                                control_port=control_port, clock=clock)

    def test_announces_once_at_start_when_hostaddr_present(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        sent = []
        sink = self._sink(out, hostaddr_ip="192.168.31.89",
                          tcp_send=lambda t, l: sent.append((t, l)))
        sink({"event": "task.start"})
        sink.close()
        lines = [l for _, l in sent]
        self.assertEqual(lines[0],
                         b'{"version":1,"cmd":"host.addr","host":"192.168.31.89","port":8767}\n')
        self.assertEqual(lines[1], b'{"event":"task.start"}\n')

    def test_missing_hostaddr_file_skips_silently(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        sent = []
        sink = self._sink(out, tcp_send=lambda t, l: sent.append(l))
        sink({"event": "task.start"})
        sink.close()
        self.assertEqual(sent, [b'{"event":"task.start"}\n'])

    def test_blank_hostaddr_file_skips_silently(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        wfile(out + ".hostaddr", "\n \n")
        sent = []
        sink = self._sink(out, tcp_send=lambda t, l: sent.append(l))
        sink({"event": "a"})
        sink.close()
        self.assertEqual(sent, [b'{"event":"a"}\n'])

    def test_repeats_about_every_10s(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        now = [100.0]
        announces = []

        def tcp_send(target, line):
            if b"host.addr" in line:
                announces.append(now[0])

        sink = self._sink(out, hostaddr_ip="10.0.0.5", tcp_send=tcp_send,
                          clock=lambda: now[0])
        sink({"event": "a"})          # t=100: first announce (once at start)
        now[0] = 105.0
        sink({"event": "b"})          # t=105: within 10 s window -> none
        now[0] = 110.5
        sink({"event": "c"})          # t=110.5: window passed -> announce
        now[0] = 115.0
        sink({"event": "d"})          # within new window -> none
        sink.close()
        self.assertEqual(announces, [100.0, 110.5])

    def test_control_port_zero_disables_announce(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        wfile(out + ".hostaddr", "10.0.0.5\n")
        sent = []
        sink = self._sink(out, control_port=0, tcp_send=lambda t, l: sent.append(l))
        sink({"event": "a"})
        sink.close()
        self.assertEqual(sent, [b'{"event":"a"}\n'])

    def test_announce_failure_does_not_crash_file_still_written(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        wfile(out + ".hostaddr", "10.0.0.5\n")

        def boom(target, line):
            raise RuntimeError("refused")

        sink = self._sink(out, tcp_send=boom)
        sink({"event": "task.start"})  # must not raise
        sink.close()
        self.assertIn(b'"event":"task.start"', rfile(out, "rb"))

    def test_announce_off_without_tcp_target(self):
        d = tempdir()
        out = os.path.join(d, "oc_events.log")
        wfile(out + ".hostaddr", "10.0.0.5\n")
        sent = []
        sink = self._sink(out, tcp=None, tcp_send=lambda t, l: sent.append(l))
        sink({"event": "a"})
        sink.close()
        self.assertEqual(sent, [])


class TestOcPollCliControlPort(unittest.TestCase):
    def test_default_8767(self):
        args = oc_poll.build_parser().parse_args([])
        self.assertEqual(args.control_port, 8767)

    def test_custom_port(self):
        args = oc_poll.build_parser().parse_args(["--control-port", "9001"])
        self.assertEqual(args.control_port, 9001)


# ---------------------------------------------------------------------------
# file_to_serial: LAN IP discovery + hostaddr file
# ---------------------------------------------------------------------------


class TestLanIpDiscovery(unittest.TestCase):
    def test_parse_ipconfig_english(self):
        text = ("Windows IP Configuration\r\n\n"
                "Wireless LAN adapter Wi-Fi:\r\n\n"
                "   IPv4 Address. . . . . . . . . . . : 192.168.31.89\r\n")
        self.assertEqual(file_to_serial._parse_ipconfig(text), "192.168.31.89")

    def test_parse_ipconfig_chinese_locale(self):
        text = "   IPv4 地址 . . . . . . . . . . . . : 10.0.0.7\r\n"
        self.assertEqual(file_to_serial._parse_ipconfig(text), "10.0.0.7")

    def test_parse_ipconfig_no_match(self):
        self.assertIsNone(file_to_serial._parse_ipconfig("no addresses here"))

    def test_get_lan_ip_injectable_ipconfig(self):
        self.assertEqual(
            file_to_serial.get_lan_ip(udp_ip=None, ipconfig_fn=lambda: "x\n   IPv4 Address. . . : 172.20.1.2\r\n"),
            "172.20.1.2")

    def test_get_lan_ip_none_everywhere(self):
        self.assertIsNone(file_to_serial.get_lan_ip(udp_ip=None, ipconfig_fn=None))

    def test_write_hostaddr_file_atomic(self):
        d = tempdir()
        path = os.path.join(d, "x.log.hostaddr")
        file_to_serial.write_hostaddr_file(path, "192.168.31.89")
        self.assertEqual(rfile(path).strip(), "192.168.31.89")
        leftovers = [n for n in os.listdir(d) if n.endswith(".tmp")]
        self.assertEqual(leftovers, [])


# ---------------------------------------------------------------------------
# file_to_serial: 500 ms line dedup (USB + WiFi double delivery)
# ---------------------------------------------------------------------------


class TestLineDedup(unittest.TestCase):
    def test_same_line_within_window_allowed_once(self):
        now = [0.0]
        dedup = file_to_serial.LineDedup(clock=lambda: now[0])
        line = b'{"v":1,"cmd":"voice.text","text":"hi"}'
        self.assertTrue(dedup.allow(line))
        now[0] += 0.2
        self.assertFalse(dedup.allow(line))  # WiFi copy right after USB copy

    def test_different_lines_both_pass(self):
        dedup = file_to_serial.LineDedup()
        self.assertTrue(dedup.allow(b'{"v":1,"cmd":"voice.text","text":"a"}'))
        self.assertTrue(dedup.allow(b'{"v":1,"cmd":"voice.text","text":"b"}'))

    def test_same_line_again_after_window_passes(self):
        now = [0.0]
        dedup = file_to_serial.LineDedup(clock=lambda: now[0])
        line = b'{"v":1,"cmd":"send"}'
        self.assertTrue(dedup.allow(line))
        now[0] += 0.5
        self.assertTrue(dedup.allow(line))

    def test_memory_is_pruned(self):
        now = [0.0]
        dedup = file_to_serial.LineDedup(clock=lambda: now[0])
        for i in range(50):
            dedup.allow(b"line-%d" % i)
        now[0] += 1.0
        dedup.allow(b"flush")
        self.assertEqual(len(dedup._seen), 1)


# ---------------------------------------------------------------------------
# file_to_serial: control listener (loopback server + injected handlers)
# ---------------------------------------------------------------------------


class TestControlListener(unittest.TestCase):
    def _run(self, lines, line_filter, handlers_kwargs=None):
        """Start the listener on loopback, push one client's lines, join."""
        srv, port = loopback_listener()
        stop = threading.Event()
        got = []
        handlers = file_to_serial.make_control_handlers(
            voice_post=lambda url, text, timeout: got.append(("voice", text)),
            send_action=lambda: got.append(("send", None)),
            **(handlers_kwargs or {}))
        th = threading.Thread(
            target=file_to_serial.control_listener,
            args=(srv, stop, handlers),
            kwargs={"line_filter": line_filter}, daemon=True)
        th.start()
        try:
            send_lines(port, *lines)
            deadline = time.monotonic() + 3.0
            while time.monotonic() < deadline and not got:
                time.sleep(0.02)
        finally:
            stop.set()
            th.join(timeout=2.0)
            srv.close()
        self.assertFalse(th.is_alive())
        return got

    def test_voice_text_and_send_dispatched(self):
        got = self._run([
            b'{"v":1,"cmd":"voice.text","text":"run tests"}\n',
            b'{"v":1,"cmd":"send"}\n',
        ], line_filter=None)
        self.assertEqual(got, [("voice", "run tests"), ("send", None)])

    def test_dedup_same_line_twice_one_action(self):
        dedup = file_to_serial.LineDedup()
        line = b'{"v":1,"cmd":"voice.text","text":"dup"}\n'
        got = self._run([line, line], line_filter=dedup.allow)
        self.assertEqual(got, [("voice", "dup")])

    def test_non_control_lines_ignored(self):
        got = self._run([b"garbage {{\n", b"random text\n"], line_filter=None)
        self.assertEqual(got, [])

    def test_line_without_newline_ignored(self):
        got = self._run([b'{"v":1,"cmd":"send"}'], line_filter=None)
        self.assertEqual(got, [])

    def test_voice_disabled_skips_voice_but_keeps_send(self):
        got = self._run(
            [b'{"v":1,"cmd":"voice.text","text":"quiet"}\n',
             b'{"v":1,"cmd":"send"}\n'],
            line_filter=None, handlers_kwargs={"voice_enabled": False})
        self.assertEqual(got, [("send", None)])

    def test_make_control_handlers_signature_respects_flags(self):
        sig = inspect.signature(file_to_serial.make_control_handlers)
        self.assertIn("voice_enabled", sig.parameters)
        self.assertIn("auto_paste", sig.parameters)
        args = file_to_serial.build_parser().parse_args(["--log", "/x.log"])
        self.assertEqual(args.control_port, 8767)
        args2 = file_to_serial.build_parser().parse_args(
            ["--log", "/x.log", "--control-port", "9002"])
        self.assertEqual(args2.control_port, 9002)

    def test_open_control_listener_binds_requested_port(self):
        sock = file_to_serial.open_control_listener(0)  # port 0 = ephemeral
        try:
            port = sock.getsockname()[1]
            self.assertTrue(file_to_serial.open_control_listener is not None)
            c = socket.create_connection(("127.0.0.1", port), timeout=2.0)
            c.close()
        finally:
            sock.close()


# ---------------------------------------------------------------------------
# file_to_serial: USB console reader participates in the same dedup
# ---------------------------------------------------------------------------


class TestConsoleReaderDedup(unittest.TestCase):
    class FakeConsole:
        def __init__(self, lines, stop_event):
            self._lines = list(lines)
            self._stop = stop_event

        def readline(self):
            if self._lines:
                return self._lines.pop(0)
            self._stop.set()
            return b""

    def test_wifi_delivered_line_not_replayed_from_usb(self):
        stop = threading.Event()
        ser = self.FakeConsole(
            [b'{"v":1,"cmd":"voice.text","text":"fast"}\r\n'], stop)
        posts = []
        dedup = file_to_serial.LineDedup()
        self.assertTrue(dedup.allow(b'{"v":1,"cmd":"voice.text","text":"fast"}'))
        file_to_serial.console_reader(
            ser, "unused-ip-file", stop,
            voice_post=lambda url, text, timeout: posts.append(text),
            line_filter=dedup.allow)
        self.assertEqual(posts, [])  # WiFi already handled it <500 ms ago

    def test_usb_line_still_fires_with_fresh_dedup(self):
        stop = threading.Event()
        ser = self.FakeConsole(
            [b'{"v":1,"cmd":"voice.text","text":"fresh"}\r\n',
             b"net: wifi connected ip=10.0.0.5\r\n"], stop)
        d = tempdir()
        ipf = os.path.join(d, "x.ip")
        posts = []
        dedup = file_to_serial.LineDedup()
        file_to_serial.console_reader(
            ser, ipf, stop,
            voice_post=lambda url, text, timeout: posts.append(text),
            line_filter=dedup.allow)
        self.assertEqual(posts, ["fresh"])
        # IP discovery is unaffected by the control-line filter.
        self.assertEqual(rfile(ipf).strip(), "10.0.0.5")


# ---------------------------------------------------------------------------
# file_to_serial: main() writes <log>.hostaddr for oc_poll to announce
# ---------------------------------------------------------------------------


class TestMainHostaddr(unittest.TestCase):
    def test_main_writes_hostaddr_from_detected_ip(self):
        d = tempdir()
        log = os.path.join(d, "x.log")
        wfile(log, b'{"event":"a"}\n')
        ser = type("S", (), {"readline": lambda s: b"",
                             "write": lambda s, b: None,
                             "close": lambda s: None})()
        rc = file_to_serial.main(
            ["--log", log, "--interval", "0.01", "--control-port", "0"],
            serial_factory=lambda p: ser,
            stop_after=0.1,
            ip_detect=lambda: "192.168.31.89")
        self.assertEqual(rc, 0)
        self.assertEqual(rfile(log + ".hostaddr").strip(), "192.168.31.89")

    def test_main_skips_hostaddr_when_detection_fails(self):
        d = tempdir()
        log = os.path.join(d, "x.log")
        wfile(log, b'{"event":"a"}\n')
        ser = type("S", (), {"readline": lambda s: b"",
                             "write": lambda s, b: None,
                             "close": lambda s: None})()
        rc = file_to_serial.main(
            ["--log", log, "--interval", "0.01", "--control-port", "0"],
            serial_factory=lambda p: ser,
            stop_after=0.1,
            ip_detect=lambda: None)
        self.assertEqual(rc, 0)
        self.assertFalse(os.path.exists(log + ".hostaddr"))


if __name__ == "__main__":
    unittest.main()
