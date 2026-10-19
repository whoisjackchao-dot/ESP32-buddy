"""M4 increment 2a: host-side event entry core tests (contract M4 §8).

Covers E2E-02 (any local script can POST an event), E2E-03 (illegal input:
no crash, error reply, no broadcast) and SEC-01 (loopback-only bind) at
the unit + loopback HTTP level. No real device/transport involved.
"""

import json
import os
import sys
import threading
import unittest
import urllib.error
import urllib.request

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
import pet_ingest
import pet_entry_server


def jbody(obj):
    return json.dumps(obj).encode("utf-8")


def call(method="POST", path="/v1/events", ct="application/json", body=b""):
    return pet_ingest.ingest_request(method, path, ct, body)


class IngestHappyPath(unittest.TestCase):
    def test_simple_form_ok(self):
        r = call(body=jbody({"version": 1, "event": "task.done", "title": "Hello"}))
        self.assertTrue(r.ok)
        self.assertEqual(r.http_status, 200)
        self.assertEqual(r.code, "OK")
        self.assertEqual(r.line, '{"version":1,"event":"task.done","title":"Hello"}\n')

    def test_rich_form_tasks0_wins(self):
        body = jbody({
            "version": 1,
            "event": "task.start",
            "title": "STALE",
            "project": "STALE_P",
            "tasks": [
                {"title": "GAMMA", "project": "P1", "status": "working", "elapsed_s": 75},
                {"title": "BETA", "status": "input_required", "elapsed_s": 900},
            ],
            "stats": ["7 DONE", "4 ASKS"],
        })
        r = call(body=body)
        self.assertTrue(r.ok)
        self.assertEqual(
            r.line,
            '{"version":1,"event":"task.start","title":"GAMMA","project":"P1","elapsed_s":75}\n',
        )

    def test_heartbeat_minimal_line(self):
        r = call(body=jbody({"version": 1, "event": "device.heartbeat"}))
        self.assertTrue(r.ok)
        self.assertEqual(r.line, '{"version":1,"event":"device.heartbeat"}\n')

    def test_title_truncated_to_63(self):
        r = call(body=jbody({"version": 1, "event": "task.done", "title": "x" * 100}))
        self.assertTrue(r.ok)
        self.assertIn('"title":"' + "x" * 63 + '"}', r.line)
        self.assertNotIn("x" * 64, r.line)

    def test_stats_ignored(self):
        r = call(body=jbody({"version": 1, "event": "agent.idle", "stats": ["1 BUSY"]}))
        self.assertTrue(r.ok)
        self.assertEqual(r.line, '{"version":1,"event":"agent.idle"}\n')


