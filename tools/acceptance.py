"""M9 acceptance harness (contract M9 §3/§4, frozen 2026-10-23).

Measures G3/G4/G6/G7 against the device console (COM5, USB-Serial-JTAG
CDC). Cross-machine latency is always host send -> host receive of the
matching console line, both stamped with time.monotonic() (M9 §1); the
device t=<us> fields are recorded as evidence only. Failures, timeouts
and anomalies all stay in the denominator (M9 §1 iron rule).

Device console lines parsed (M9 §2, ASCII):
    rf t=<us> st=<n> n=<k> top=<title<=8>
    off t=<us> / on t=<us>
    voice id=<0..3> t=<us>
Everything else (st: 1 s heartbeat, boot noise) is ignored.

The serial layer sits behind a small Link interface (write_line /
read_line / close). SerialLink imports pyserial lazily, so importing
this module and every unit test never needs it. --dry-run drives the
same gate loop against DryRunDevice, a contract-faithful console
simulator (sounds per pet_state.c/pet_voice.c, off at the 15 s link
timeout), and is a self-check of stats/verdict logic, not evidence.

Run on Windows (WSL via interop):  python acceptance.py --port COM5 --gate g3
"""

import argparse
import dataclasses
import heapq
import json
import math
import queue
import random
import re
import sys
import threading
import time
from datetime import datetime
from pathlib import Path

PET_LINE_MAX = 512          # M4 §6.6
LOG_TITLE_MAX = 8           # M9 §2: rf top=<title<=8>
TITLE_MAX_STORED = 63       # M4 §6.1: device truncates titles to 63

KNOWN_EVENTS = {            # M4 §6.2
    "task.start", "agent.input_required", "task.done",
    "task.error", "agent.blocked", "agent.idle", "device.heartbeat",
}

# pet_voice.h: PET_VOICE_INPUT=1, PET_VOICE_DONE=2, PET_VOICE_FAILED=3
VOICE_ID = {"input_required": 1, "done": 2, "failed": 3}

# pet_state.c: pending_sound only on transitions into INPUT/DONE/FAILED;
# task.done/agent.input_required/task.error are the explicit triggers.
G7_KINDS = ("input_required", "done", "failed")
TRIGGER_EVENT = {
    "input_required": "agent.input_required",
    "done": "task.done",
    "failed": "task.error",
}
CONTROL_KINDS = ("working", "idle", "heartbeat")
CONTROL_EVENT = {
    "working": "task.start",
    "idle": "agent.idle",
    "heartbeat": "device.heartbeat",
}

# G6 "event entry" rotates view-changing events; heartbeat never renders,
# so it would only burn the 3 s rf window (M9 §3 G6: 口径同 G3).
G6_KINDS = ("task.start", "agent.input_required", "task.done",
            "task.error", "agent.blocked")

_RF_RE = re.compile(r"(?:^|\s)rf t=(\d+) st=(\d+) n=(\d+) top=(.*)$")
_VOICE_RE = re.compile(r"(?:^|\s)voice id=(\d+) t=(\d+)$")
_TRISTATE_RE = re.compile(r"(?:^|\s)(off|on) t=(\d+)$")


# ---------------------------------------------------------------------------
# Pure logic: parsing, matching, stats, verdicts
# ---------------------------------------------------------------------------


def parse_device_line(line):
    """Parse one console line; rf/off/on/voice -> dict, anything else -> None."""
    s = line.strip()
    if not s:
        return None
    m = _RF_RE.search(s)
    if m:
        return {"kind": "rf", "t_us": int(m.group(1)), "st": int(m.group(2)),
                "n": int(m.group(3)), "top": m.group(4)}
    m = _VOICE_RE.search(s)
    if m:
        return {"kind": "voice", "id": int(m.group(1)), "t_us": int(m.group(2))}
    m = _TRISTATE_RE.search(s)
    if m:
        return {"kind": m.group(1), "t_us": int(m.group(2))}
    return None


def find_rf(events, title, since=0.0):
    """First (recv, ev, raw) rf with top == title[:8] received at/after since."""
    key = title[:LOG_TITLE_MAX]
    for recv, mev, raw in events:
        if mev["kind"] == "rf" and mev["top"] == key and recv >= since:
            return (recv, mev, raw)
    return None


