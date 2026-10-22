"""M9：单进程桥的 host 测试（无需设备）。

覆盖：分帧、写失败重连一次、两次都失败则丢弃并记账、HTTP→sink 端到端。
"""

import json
import os
import sys
import threading
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import pet_bridge
import pet_entry_server


class FakeSerial:
    def __init__(self, fail_times=0, exc=OSError("boom")):
        self.fail_times = fail_times
        self.exc = exc
        self.writes = []
        self.closed = False

    def write(self, data):
        if self.fail_times > 0:
            self.fail_times -= 1
            raise self.exc
        self.writes.append(data)

    def close(self):
        self.closed = True


class SinkFraming(unittest.TestCase):
    def test_dict_is_framed_as_one_ndjson_line(self):
        fake = FakeSerial()
        sink = pet_bridge.ReconnectingSink("X", open_serial=lambda p, b: fake)
        self.assertTrue(sink({"version": 1, "event": "agent.idle"}))
        self.assertEqual(fake.writes,
                         [b'{"version":1,"event":"agent.idle"}\n'])

    def test_existing_newline_not_duplicated(self):
        fake = FakeSerial()
        sink = pet_bridge.ReconnectingSink("X", open_serial=lambda p, b: fake)
        sink('{"version":1,"event":"agent.idle"}\n')
        self.assertEqual(fake.writes,
                         [b'{"version":1,"event":"agent.idle"}\n'])

    def test_serial_opened_lazily(self):
        opened = []

        def opener(p, b):
            opened.append((p, b))
            return FakeSerial()

        sink = pet_bridge.ReconnectingSink("COM9", 115200, open_serial=opener)
        self.assertEqual(opened, [])
        sink({"version": 1, "event": "agent.idle"})
        self.assertEqual(opened, [("COM9", 115200)])


class SinkRecovery(unittest.TestCase):
    def test_write_failure_reconnects_once(self):
        logs = []
        serials = []

        def opener(p, b):
            s = FakeSerial(fail_times=1 if not serials else 0)
            serials.append(s)
            return s

        sink = pet_bridge.ReconnectingSink("X", open_serial=opener, log=logs.append)
        self.assertTrue(sink({"version": 1, "event": "task.done"}))
        self.assertEqual(sink.sent, 1)
        self.assertEqual(sink.dropped, 0)
        self.assertEqual(sink.reconnects, 1)
        self.assertEqual(len(serials), 2)          # 重连后换了新对象
        self.assertTrue(serials[0].closed)         # 旧的被关掉
        self.assertEqual(len(serials[1].writes), 1)

    def test_two_failures_drop_and_count(self):
        logs = []

        def opener(p, b):
            return FakeSerial(fail_times=99)

        sink = pet_bridge.ReconnectingSink("X", open_serial=opener, log=logs.append)
        self.assertFalse(sink({"version": 1, "event": "task.done"}))
        self.assertEqual(sink.sent, 0)
        self.assertEqual(sink.dropped, 1)
        self.assertEqual(sink.reconnects, 1)
        self.assertTrue(any("丢弃" in m for m in logs))

    def test_open_failure_is_not_a_crash(self):
        def opener(p, b):
            raise OSError("no such port")

        sink = pet_bridge.ReconnectingSink("X", open_serial=opener,
                                           log=lambda m: None)
        self.assertFalse(sink({"version": 1, "event": "agent.idle"}))
        self.assertEqual(sink.dropped, 1)


class BridgeIntegration(unittest.TestCase):
    def test_post_reaches_serial(self):
        fake = FakeSerial()
        sink = pet_bridge.ReconnectingSink("X", open_serial=lambda p, b: fake)
        srv = pet_entry_server.make_server(port=0, sink=sink)
        thread = threading.Thread(target=srv.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = srv.server_address[:2]
            payload = json.dumps({"version": 1, "event": "task.done",
                                  "title": "桥接"}).encode("utf-8")
            req = urllib.request.Request(
                f"http://{host}:{port}/v1/events", data=payload, method="POST",
                headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(req, timeout=5) as resp:
                self.assertEqual(resp.status, 200)
        finally:
            srv.shutdown()
            srv.server_close()

        self.assertEqual(sink.sent, 1)
        self.assertEqual(json.loads(fake.writes[0].decode("utf-8"))["title"], "桥接")

    def test_invalid_request_never_reaches_serial(self):
        fake = FakeSerial()
        sink = pet_bridge.ReconnectingSink("X", open_serial=lambda p, b: fake)
        srv = pet_entry_server.make_server(port=0, sink=sink)
        thread = threading.Thread(target=srv.serve_forever, daemon=True)
        thread.start()
        try:
            host, port = srv.server_address[:2]
            req = urllib.request.Request(
                f"http://{host}:{port}/v1/events", data=b"not json", method="POST",
                headers={"Content-Type": "application/json"})
            try:
                urllib.request.urlopen(req, timeout=5)
                self.fail("应当返回 400")
            except urllib.error.HTTPError as exc:
                self.assertEqual(exc.code, 400)
        finally:
            srv.shutdown()
            srv.server_close()

        self.assertEqual(fake.writes, [])


if __name__ == "__main__":
    unittest.main()