class IngestErrors(unittest.TestCase):
    def test_method_get_405(self):
        r = call(method="GET")
        self.assertFalse(r.ok)
        self.assertEqual(r.code, "METHOD")
        self.assertEqual(r.http_status, 405)
        self.assertIsNone(r.line)

    def test_path_not_found_404(self):
        r = call(path="/bad")
        self.assertFalse(r.ok)
        self.assertEqual(r.code, "NOT_FOUND")
        self.assertEqual(r.http_status, 404)

    def test_content_type_415(self):
        r = call(ct="text/plain", body=jbody({"version": 1, "event": "task.done"}))
        self.assertEqual((r.code, r.http_status), ("CONTENT_TYPE", 415))
        r2 = call(ct=None, body=jbody({"version": 1, "event": "task.done"}))
        self.assertEqual((r2.code, r2.http_status), ("CONTENT_TYPE", 415))

    def test_body_too_long_413(self):
        body = b"{" + b" " * pet_ingest.MAX_BODY
        self.assertEqual(len(body), pet_ingest.MAX_BODY + 1)
        r = call(body=body)
        self.assertEqual((r.code, r.http_status), ("TOO_LONG", 413))

    def test_non_json_body_400(self):
        r = call(body=b"not json")
        self.assertEqual((r.code, r.http_status), ("JSON", 400))

    def test_top_level_array_400(self):
        r = call(body=b"[1,2]")
        self.assertEqual((r.code, r.http_status), ("JSON", 400))

    def test_version_missing_400(self):
        r = call(body=jbody({"event": "task.done"}))
        self.assertEqual((r.code, r.http_status), ("VERSION", 400))

    def test_version_2_400(self):
        r = call(body=jbody({"version": 2, "event": "task.done"}))
        self.assertEqual((r.code, r.http_status), ("VERSION", 400))

    def test_event_missing_400(self):
        r = call(body=jbody({"version": 1}))
        self.assertEqual((r.code, r.http_status), ("EVENT", 400))

    def test_event_non_str_400(self):
        r = call(body=jbody({"version": 1, "event": 123}))
        self.assertEqual((r.code, r.http_status), ("EVENT", 400))

    def test_unknown_event_400(self):
        r = call(body=jbody({"version": 1, "event": "task.pause"}))
        self.assertEqual((r.code, r.http_status), ("UNKNOWN_EVENT", 400))

    def test_title_int_type_400(self):
        r = call(body=jbody({"version": 1, "event": "task.done", "title": 7}))
        self.assertEqual((r.code, r.http_status), ("TYPE", 400))

    def test_elapsed_negative_400(self):
        r = call(body=jbody({"version": 1, "event": "task.start", "elapsed_s": -1}))
        self.assertEqual((r.code, r.http_status), ("TYPE", 400))

    def test_elapsed_bool_400(self):
        r = call(body=jbody({"version": 1, "event": "task.start", "elapsed_s": True}))
        self.assertEqual((r.code, r.http_status), ("TYPE", 400))

    def test_elapsed_over_device_limit_400(self):
        # Device-side pet_link caps elapsed_s at 1e9 (parse_uint); accepting a
        # larger value here would build a line the device rejects.
        r = call(body=jbody({"version": 1, "event": "task.start",
                             "elapsed_s": 1000000001}))
        self.assertEqual((r.code, r.http_status), ("TYPE", 400))

    def test_elapsed_at_device_limit_ok(self):
        r = call(body=jbody({"version": 1, "event": "task.start",
                             "elapsed_s": 1000000000}))
        self.assertTrue(r.ok)

    def test_control_char_title_400(self):
        # \u0001 would be emitted as "\u0001" by json.dumps; the device
        # parser only supports \" \\ \/ \n \t \r \b \f, so reject it here.
        r = call(body=jbody({"version": 1, "event": "task.done",
                             "title": "bad\u0001title"}))
        self.assertEqual((r.code, r.http_status), ("TYPE", 400))

    def test_control_char_project_400(self):
        r = call(body=jbody({"version": 1, "event": "task.start",
                             "project": "bad\u0000project"}))
        self.assertEqual((r.code, r.http_status), ("TYPE", 400))

    def test_tasks0_control_char_400(self):
        r = call(body=jbody({"version": 1, "event": "task.start",
                             "tasks": [{"title": "bad\u0001"}]}))
        self.assertEqual((r.code, r.http_status), ("TYPE", 400))

    def test_tasks_not_list_400(self):
        r = call(body=jbody({"version": 1, "event": "task.start", "tasks": "nope"}))
        self.assertEqual((r.code, r.http_status), ("TYPE", 400))

    def test_tasks0_not_dict_400(self):
        r = call(body=jbody({"version": 1, "event": "task.start", "tasks": ["nope"]}))
        self.assertEqual((r.code, r.http_status), ("TYPE", 400))


class LoopbackServer(unittest.TestCase):
    def test_server_binds_loopback(self):
        srv = pet_entry_server.make_server(port=0, sink=lambda line: None)
        try:
            self.assertEqual(srv.server_address[0], "127.0.0.1")
        finally:
            srv.server_close()

    def test_loopback_integration(self):
        collected = []
        srv = pet_entry_server.make_server(port=0, sink=collected.append)
        thread = threading.Thread(target=srv.serve_forever, daemon=True)
        thread.start()
        try:
            self.assertEqual(srv.server_address[0], "127.0.0.1")
            base = "http://127.0.0.1:%d" % srv.server_address[1]

            # valid event -> 200 and sink receives the device line
            req = urllib.request.Request(
                base + "/v1/events",
                data=jbody({"version": 1, "event": "task.done", "title": "Hello"}),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with urllib.request.urlopen(req, timeout=5) as resp:
                self.assertEqual(resp.status, 200)
            self.assertEqual(collected, ['{"version":1,"event":"task.done","title":"Hello"}\n'])

            # invalid event -> 400 and sink NOT called again (E2E-03: no broadcast)
            bad = urllib.request.Request(
                base + "/v1/events",
                data=jbody({"version": 2, "event": "task.done"}),
                headers={"Content-Type": "application/json"},
                method="POST",
            )
            with self.assertRaises(urllib.error.HTTPError) as cm:
                urllib.request.urlopen(bad, timeout=5)
            self.assertEqual(cm.exception.code, 400)
            cm.exception.close()
            self.assertEqual(len(collected), 1)

            # wrong method on the entry -> 405
            get = urllib.request.Request(base + "/v1/events", method="GET")
            with self.assertRaises(urllib.error.HTTPError) as cm2:
                urllib.request.urlopen(get, timeout=5)
            self.assertEqual(cm2.exception.code, 405)
            cm2.exception.close()
            self.assertEqual(len(collected), 1)
        finally:
            srv.shutdown()
            srv.server_close()
            thread.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