def find_kind(events, kind, since=0.0):
    for recv, mev, raw in events:
        if mev["kind"] == kind and recv >= since:
            return (recv, mev, raw)
    return None


def voices_in(events, lo, hi):
    """Voice triples with lo <= recv <= hi (trial observation window)."""
    return [t for t in events
            if t[1]["kind"] == "voice" and lo <= t[0] <= hi]


def percentile(values, p):
    """Nearest-rank percentile; None for no data (e.g. all timeouts)."""
    if not values:
        return None
    vs = sorted(values)
    idx = max(0, math.ceil(p / 100.0 * len(vs)) - 1)
    return vs[idx]


def summarize(rows):
    """M9 §1: total attempts is the denominator; nothing is dropped."""
    return {
        "total": len(rows),
        "pass": sum(1 for r in rows if r["result"] == "pass"),
        "fail": sum(1 for r in rows if r["result"] == "fail"),
        "timeout": sum(1 for r in rows if r["result"] == "timeout"),
    }


def classify_g36(delta_ms, budget_ms):
    if delta_ms is None:
        return "timeout"
    return "pass" if delta_ms <= budget_ms else "fail"


def verdict_g36(summary, trials):
    return summary["pass"] == trials


def classify_g4(delta_s, low_s, high_s):
    if delta_s is None:
        return "timeout"
    return "pass" if low_s <= delta_s <= high_s else "fail"


def verdict_g4(summary, trials, min_pass):
    # Contract letter: PASS iff >=min_pass of the (full) denominator fell
    # inside the window; a timeout trial counts against but not extra.
    return summary["total"] == trials and summary["pass"] >= min_pass


def classify_g7_trial(ids, expected_id):
    if len(ids) == 0:
        return "missing"
    if len(ids) > 1:
        return "duplicate"
    return "pass" if ids[0] == expected_id else "wrong_id"


def classify_g7_control(ids):
    return "pass" if len(ids) == 0 else "fail"


def verdict_g7(pos_summary, ctrl_summary, trials, ctrl_total):
    return pos_summary["pass"] == trials and ctrl_summary["pass"] == ctrl_total


def g7_sequence(trials):
    return [G7_KINDS[i % 3] for i in range(trials)]  # -> ~7/7/6 at 20


def g7_control_sequence(each):
    return ["working"] * each + ["idle"] * each + ["heartbeat"] * each


def make_title(tag, i):
    """Distinct <=8 char ASCII title; device rf logs top= first <=8 chars."""
    return (tag + format(i, "05d"))[:LOG_TITLE_MAX]


def card_event(tag, i, elapsed_s):
    return {"version": 1, "event": "task.start",
            "title": make_title(tag, i), "project": "M9",
            "elapsed_s": elapsed_s}


def g6_event(tag, i, elapsed_s):
    return {"version": 1, "event": G6_KINDS[i % len(G6_KINDS)],
            "title": make_title(tag, i), "project": "M9",
            "elapsed_s": elapsed_s}


def g7_reset_event(i):
    return {"version": 1, "event": "task.start",
            "title": make_title("R7", i), "project": "M9", "elapsed_s": 0}


def g7_trigger_event(kind, i):
    return {"version": 1, "event": TRIGGER_EVENT[kind],
            "title": make_title("T7", i), "project": "M9", "elapsed_s": i}


def g7_control_event(kind, j):
    e = {"version": 1, "event": CONTROL_EVENT[kind]}
    if kind == "working":
        e["title"] = make_title("C7", j)
        e["project"] = "M9"
        e["elapsed_s"] = 0
    return e


