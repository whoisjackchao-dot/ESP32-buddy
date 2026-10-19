"""M4 increment 2a: thin HTTP entry around pet_ingest (contract M4 §8.1/§8.4).

Loopback-only: POST /v1/events. Valid lines go to the injectable sink
(default: stdout, the future BLE/serial injection point); invalid input
never reaches the sink (§8.3: 回错、不广播). No transport is implemented.
"""

import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pet_ingest


def _default_sink(line):
    # Placeholder transport: stdout until BLE/serial replaces it (§8.4).
    print(line, end="")


class _EntryHandler(BaseHTTPRequestHandler):
    server_version = "pet-entry/1"

    def _authorized(self):
        """Optional bearer-style token; absent token means loopback-only mode."""
        expected = getattr(self.server, "token", None)
        if not expected:
            return True
        return self.headers.get("X-Pet-Token") == expected

    def do_POST(self):
        if not self._authorized():
            self._respond_text(401, "AUTH")
            return
        length_header = self.headers.get("Content-Length")
        content_type = self.headers.get("Content-Type")
        if length_header is None:
            result = pet_ingest.ingest_request("POST", self.path, content_type, b"")
        else:
            try:
                length = int(length_header)
            except ValueError:
                # Unparseable length: body unusable, answer JSON error explicitly.
                self._respond_text(400, "JSON")
                return
            if length > pet_ingest.MAX_BODY:
                # Reply without reading the oversized body (§8.3).
                self._respond_text(413, "TOO_LONG")
                return
            body = self.rfile.read(length)
            result = pet_ingest.ingest_request("POST", self.path, content_type, body)
        self._respond(result)

    def _respond(self, result):
        if result.ok:
            self.server.sink(result.line)
            self._respond_text(200, "OK")
        else:
            self._respond_text(result.http_status, result.code)

    def _respond_text(self, status, text):
        payload = (text + "\n").encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def _method_not_allowed(self):
        self._respond_text(405, "METHOD")

    do_GET = _method_not_allowed
    do_PUT = _method_not_allowed
    do_DELETE = _method_not_allowed
    do_PATCH = _method_not_allowed
    do_OPTIONS = _method_not_allowed

    def do_HEAD(self):
        # Same 405 as the other non-POST methods, but without a body.
        self.send_response(405)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", "7")
        self.end_headers()

    def log_message(self, format, *args):
        # Access logs stay off: the sink is the designated output channel.
        pass


def make_server(host="127.0.0.1", port=None, sink=None, token=None):
    """Bind host:port (loopback by default, §8.1) and return the server.

    port=None -> EVENT_INGEST_PORT, overridable via PET_EVENT_PORT (§8.5).
    token=None keeps the historical no-auth loopback mode; when set, POST
    requests must carry the same value in the X-Pet-Token header.
    """
    if port is None:
        port = int(os.environ.get("PET_EVENT_PORT", pet_ingest.EVENT_INGEST_PORT))
    if sink is None:
        sink = _default_sink
    server = ThreadingHTTPServer((host, port), _EntryHandler)
    server.sink = sink
    server.token = token
    return server
