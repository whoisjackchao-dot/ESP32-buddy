"""M4 increment 2a: host-side event ingest core (contract M4 §8).

Pure logic only: HTTP request fields in -> Result (device line) out.
No I/O here; pet_entry_server.py wraps this in http.server. The produced
line is the device line format frozen in M4 §6.1 (NDJSON, one object).
"""

import json

VERSION = 1
MAX_BODY = 512
TITLE_MAX = 64
PROJECT_MAX = 64
EVENT_INGEST_PORT = 8787

KNOWN_EVENTS = frozenset({
    "task.start", "agent.input_required", "task.done",
    "task.error", "agent.blocked", "agent.idle", "device.heartbeat",
})

ENTRY_PATH = "/v1/events"
OK = "OK"


class Result:
    """Outcome of one ingest attempt (value object)."""

    __slots__ = ("ok", "http_status", "code", "line")

    def __init__(self, ok, http_status, code, line=None):
        self.ok = ok
        self.http_status = http_status
        self.code = code
        self.line = line

    def __repr__(self):
        return "Result(ok=%r, http_status=%r, code=%r, line=%r)" % (
            self.ok, self.http_status, self.code, self.line)


def _err(code, http_status):
    return Result(False, http_status, code, None)


def _is_json_content_type(content_type):
    # "application/json" with an optional "; charset=..." parameter (§8.1)
    if not content_type:
        return False
    media_type = content_type.split(";", 1)[0].strip().lower()
    return media_type == "application/json"


def build_line(version, event, title=None, project=None, elapsed_s=None):
    """Device line (M4 §6.1): compact JSON, key order version,event,title,project,elapsed_s."""
    obj = {"version": version, "event": event}
    if title is not None:
        obj["title"] = title
    if project is not None:
        obj["project"] = project
    if elapsed_s is not None:
        obj["elapsed_s"] = elapsed_s
    # ensure_ascii=False keeps UTF-8 text raw: §6.1 only allows the
    # \" \\ \/ \n \t escapes, not \uXXXX, on the device side.
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":")) + "\n"


def ingest_request(method, path, content_type, body):
    """Validate one HTTP request and map it to a device line (M4 §8.2/§8.3)."""
    if method != "POST":
        return _err("METHOD", 405)
    if path != ENTRY_PATH:
        return _err("NOT_FOUND", 404)
    if not _is_json_content_type(content_type):
        return _err("CONTENT_TYPE", 415)
    if len(body) > MAX_BODY:
        return _err("TOO_LONG", 413)

    try:
        obj = json.loads(body.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return _err("JSON", 400)
    if not isinstance(obj, dict):
        return _err("JSON", 400)

    version = obj.get("version")
    if isinstance(version, bool) or not isinstance(version, int) or version != VERSION:
        return _err("VERSION", 400)

    event = obj.get("event")
    if not isinstance(event, str):
        return _err("EVENT", 400)
    if event not in KNOWN_EVENTS:
        return _err("UNKNOWN_EVENT", 400)

    # Field source (§8.2): tasks[0] wins when tasks is a non-empty list,
    # else top-level. An empty tasks list falls back to top-level.
    src = obj
    if "tasks" in obj:
        tasks = obj["tasks"]
        if not isinstance(tasks, list):
            return _err("TYPE", 400)
        if tasks:
            first = tasks[0]
            if not isinstance(first, dict):
                return _err("TYPE", 400)
            src = first

    title = src.get("title")
    if title is not None:
        if not isinstance(title, str):
            return _err("TYPE", 400)
        title = title[:TITLE_MAX - 1]

    project = src.get("project")
    if project is not None:
        if not isinstance(project, str):
            return _err("TYPE", 400)
        project = project[:PROJECT_MAX - 1]

    elapsed_s = src.get("elapsed_s")
    if elapsed_s is not None:
        if isinstance(elapsed_s, bool) or not isinstance(elapsed_s, int) or elapsed_s < 0:
            return _err("TYPE", 400)

    # Unknown input keys (e.g. status, stats) are ignored (§8.2, §6.1).
    return Result(True, 200, OK, build_line(version, event, title, project, elapsed_s))