def validate_event(obj):
    """Reject anything the device would drop (M4 §6.1), so a trial never
    burns its window on a line the pet_rx discards."""
    if not isinstance(obj, dict):
        raise ValueError("event must be a JSON object")
    version = obj.get("version")
    if isinstance(version, bool) or version != 1:
        raise ValueError("version must be 1")
    event = obj.get("event")
    if not isinstance(event, str) or event not in KNOWN_EVENTS:
        raise ValueError("unknown event: %r" % (event,))
    for key in ("title", "project"):
        if key in obj:
            if not isinstance(obj[key], str):
                raise ValueError("%s must be a string" % key)
            if len(obj[key]) > TITLE_MAX_STORED:
                raise ValueError("%s longer than %d chars" % (key, TITLE_MAX_STORED))
    if "elapsed_s" in obj:
        e = obj["elapsed_s"]
        if isinstance(e, bool) or not isinstance(e, int) or e < 0:
            raise ValueError("elapsed_s must be a non-negative integer")


@dataclasses.dataclass
class GateConfig:
    """Contract timing constants (M9 §3); tests shrink them, CLI uses defaults."""
    trials: int = 20
    rf_timeout_s: float = 3.0        # G3/G6: match window per trial
    budget_ms: float = 1000.0        # G3/G6: <= 1000 ms
    beat_interval_s: float = 1.0     # G4: heartbeat cadence
    beat_seconds: float = 5.0        # G4: heartbeat for 5 s, then stop
    off_wait_s: float = 20.0         # G4: >20 s counts timeout
    off_low_s: float = 15.0          # G4: window [15.0, 17.0] s
    off_high_s: float = 17.0
    g4_min_pass: int = 19            # G4: >=19/20 in window
    settle_s: float = 1.5            # G7: after reset / after trigger
    observe_s: float = 1.5
    idle_settle_s: float = 2.5       # G7 control-idle: let the deferred
    #   done edge (pet_state.c, +1500 ms -> DONE + sound) fire before the
    #   trigger, so the control window itself is genuinely silent.
    control_each: int = 5
    poll_s: float = 0.01


# ---------------------------------------------------------------------------
# Link layer: serial behind an interface; dry-run device simulator
# ---------------------------------------------------------------------------


class SerialLink:
    """pyserial-backed Link; pyserial is imported lazily on first open."""

    def __init__(self, port, baud=115200):
        import serial  # lazy: only the real-serial path needs it
        self.port = port
        self._ser = serial.Serial(port, baud, timeout=0.1)

    def write_line(self, line):
        self._ser.write((line + "\n").encode("utf-8"))
        self._ser.flush()

    def read_line(self):
        """One decoded line, or None when the port had nothing (timeout)."""
        raw = self._ser.readline()
        if not raw:
            return None
        return raw.decode("utf-8", "replace")

    def close(self):
        self._ser.close()


class Recorder:
    """Thread-safe log of parsed console events with host receive times."""

    def __init__(self):
        self._events = []  # (recv_monotonic, parsed, raw_line)
        self._lock = threading.Lock()

    def add(self, recv, parsed, raw):
        with self._lock:
            self._events.append((recv, parsed, raw))

    def all(self):
        with self._lock:
            return list(self._events)


class ReaderThread(threading.Thread):
    """Reads the console, records host time.monotonic() per parsed event."""

    def __init__(self, link, recorder):
        super().__init__(daemon=True, name="m9-reader")
        self._link = link
        self._rec = recorder
        self._stop_evt = threading.Event()

    def run(self):
        while not self._stop_evt.is_set():
            try:
                line = self._link.read_line()
            except Exception:
                break  # port gone
            if not line:
                continue
            parsed = parse_device_line(line)
            if parsed is not None:
                self._rec.add(time.monotonic(), parsed, line.strip())

    def stop(self):
        self._stop_evt.set()


class Harness:
    """send() frames one event line, writes + flushes, stamps t_send."""

    def __init__(self, link):
        self.link = link
        self.rec = Recorder()
        self.reader = ReaderThread(link, self.rec)
        self.reader.start()
        self.sends = []  # (t_send_monotonic, line)

    def send(self, obj):
        validate_event(obj)
        line = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
        if len(line.encode("utf-8")) + 1 > PET_LINE_MAX:
            raise ValueError("event line exceeds PET_LINE_MAX (%d)" % PET_LINE_MAX)
        self.link.write_line(line)
        t_send = time.monotonic()
        self.sends.append((t_send, line))
        return t_send

    def close(self):
        self.reader.stop()
        self.reader.join(timeout=1.0)
        self.link.close()


