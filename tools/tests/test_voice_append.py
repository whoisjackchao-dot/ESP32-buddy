"""voice.text -> OpenCode draft append tests (T entry).

TDD RED→GREEN per evidence/2026-10-25-语音写入OpenCode-T.md. The bridge's
console reader detects the device line {"v":1,"cmd":"voice.text","text":"…"}
on the USB console and best-effort POSTs the text to the local OpenCode
draft-append plugin (POST /v1/draft/append). All logic is stdlib-only and
testable without hardware: the console is a fake object with readline, the
plugin endpoint is a real loopback HTTP listener on an ephemeral port, and
the reader takes an injected post callable. No test touches COM5, a real
device, or a real OpenCode instance.
"""

import json
import os
import sys
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "host"))

import file_to_serial  # noqa: E402


class FakeConsole:
    """Queued console lines; closes the reader (empty line) when drained."""

    def __init__(self, lines, stop_event):
        self._lines = list(lines)
        self._stop = stop_event
        self.written = []

    def readline(self):
        if self._lines:
            return self._lines.pop(0)
        self._stop.set()
        return b""

    def write(self, b):
        self.written.append(b)

    def close(self):
        pass


class LoopbackCollector:
    """Minimal HTTP server capturing one POST /v1/draft/append request."""

    def __init__(self):
        self.lock = threading.Lock()
        self.requests = []
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), self._make_handler())
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def _make_handler(self):
        collector = self

        class Handler(BaseHTTPRequestHandler):
            def do_POST(self):
                length = int(self.headers.get("content-length", "0"))
                body = self.rfile.read(length)
                with collector.lock:
                    collector.requests.append({
                        "path": self.path,
                        "method": self.command,
                        "body": body,
                        "content_type": self.headers.get("content-type", ""),
                    })
                payload = json.dumps({"status": "applied"}).encode("utf-8")
                self.send_response(200)
                self.send_header("content-type", "application/json")
                self.send_header("content-length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)

            def log_message(self, *args):
                pass

        return Handler

    @property
    def url(self):
        return "http://127.0.0.1:%d/v1/draft/append" % self.server.server_address[1]

    def stop(self):
        self.server.shutdown()
        self.server.server_close()


class TestExtractVoiceText(unittest.TestCase):
    def test_valid_voice_line(self):
        line = b'{"v":1,"cmd":"voice.text","text":"hello"}\r\n'
        self.assertEqual(file_to_serial.extract_voice_text(line), "hello")

    def test_utf8_multibyte_text(self):
        raw = json.dumps({"v": 1, "cmd": "voice.text", "text": "打开绿灯"},
                         ensure_ascii=False).encode("utf-8")
        self.assertEqual(file_to_serial.extract_voice_text(raw), "打开绿灯")

    def test_invalid_json_returns_none(self):
        self.assertIsNone(file_to_serial.extract_voice_text(b'{"cmd":"voice.text" '))
        self.assertIsNone(file_to_serial.extract_voice_text(b"not json at all"))

    def test_other_cmd_returns_none(self):
        line = b'{"v":1,"cmd":"net.status","text":"hello"}'
        self.assertIsNone(file_to_serial.extract_voice_text(line))

    def test_missing_or_nonstring_text_returns_none(self):
        self.assertIsNone(file_to_serial.extract_voice_text(
            b'{"v":1,"cmd":"voice.text"}'))
        self.assertIsNone(file_to_serial.extract_voice_text(
            b'{"v":1,"cmd":"voice.text","text":42}'))

    def test_empty_text_returns_none(self):
        self.assertIsNone(file_to_serial.extract_voice_text(
            b'{"v":1,"cmd":"voice.text","text":""}'))

    def test_json_array_or_scalar_returns_none(self):
        self.assertIsNone(file_to_serial.extract_voice_text(b'["voice.text"]'))
        self.assertIsNone(file_to_serial.extract_voice_text(b'42'))


class TestPostVoiceText(unittest.TestCase):
    def test_real_post_reaches_loopback_listener(self):
        collector = LoopbackCollector()
        try:
            ok = file_to_serial.post_voice_text(collector.url, "打开绿灯",
                                                timeout=2.0)
            self.assertTrue(ok)
            reqs = collector.requests
            self.assertEqual(len(reqs), 1)
            self.assertEqual(reqs[0]["path"], "/v1/draft/append")
            self.assertEqual(reqs[0]["method"], "POST")
            self.assertIn("application/json", reqs[0]["content_type"])
            self.assertEqual(json.loads(reqs[0]["body"].decode("utf-8")),
                             {"text": "打开绿灯"})
        finally:
            collector.stop()

    def test_post_failure_is_swallowed(self):
        # Closed port -> connection refused -> False, never an exception.
        self.assertFalse(file_to_serial.post_voice_text(
            "http://127.0.0.1:1/v1/draft/append", "x", timeout=0.5))

    def test_post_fn_injection(self):
        seen = {}

        def fake_post(url, text, timeout):
            seen["url"], seen["text"], seen["timeout"] = url, text, timeout
            return True

        ok = file_to_serial.post_voice_text("http://h:1/a", "hi",
                                            post_fn=fake_post)
        self.assertTrue(ok)
        self.assertEqual(seen, {"url": "http://h:1/a", "text": "hi",
                                "timeout": file_to_serial.VOICE_POST_TIMEOUT})


class TestConsoleReaderVoice(unittest.TestCase):
    def test_reader_posts_voice_text(self):
        d = os.path.join(ROOT, "tmp-voice-test-dummy")  # never written
        ipf = os.path.join(d, "unused")
        stop = threading.Event()
        ser = FakeConsole([
            b"boot ok\r\n",
            b'{"v":1,"cmd":"voice.text","text":"run tests"}\r\n',
            b"net: wifi connected ip=10.0.0.5\r\n",
        ], stop)
        posts = []

        file_to_serial.console_reader(ser, ipf, stop,
                                      voice_url="http://127.0.0.1:8790/v1/draft/append",
                                      voice_post=lambda url, text, timeout: posts.append((url, text)) or True)
        self.assertEqual(posts, [("http://127.0.0.1:8790/v1/draft/append",
                                  "run tests")])

    def test_reader_voice_post_error_does_not_crash_reader(self):
        ipf = "unused-ip-file"
        stop = threading.Event()
        ser = FakeConsole([
            b'{"v":1,"cmd":"voice.text","text":"a"}\r\n',
            b'{"v":1,"cmd":"voice.text","text":"b"}\r\n',
        ], stop)

        def boom(url, text, timeout):
            raise RuntimeError("plugin down")

        # Must drain both lines and exit cleanly despite the failing post.
        file_to_serial.console_reader(ser, ipf, stop, voice_post=boom)

    def test_reader_ignores_non_voice_lines(self):
        ipf = "unused-ip-file"
        stop = threading.Event()
        ser = FakeConsole([
            b"net: wifi connected ip=10.0.0.5\r\n",
            b'{"v":1,"cmd":"net.status"}\r\n',
            b"garbage {{\r\n",
        ], stop)
        posts = []
        file_to_serial.console_reader(ser, ipf, stop,
                                      voice_post=lambda url, text, timeout: posts.append(text) or True)
        self.assertEqual(posts, [])

    def test_reader_disabled_skips_posting(self):
        ipf = "unused-ip-file"
        stop = threading.Event()
        ser = FakeConsole([
            b'{"v":1,"cmd":"voice.text","text":"quiet"}\r\n',
        ], stop)
        posts = []
        file_to_serial.console_reader(ser, ipf, stop, voice_enabled=False,
                                      voice_post=lambda url, text, timeout: posts.append(text) or True)
        self.assertEqual(posts, [])

    def test_reader_default_voice_post_is_real_function(self):
        # Signature contract: console_reader passes (url, text, timeout).
        import inspect
        sig = inspect.signature(file_to_serial.console_reader)
        self.assertIn("voice_url", sig.parameters)
        self.assertIn("voice_post", sig.parameters)
        self.assertIn("voice_enabled", sig.parameters)
        self.assertEqual(sig.parameters["voice_url"].default,
                         file_to_serial.VOICE_DEFAULT_URL)


class TestCli(unittest.TestCase):
    def test_voice_url_default(self):
        args = file_to_serial.build_parser().parse_args(["--log", "/x/y.log"])
        # 默认空 = 写剪贴板（OpenCode 1.19 插件槽不兼容后的方案）
        self.assertEqual(args.voice_url, "")
        self.assertFalse(args.no_voice)

    def test_voice_flags(self):
        args = file_to_serial.build_parser().parse_args([
            "--log", "/x/y.log", "--no-voice",
            "--voice-url", "http://127.0.0.1:9999/v1/draft/append"])
        self.assertTrue(args.no_voice)
        self.assertEqual(args.voice_url,
                         "http://127.0.0.1:9999/v1/draft/append")

    def test_main_no_voice_disables_reader_posting(self):
        log = os.path.join(ROOT, "tmp-voice-cli.log")
        try:
            with open(log, "wb") as f:
                f.write(b'{"event":"a"}\n')
            ser = FakeConsole([], threading.Event())
            state = {"fed": False}

            def fake_sleep(s):
                if not state["fed"]:
                    state["fed"] = True
                    ser._lines.append(
                        b'{"v":1,"cmd":"voice.text","text":"nope"}\r\n')
                    with open(log, "ab") as f:
                        f.write(b'{"event":"b"}\n')

            posts = []
            rc = file_to_serial.main(
                ["--log", log, "--interval", "0.01", "--no-voice"],
                serial_factory=lambda p: ser,
                sleep_fn=fake_sleep,
                stop_after=0.3,
                voice_post=lambda url, text, timeout: posts.append(text) or True)
            self.assertEqual(rc, 0)
            self.assertEqual(posts, [])
        finally:
            if os.path.exists(log):
                os.unlink(log)

    def test_main_posts_voice_line(self):
        log = os.path.join(ROOT, "tmp-voice-cli.log")
        try:
            with open(log, "wb") as f:
                f.write(b'{"event":"a"}\n')
            ser = FakeConsole([], threading.Event())
            state = {"fed": False}

            def fake_sleep(s):
                if not state["fed"]:
                    state["fed"] = True
                    ser._lines.append(
                        b'{"v":1,"cmd":"voice.text","text":"go"}\r\n')
                    with open(log, "ab") as f:
                        f.write(b'{"event":"b"}\n')

            posts = []
            rc = file_to_serial.main(
                ["--log", log, "--interval", "0.01"],
                serial_factory=lambda p: ser,
                sleep_fn=fake_sleep,
                stop_after=0.3,
                voice_post=lambda url, text, timeout: posts.append(text) or True)
            self.assertEqual(rc, 0)
            self.assertEqual(posts, ["go"])
        finally:
            if os.path.exists(log):
                os.unlink(log)


if __name__ == "__main__":
    unittest.main()

class TestSendCmd(unittest.TestCase):
    def test_extract_send_cmd(self):
        self.assertTrue(file_to_serial.extract_send_cmd(b'{"v":1,"cmd":"send"}\n'))
        self.assertTrue(file_to_serial.extract_send_cmd(b'{"cmd": "send"}'))
        self.assertFalse(file_to_serial.extract_send_cmd(b'{"cmd":"voice.text","text":"x"}'))
        self.assertFalse(file_to_serial.extract_send_cmd(b'{oops'))
        self.assertFalse(file_to_serial.extract_send_cmd(b''))
