"""Unified host service tests (M4/M6 productization, B3/B4).

All tests use an injected fake serial and fake sources: no real port,
pyserial, OpenCode DB or device is required.
"""

import json
import os
import sys
import threading
import time
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import pet_service


class FakeSerial:
    def __init__(self, lines=(), fail_writes=0):
        self.writes = []
        self.lines = list(lines)
        self.closed = False
        self.fail_writes = fail_writes

    def write(self, data):
        if self.fail_writes > 0:
            self.fail_writes -= 1
            raise OSError("injected write failure")
        self.writes.append(data)

    def readline(self):
        if self.lines:
            return self.lines.pop(0)
        return b""

    def close(self):
        self.closed = True


class FakeSource:
    def __init__(self, events=(), raise_once=False):
        self.events = list(events)
        self.raise_once = raise_once

    def poll(self):
        if self.raise_once:
            self.raise_once = False
            raise RuntimeError("injected poll failure")
        if self.events:
            return self.events.pop(0)
        return None


def wait_for(predicate, timeout=3.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(0.01)
    return False


def post(port, payload, token=None):
    headers = {"Content-Type": "application/json"}
    if token is not None:
        headers["X-Pet-Token"] = token
    req = urllib.request.Request(
        "http://127.0.0.1:%d/v1/events" % port,
        data=json.dumps(payload).encode("utf-8"),
        headers=headers,
        method="POST",
    )
    return urllib.request.urlopen(req, timeout=3)


class ServiceIntegration(unittest.TestCase):
    def make(self, serial_obj=None, **kwargs):
        fake = serial_obj or FakeSerial()
        kwargs.setdefault("poll_interval", 0.01)
        kwargs.setdefault("heartbeat", 5.0)
        service = pet_service.PetService(fake, port=0, **kwargs)
        service.start()
        self.addCleanup(service.stop)
        return service, fake

    def test_http_event_reaches_serial(self):
        service, fake = self.make()
        port = service.server.server_address[1]
        with post(port, {"version": 1, "event": "task.done", "title": "Hello"}) as resp:
            self.assertEqual(resp.status, 200)
        self.assertTrue(wait_for(
            lambda: any(b'"title":"Hello"' in w for w in fake.writes)))
        hello = [w for w in fake.writes if b'"title":"Hello"' in w][0]
        self.assertTrue(hello.endswith(b"\n"))

    def test_source_event_reaches_serial(self):
        source = FakeSource([{"version": 1, "event": "task.start", "title": "OC"}])
        service, fake = self.make(source=source)
        self.assertTrue(wait_for(lambda: len(fake.writes) >= 1))
        self.assertIn(b'"event":"task.start"', fake.writes[0])

    def test_heartbeat_when_idle(self):
        service, fake = self.make(heartbeat=0.05)
        self.assertTrue(wait_for(
            lambda: any(b'"device.heartbeat"' in w for w in fake.writes)))

    def test_device_back_window_calls_focus(self):
        calls = []
        service, fake = self.make(
            serial_obj=FakeSerial([b'{"v":1,"cmd":"back_window"}\n']),
            focus=lambda: calls.append(1) or True)
        self.assertTrue(wait_for(lambda: calls == [1]))

    def test_source_error_does_not_kill_service(self):
        source = FakeSource(
            [{"version": 1, "event": "task.done", "title": "AFTER"}],
            raise_once=True)
        service, fake = self.make(source=source)
        self.assertTrue(wait_for(
            lambda: any(b'"title":"AFTER"' in w for w in fake.writes)))

    def test_write_error_reopens_and_delivers(self):
        first = FakeSerial(fail_writes=1)
        second = FakeSerial()
        service = pet_service.PetService(
            first, port=0, poll_interval=0.01, heartbeat=5.0,
            serial_factory=lambda: second)
        service.start()
        self.addCleanup(service.stop)
        port = service.server.server_address[1]
        with post(port, {"version": 1, "event": "task.done", "title": "RETRY"}) as resp:
            self.assertEqual(resp.status, 200)
        self.assertTrue(wait_for(
            lambda: any(b'"title":"RETRY"' in w for w in second.writes)),
            second.writes)
        self.assertTrue(first.closed)

    def test_token_required(self):
        service, fake = self.make(token="s3cret")
        port = service.server.server_address[1]
        with self.assertRaises(urllib.error.HTTPError) as cm:
            post(port, {"version": 1, "event": "task.done"})
        self.assertEqual(cm.exception.code, 401)
        cm.exception.close()
        with post(port, {"version": 1, "event": "task.done"}, token="s3cret") as resp:
            self.assertEqual(resp.status, 200)
        self.assertTrue(wait_for(
            lambda: any(b'"event":"task.done"' in w for w in fake.writes)))


class QueueSinkUnit(unittest.TestCase):
    def test_full_queue_drops_oldest(self):
        q = pet_service.QueueSink(maxsize=2)
        q("a\n")
        q("b\n")
        q("c\n")
        self.assertEqual(q.qsize(), 2)
        self.assertEqual(q.get(), "b\n")
        self.assertEqual(q.get(), "c\n")
        self.assertEqual(q.dropped, 1)


if __name__ == "__main__":
    unittest.main()