class DryRunDevice:
    """Contract-faithful simulator of the M9 §2 console (no hardware).

    Semantics follow pet_state.c / pet_voice.c: voice on transitions into
    INPUT/DONE/FAILED only; agent.idle from WORKING/INPUT_REQUIRED takes
    the deferred done edge (+debounce_s -> DONE + done sound); rf fires on
    (state, card count, top title) change; off/on on link-timeout
    transitions; DONE dwell -> IDLE; st: heartbeat every 1 s as noise.
    drop_titles suppresses rf for those tops (fault injection).
    """

    DONE_EDGE = ("done_edge",)
    STATE_IDX = {"IDLE": 0, "WORKING": 1, "INPUT_REQUIRED": 2, "DONE": 3,
                 "FAILED": 4, "OFFLINE": 5}

    def __init__(self, latency_s=0.12, latency_jitter_s=0.03,
                 link_timeout_s=15.0, debounce_s=1.5, dwell_s=5.0,
                 noise=True, drop_titles=()):
        self._latency_s = latency_s
        self._jitter_s = latency_jitter_s
        self._link_timeout_s = link_timeout_s
        self._debounce_s = debounce_s
        self._dwell_s = dwell_s
        self._noise = noise
        self._drop = set(drop_titles)
        self._t0 = time.monotonic()
        self.console = queue.Queue()
        self._lock = threading.Lock()
        self._heap = []  # (fire_at, seq, payload) payload=str | DONE_EDGE
        self._seq = 0
        self._state = "IDLE"
        self._since = self._t0
        self._top = None
        self._card_count = 0
        self._view = (self.STATE_IDX["IDLE"], 0, None)
        self._offline = False
        self._last_valid = None
        self._last_noise = self._t0
        self._stop_evt = threading.Event()
        if noise:
            self.console.put("ESP-ROM:esp32s3-20210301-build")
            self.console.put("I (31) boot: ESP-IDF v5.5")
        self._thread = threading.Thread(target=self._run, daemon=True,
                                        name="m9-simdev")
        self._thread.start()

    # -- public surface (DryRunLink + tests) --------------------------------

    def on_send(self, line):
        """Device receives one NDJSON line (input path is near-instant)."""
        now = time.monotonic()
        with self._lock:
            self._last_valid = now
            if self._offline:
                self._offline = False
                self._push(now, "on t=%d" % self._t_us(now))
        try:
            obj = json.loads(line)
        except ValueError:
            return
        if not isinstance(obj, dict):
            return
        event = obj.get("event")
        title = obj.get("title") if isinstance(obj.get("title"), str) else None
        if event == "device.heartbeat":
            return  # refreshes the link only, never the state (M4 §6.2)
        with self._lock:
            if event == "agent.idle":
                if self._state in ("WORKING", "INPUT_REQUIRED"):
                    # pet_state.c done-edge debounce: DONE + sound at +1.5 s
                    self._push(now + self._debounce_s, self.DONE_EDGE)
                else:
                    self._set_state("IDLE", now)
                return
            self._apply(event, now, title)

    def describe(self):
        return {"type": "DryRunDevice", "latency_s": self._latency_s,
                "latency_jitter_s": self._jitter_s,
                "link_timeout_s": self._link_timeout_s,
                "debounce_s": self._debounce_s, "dwell_s": self._dwell_s,
                "noise": self._noise, "drop_titles": sorted(self._drop)}

    def stop(self):
        self._stop_evt.set()
        self._thread.join(timeout=1.0)

    # -- internals (lock held unless noted) ---------------------------------

    def _apply(self, event, now, title):
        if event == "task.start":
            self._set_state("WORKING", now, title)
        elif event == "agent.input_required":
            self._set_state("INPUT_REQUIRED", now, title)
            self._voice(1, now)
        elif event == "task.done":
            self._set_state("DONE", now, title)
            self._voice(2, now)
        elif event in ("task.error", "agent.blocked"):
            self._set_state("FAILED", now, title)
            self._voice(3, now)
        # unknown events are dropped by the device (M4 §6.4)

    def _set_state(self, state, now, title=None):
        self._state = state
        self._since = now
        if title is not None:
            self._top = title[:LOG_TITLE_MAX]
            self._card_count = 1
        self._maybe_rf(now)

    def _maybe_rf(self, now):
        view = (self.STATE_IDX[self._state], self._card_count, self._top)
        if view == self._view:
            return
        self._view = view
        if self._top in self._drop:
            return  # fault injection: device never shows this title
        fire = now + self._latency()
        self._push(fire, "rf t=%d st=%d n=%d top=%s"
                   % (self._t_us(fire), view[0], view[1], view[2] or ""))

    def _voice(self, vid, now):
        fire = now + self._latency()
        self._push(fire, "voice id=%d t=%d" % (vid, self._t_us(fire)))

    def _latency(self):
        return self._latency_s + random.uniform(0.0, self._jitter_s)

    def _push(self, fire_at, payload):
        self._seq += 1
        heapq.heappush(self._heap, (fire_at, self._seq, payload))

    def _t_us(self, when):
        return int((when - self._t0) * 1e6)

    def _run(self):
        while not self._stop_evt.is_set():
            now = time.monotonic()
            emit = []
            with self._lock:
                while self._heap and self._heap[0][0] <= now:
                    _, _, payload = heapq.heappop(self._heap)
                    if payload == self.DONE_EDGE:
                        self._state = "DONE"
                        self._since = now
                        self._voice(2, now)
                        self._maybe_rf(now)
                    else:
                        emit.append(payload)
                if (not self._offline and self._last_valid is not None
                        and now - self._last_valid >= self._link_timeout_s):
                    self._offline = True
                    emit.append("off t=%d" % self._t_us(now))
                if (self._state == "DONE" and self._dwell_s > 0
                        and now - self._since >= self._dwell_s):
                    self._state = "IDLE"
                    self._since = now
                    self._maybe_rf(now)
                if self._noise and now - self._last_noise >= 1.0:
                    self._last_noise = now
                    emit.append("st: up=%d s heap=212K fps=28"
                                % int(now - self._t0))
            for line in emit:
                self.console.put(line)
            time.sleep(0.005)


class DryRunLink:
    """Link over a DryRunDevice: writes feed the sim, reads drain console."""

    def __init__(self, device):
        self._dev = device

    def write_line(self, line):
        self._dev.on_send(line)

    def read_line(self):
        try:
            return self._dev.console.get(timeout=0.05)
        except queue.Empty:
            return None

    def close(self):
        self._dev.stop()


# ---------------------------------------------------------------------------
# Gate runners
# ---------------------------------------------------------------------------


def _poll(rec, match, deadline, poll_s):
    while True:
        found = match(rec.all())
        if found is not None:
            return found
        if time.monotonic() >= deadline:
            return None
        time.sleep(poll_s)


def _run_g36(h, payload, cfg, gate):
    tag = gate.upper()
    rows = []
    for i in range(cfg.trials):
        elapsed_s = 60 * (i + 1)
        e = card_event(tag, i, elapsed_s) if gate == "g3" else g6_event(tag, i, elapsed_s)
        title = make_title(tag, i)
        t_send = h.send(e)
        found = _poll(h.rec, lambda evs: find_rf(evs, title, since=t_send),
                      t_send + cfg.rf_timeout_s, cfg.poll_s)
        row = {"i": i, "event": e["event"], "title": title,
               "send_monotonic": t_send, "recv_monotonic": None,
               "delta_ms": None, "result": "timeout", "evidence": None}
        if found is not None:
            recv, _, raw = found
            delta_ms = (recv - t_send) * 1000.0
            row.update(recv_monotonic=recv, delta_ms=round(delta_ms, 1),
                       result=classify_g36(delta_ms, cfg.budget_ms),
                       evidence=raw)
        rows.append(row)
    summary = summarize(rows)
    deltas = [r["delta_ms"] for r in rows if r["delta_ms"] is not None]
    payload.update(
        rows=rows, summary=summary,
        p50_ms=percentile(deltas, 50), p95_ms=percentile(deltas, 95),
        verdict="PASS" if verdict_g36(summary, cfg.trials) else "FAIL")


def _run_g4(h, payload, cfg):
    beat = {"version": 1, "event": "device.heartbeat"}
    n_beats = int(round(cfg.beat_seconds / cfg.beat_interval_s)) + 1
    rows = []
    for i in range(cfg.trials):
        last_send = None
        for b in range(n_beats):
            last_send = h.send(beat)
            if b < n_beats - 1:
                time.sleep(cfg.beat_interval_s)
        stop = last_send  # 停发 moment: right after the last heartbeat
        found = _poll(h.rec, lambda evs: find_kind(evs, "off", since=stop),
                      stop + cfg.off_wait_s, cfg.poll_s)
        row = {"i": i, "beats": n_beats, "stop_monotonic": stop,
               "recv_monotonic": None, "delta_s": None,
               "result": "timeout", "evidence": None}
        if found is not None:
            recv, _, raw = found
            delta_s = recv - stop
            row.update(recv_monotonic=recv, delta_s=round(delta_s, 2),
                       result=classify_g4(delta_s, cfg.off_low_s, cfg.off_high_s),
                       evidence=raw)
        rows.append(row)
    summary = summarize(rows)
    payload.update(rows=rows, summary=summary,
                   verdict="PASS" if verdict_g4(summary, cfg.trials,
                                                cfg.g4_min_pass) else "FAIL")


def _run_g7(h, payload, cfg):
    pos_rows, ctrl_rows = [], []
    for i, kind in enumerate(g7_sequence(cfg.trials)):
        h.send(g7_reset_event(i))
        time.sleep(cfg.settle_s)
        t_trig = h.send(g7_trigger_event(kind, i))
        time.sleep(cfg.observe_s)
        vs = voices_in(h.rec.all(), t_trig, t_trig + cfg.observe_s)
        ids = [v[1]["id"] for v in vs]
        pos_rows.append({
            "group": "positive", "i": i, "kind": kind,
            "expected_voice_id": VOICE_ID[kind], "voice_ids": ids,
            "send_monotonic": t_trig,
            "result": classify_g7_trial(ids, VOICE_ID[kind]),
            "evidence": [v[2] for v in vs]})
    for j, kind in enumerate(g7_control_sequence(cfg.control_each)):
        h.send(g7_reset_event(100 + j))  # back to WORKING first (silent)
        time.sleep(cfg.settle_s)
        if kind == "idle":
            # pet_state.c: agent.idle from WORKING schedules a done edge
            # (+1.5 s -> DONE + sound); let it fire before the trigger so
            # the control window observes the idle transition itself.
            h.send({"version": 1, "event": "agent.idle"})
            time.sleep(cfg.idle_settle_s)
        t_trig = h.send(g7_control_event(kind, j))
        time.sleep(cfg.observe_s)
        vs = voices_in(h.rec.all(), t_trig, t_trig + cfg.observe_s)
        ids = [v[1]["id"] for v in vs]
        ctrl_rows.append({
            "group": "control", "i": j, "kind": kind, "voice_ids": ids,
            "send_monotonic": t_trig,
            "result": classify_g7_control(ids),
            "evidence": [v[2] for v in vs]})
    pos_sum, ctrl_sum = summarize(pos_rows), summarize(ctrl_rows)
    payload.update(
        rows=pos_rows + ctrl_rows,
        summary={"positive": pos_sum, "control": ctrl_sum},
        verdict="PASS" if verdict_g7(pos_sum, ctrl_sum, cfg.trials,
                                     cfg.control_each * 3) else "FAIL")


def run_gate(link, gate, cfg, mode="serial", port=None, device_desc=None):
    if gate not in ("g3", "g4", "g6", "g7"):
        raise ValueError("unsupported gate: %r" % (gate,))
    h = Harness(link)
    payload = {
        "schema": "M9-acceptance/1",
        "contract": "M9 §3/§4",
        "gate": gate,
        "mode": mode,
        "port": port,
        "device": device_desc,
        "started_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "config": dataclasses.asdict(cfg),
    }
    try:
        if gate in ("g3", "g6"):
            _run_g36(h, payload, cfg, gate)
        elif gate == "g4":
            _run_g4(h, payload, cfg)
        else:
            _run_g7(h, payload, cfg)
    finally:
        h.close()
    payload["ended_at"] = datetime.now().astimezone().isoformat(timespec="seconds")
    payload["date"] = payload["started_at"][:10]
    return payload


# ---------------------------------------------------------------------------
# Reports (M9 §4: evidence/M9/<date>-<gate>.{json,md}, full denominators)
# ---------------------------------------------------------------------------


def _fmt(v):
    return "—" if v is None else str(v)


def _render_md(payload):
    gate = payload["gate"]
    s = payload["summary"]
    lines = []
    if gate in ("g3", "g6"):
        name = "G3 任务卡更新" if gate == "g3" else "G6 事件入口"
        lines += [
            "# M9 %s · 验收测量（harness 自动）" % name,
            "",
            "- 契约：M9 §3 %s / §4　·　日期：%s　·　模式：**%s**　·　端口：%s"
            % (gate.upper(), payload["date"], payload["mode"], payload["port"]),
            "- 判定：**%s**" % payload["verdict"],
            "- 总尝试 %d = 达标 %d ＋ 失败 %d ＋ 超时 %d（分母=总尝试，M9 §1）"
            % (s["total"], s["pass"], s["fail"], s["timeout"]),
            "- p50 = %s ms，p95 = %s ms（判线 ≤ %g ms）"
            % (_fmt(payload.get("p50_ms")), _fmt(payload.get("p95_ms")),
               payload["config"]["budget_ms"]),
            "- 原始数据：`%s-%s.json`" % (payload["date"], gate),
        ]
        if payload["mode"] == "dry-run":
            lines.append("- ⚠ **dry-run：合成设备自检，非实机证据**")
        lines += ["", "| # | 事件 | title | delta_ms | 结果 | rf 证据 |",
                  "| --- | --- | --- | --- | --- | --- |"]
        for r in payload["rows"]:
            lines.append("| %d | %s | %s | %s | %s | %s |" % (
                r["i"], r["event"], r["title"], _fmt(r["delta_ms"]),
                r["result"], _fmt(r["evidence"])))
    elif gate == "g4":
        lines += [
            "# M9 G4 失联 · 验收测量（harness 自动）",
            "",
            "- 契约：M9 §3 G4 / §4　·　日期：%s　·　模式：**%s**　·　端口：%s"
            % (payload["date"], payload["mode"], payload["port"]),
            "- 判定：**%s**" % payload["verdict"],
            "- 总尝试 %d = 达标 %d ＋ 失败 %d ＋ 超时 %d（判线：delta ∈ [%g, %g] s，≥19/20；>20 s 记 timeout）"
            % (s["total"], s["pass"], s["fail"], s["timeout"],
               payload["config"]["off_low_s"], payload["config"]["off_high_s"]),
            "- 口径：停发（最后一拍心跳）→ 收到 `off` 行（host monotonic，含 USB 链路）",
            "- 原始数据：`%s-g4.json`" % payload["date"],
        ]
        if payload["mode"] == "dry-run":
            lines.append("- ⚠ **dry-run：合成设备自检，非实机证据**")
        lines += ["", "| # | 拍数 | delta_s | 结果 | off 证据 |",
                  "| --- | --- | --- | --- | --- |"]
        for r in payload["rows"]:
            lines.append("| %d | %d | %s | %s | %s |" % (
                r["i"], r["beats"], _fmt(r["delta_s"]), r["result"],
                _fmt(r["evidence"])))
    else:  # g7
        pos, ctrl = s["positive"], s["control"]
        lines += [
            "# M9 G7 语音 · 验收测量（harness 自动）",
            "",
            "- 契约：M9 §3 G7 / §4　·　日期：%s　·　模式：**%s**　·　端口：%s"
            % (payload["date"], payload["mode"], payload["port"]),
            "- 判定：**%s**" % payload["verdict"],
            "- 正例：总尝试 %d = 达标 %d ＋ 失败 %d ＋ 超时 %d（判线：恰好 1 条正确 id 的 `voice`；id 映射 input=1/done=2/failed=3，见 pet_voice.h）"
            % (pos["total"], pos["pass"], pos["fail"], pos["timeout"]),
            "- 对照：总尝试 %d = 静默 %d ＋ 出声 %d（判线：0 条 `voice`）"
            % (ctrl["total"], ctrl["pass"], ctrl["fail"]),
            "- 原始数据：`%s-g7.json`" % payload["date"],
        ]
        if payload["mode"] == "dry-run":
            lines.append("- ⚠ **dry-run：合成设备自检，非实机证据**")
        lines += ["", "## 正例（20 次，先回 WORKING 再触发）", "",
                  "| # | kind | 期望 id | 实测 ids | 结果 | voice 证据 |",
                  "| --- | --- | --- | --- | --- | --- |"]
        for r in payload["rows"]:
            if r["group"] != "positive":
                continue
            lines.append("| %d | %s | %d | %s | %s | %s |" % (
                r["i"], r["kind"], r["expected_voice_id"], r["voice_ids"],
                r["result"], "; ".join(r["evidence"]) or "—"))
        lines += ["", "## 对照（working/idle/heartbeat ×%d，应静默）" %
                  payload["config"]["control_each"], "",
                  "| # | kind | 实测 ids | 结果 | voice 证据 |",
                  "| --- | --- | --- | --- | --- |"]
        for r in payload["rows"]:
            if r["group"] != "control":
                continue
            lines.append("| %d | %s | %s | %s | %s |" % (
                r["i"], r["kind"], r["voice_ids"], r["result"],
                "; ".join(r["evidence"]) or "—"))
    return "\n".join(lines) + "\n"


def write_reports(payload, out_dir):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    base = "%s-%s" % (payload["date"], payload["gate"])
    json_path = out / (base + ".json")
    md_path = out / (base + ".md")
    json_path.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    md_path.write_text(_render_md(payload), encoding="utf-8")
    return json_path, md_path


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def build_parser():
    parser = argparse.ArgumentParser(
        prog="acceptance",
        description="M9 acceptance harness (contract M9 §3/§4); "
                    "measures G3/G4/G6/G7 over the device serial console.")
    parser.add_argument("--port", default="COM5",
                        help="serial port (default: %(default)s)")
    parser.add_argument("--gate", required=True, choices=["g3", "g4", "g6", "g7"],
                        help="which gate to measure")
    parser.add_argument("--out", default=None,
                        help="output dir (default: <repo>/evidence/M9;"
                             " dry-run defaults to <repo>/evidence/M9/dry-run)")
    parser.add_argument("--dry-run", action="store_true",
                        help="feed synthetic console lines from a simulated"
                             " device (no serial, self-check only)")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    cfg = GateConfig()
    if args.dry_run:
        dev = DryRunDevice()
        link = DryRunLink(dev)
        mode, port, desc = "dry-run", None, dev.describe()
    else:
        link = SerialLink(args.port)
        mode, port, desc = "serial", args.port, None
    repo_evidence = Path(__file__).resolve().parent.parent / "evidence" / "M9"
    if args.out:
        out_dir = Path(args.out)
    elif args.dry_run:
        out_dir = repo_evidence / "dry-run"
    else:
        out_dir = repo_evidence
    payload = run_gate(link, args.gate, cfg, mode=mode, port=port,
                       device_desc=desc)
    json_path, md_path = write_reports(payload, out_dir)
    s = payload["summary"]
    if args.gate == "g7":
        print("M9 %s [%s] verdict=%s positive=%s control=%s"
              % (args.gate, mode, payload["verdict"], s["positive"], s["control"]))
    else:
        print("M9 %s [%s] verdict=%s total=%d pass=%d fail=%d timeout=%d"
              % (args.gate, mode, payload["verdict"],
                 s["total"], s["pass"], s["fail"], s["timeout"]))
        if "p50_ms" in payload:
            print("p50=%s ms p95=%s ms"
                 % (_fmt(payload["p50_ms"]), _fmt(payload["p95_ms"])))
    print("reports: %s" % json_path)
    print("reports: %s" % md_path)
    return 0 if payload["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
