"""T entry: entry supervisor keeping the OpenCode poller + serial bridge alive.

Children are plain argv lists (default: oc_poll.py on WSL, and the
Windows bridge via powershell.exe). A child that exits is restarted with
exponential backoff (1 s -> 2 -> 4 ... capped at 30 s); a child that ran
for >= the cap is considered stable and its next restart waits only the
base delay again. SIGINT/SIGTERM terminate all children and exit cleanly.

Everything is stdlib-only and inspectable without spawning:
- build_children() / wsl_to_win() are pure command construction;
- --check only stats paths (works on WSL, starts nothing);
- --children/--child override the default children for tests;
- the run loop takes injected clock/sleep/popen for tests.
"""

import argparse
import json
import os
import re
import shlex
import signal
import subprocess
import sys
import time

DEFAULT_OUT = "/mnt/d/buddy_flash/oc_events.log"
DEFAULT_DB = os.path.expanduser("~/.local/share/opencode/opencode.db")
DEFAULT_WIN_PYTHON = (r"D:\Espressif\python_env"
                      r"\idf5.5_py3.14_env\Scripts\python.exe")
DEFAULT_PORT = "COM5"  # §9.3: device sits on COM5 (USB-Serial-JTAG)
BACKOFF_BASE = 1.0
BACKOFF_CAP = 30.0


def default_win_python():
    return os.environ.get("BUDDY_WIN_PYTHON") or DEFAULT_WIN_PYTHON


def wsl_to_win(path):
    """/mnt/d/x/y -> D:\\x\\y; /mnt/d -> D:\\; others pass through."""
    m = re.match(r"^/mnt/([A-Za-z])(?:/(.*))?$", path)
    if m is None:
        return path
    rest = (m.group(2) or "").replace("/", "\\")
    return "%s:\\%s" % (m.group(1).upper(), rest) if rest \
        else "%s:\\" % m.group(1).upper()


def win_to_wsl(path):
    """D:\\x\\y -> /mnt/d/x/y (best effort, for --check path candidates)."""
    m = re.match(r"^([A-Za-z]):[\\/](.*)$", path)
    if m is None:
        return path
    return "/mnt/%s/%s" % (m.group(1).lower(), m.group(2).replace("\\", "/"))


def build_children(out=DEFAULT_OUT, win_python=None, host_dir=None,
                   port=DEFAULT_PORT):
    """Default child commands as argv lists (pure data, nothing spawned).

    Dual transport: the poller mirrors every line to the device over
    WiFi TCP (--tcp auto: host from <out>.ip, port 8766) while the file
    bridge records the device IP announced on the console (<out>.ip).
    """
    win_python = win_python or default_win_python()
    host_dir = host_dir or os.path.dirname(os.path.abspath(__file__))
    poll = [sys.executable, os.path.join(host_dir, "oc_poll.py"),
            "--out", out, "--tcp", "auto", "--ip-file", out + ".ip"]
    ps = "& '%s' '%s' --log '%s' --port %s --stop-file '%s' --ip-file '%s'" % (
        win_python,
        wsl_to_win(os.path.join(host_dir, "file_to_serial.py")),
        wsl_to_win(out),
        port,
        wsl_to_win(out) + ".stop",
        wsl_to_win(out) + ".ip")
    bridge = ["powershell.exe", "-NoProfile", "-Command", ps]
    return [poll, bridge]


def next_delay(current, cap=BACKOFF_CAP):
    """Exponential backoff step: double, capped."""
    return min(current * 2.0, cap)


class Slot:
    """One supervised child and its restart bookkeeping."""

    def __init__(self, index, argv):
        self.index = index
        self.argv = argv
        self.proc = None
        self.phase = "stopped"      # running / backing-off / stopped
        self.delay = BACKOFF_BASE   # set to base by the supervisor
        self.next_start = 0.0
        self.started_at = None
        self.ever_started = False
        self.restarts = 0


class Supervisor:
    """Single-threaded supervise loop over a fixed child list."""

    def __init__(self, children, log_path=None, state_path=None,
                 base=BACKOFF_BASE, cap=BACKOFF_CAP, tick=0.05,
                 popen_factory=None, clock=time.monotonic, sleep=time.sleep):
        self.base, self.cap, self.tick = float(base), float(cap), tick
        self._clock, self._sleep = clock, sleep
        self._popen = popen_factory or subprocess.Popen
        self._stop = False
        self._starts = 0
        self._logf = open(log_path, "a", encoding="utf-8") if log_path else None
        self._state_path = state_path
        self.slots = [Slot(i, argv) for i, argv in enumerate(children)]
        for slot in self.slots:
            slot.delay = self.base
        self._log("supervisor pid=%d children=%d"
                  % (os.getpid(), len(self.slots)))

    # -- logging / state ---------------------------------------------------

    def _log(self, msg):
        line = "%s %s" % (time.strftime("%Y-%m-%dT%H:%M:%S"), msg)
        if self._logf is not None:
            self._logf.write(line + "\n")
            self._logf.flush()
        else:
            print(line, flush=True)

    def _write_state(self):
        if not self._state_path:
            return
        data = {
            "pid": os.getpid(),
            "updated": time.strftime("%Y-%m-%dT%H:%M:%S"),
            "children": [
                {"slot": s.index,
                 "argv": s.argv,
                 "pid": s.proc.pid if s.proc is not None else None,
                 "state": s.phase,
                 "restarts": s.restarts,
                 "delay": s.delay}
                for s in self.slots],
        }
        tmp = self._state_path + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False)
        os.replace(tmp, self._state_path)

    # -- child lifecycle ---------------------------------------------------

    def _argv_str(self, argv):
        return " ".join(shlex.quote(x) for x in argv)

    def _start(self, slot, now):
        first = not slot.ever_started
        slot.proc = self._popen(slot.argv)
        slot.started_at = now
        slot.ever_started = True
        slot.phase = "running"
        self._starts += 1
        self._log("start slot=%d pid=%d argv=%s"
                  % (slot.index, slot.proc.pid, self._argv_str(slot.argv)))
        if not first:
            slot.restarts += 1
        self._write_state()

    def _on_exit(self, slot, now):
        rc = slot.proc.returncode
        uptime = now - (slot.started_at or now)
        wait = slot.delay
        slot.proc = None
        slot.phase = "backing-off"
        slot.next_start = now + wait
        slot.delay = (self.base if uptime >= self.cap
                      else next_delay(slot.delay, cap=self.cap))
        self._log("exit slot=%d rc=%s uptime=%.1fs restart in %.2fs"
                  % (slot.index, rc, uptime, wait))
        self._write_state()

    def _terminate_all(self):
        # 先写停止文件：Windows 桥是 powershell 的孙进程，SIGTERM 杀不到
        for slot in self.slots:
            joined = " ".join(str(a) for a in (getattr(slot, "argv", None) or []))
            m = re.search(r"--stop-file '?([^'\s]+)'?", joined)
            if m:
                try:
                    open(win_to_wsl(m.group(1)), "w").close()
                except OSError:
                    pass
        for slot in self.slots:
            if slot.proc is not None and slot.proc.poll() is None:
                try:
                    slot.proc.terminate()
                except OSError:
                    pass
        deadline = self._clock() + 2.0
        for slot in self.slots:
            while (slot.proc is not None and slot.proc.poll() is None
                   and self._clock() < deadline):
                self._sleep(self.tick)
        for slot in self.slots:
            if slot.proc is not None and slot.proc.poll() is None:
                try:
                    slot.proc.kill()
                    slot.proc.poll()
                except OSError:
                    pass
            slot.proc = None
            slot.phase = "stopped"
        self._log("supervisor stop")
        self._write_state()

    # -- main loop ---------------------------------------------------------

    def request_stop(self):
        self._stop = True

    def run(self, stop_after=None):
        deadline = (self._clock() + stop_after
                    if stop_after is not None else None)
        # 清掉上次关停留下的停止文件，否则桥一启动就退出
        for slot in self.slots:
            joined = " ".join(str(a) for a in (getattr(slot, "argv", None) or []))
            m = re.search(r"--stop-file '?([^'\s]+)'?", joined)
            if m:
                try:
                    os.remove(win_to_wsl(m.group(1)))
                except OSError:
                    pass
        try:
            while not self._stop:
                now = self._clock()
                if deadline is not None and now >= deadline:
                    break
                for slot in self.slots:
                    if slot.proc is not None:
                        if slot.proc.poll() is not None:
                            self._on_exit(slot, now)
                    elif now >= slot.next_start:
                        self._start(slot, now)
                self._sleep(self.tick)
        finally:
            self._terminate_all()
        return {"starts": self._starts,
                "restarts": self._starts
                - sum(1 for s in self.slots if s.ever_started)}


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def resolve_children(args):
    """Children from --children JSON and/or repeated --child, else defaults."""
    kids = []
    if args.children:
        kids.extend(json.loads(args.children))
    kids.extend(shlex.split(c) for c in (args.child or []))
    if not kids:
        return build_children(out=args.out, win_python=args.win_python,
                              host_dir=args.host_dir, port=args.port)
    for kid in kids:
        if not (isinstance(kid, list) and kid
                and all(isinstance(x, str) for x in kid)):
            raise SystemExit(
                "each child must be a non-empty argv list of strings")
    return kids


def cmd_check(args):
    """Path table; exit 0 iff everything exists. Starts nothing."""
    host = args.host_dir or os.path.dirname(os.path.abspath(__file__))
    winpy = args.win_python or default_win_python()
    out = args.out or DEFAULT_OUT
    rows = [
        ("oc_poll.py", [os.path.join(host, "oc_poll.py")]),
        ("file_to_serial.py", [os.path.join(host, "file_to_serial.py")]),
        ("win python", [winpy, win_to_wsl(winpy)]),
        ("opencode db", [os.path.expanduser(args.db)]),
        ("out dir", [os.path.dirname(out) or "."]),
    ]
    ok = True
    print("%-18s %-8s %s" % ("name", "status", "path"))
    for name, candidates in rows:
        hit = next((c for c in candidates if os.path.exists(c)), None)
        ok = ok and hit is not None
        print("%-18s %-8s %s" % (name, "OK" if hit else "MISSING",
                                 hit or " | ".join(candidates)))
    print("check: %s" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


def build_parser():
    parser = argparse.ArgumentParser(
        prog="pet_supervisor",
        description=("Supervise the ESP32-buddy entry children"
                     " (OpenCode poller + Windows serial bridge)."),
    )
    parser.add_argument("--log", default="",
                        help="supervisor log file (default: stdout)")
    parser.add_argument("--state", default="",
                        help="child-state JSON (default: <log>.state)")
    parser.add_argument("--out", default=DEFAULT_OUT,
                        help="event log passed to oc_poll (default: %(default)s)")
    parser.add_argument("--db", default=DEFAULT_DB,
                        help="OpenCode SQLite path, --check only")
    parser.add_argument("--win-python", default=None,
                        help="Windows python path (default: $BUDDY_WIN_PYTHON"
                             " or the idf5.5 py3.14 env)")
    parser.add_argument("--port", default=DEFAULT_PORT,
                        help="device serial port (default: %(default)s)")
    parser.add_argument("--host-dir", default=None,
                        help="directory holding oc_poll.py/file_to_serial.py"
                             " (default: this file's directory)")
    parser.add_argument("--children", default=None,
                        help="override children as a JSON list of argv lists")
    parser.add_argument("--child", action="append", default=[],
                        help="extra child as one argv string (repeatable)")
    parser.add_argument("--check", action="store_true",
                        help="verify paths and exit (no children started)")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)
    if args.check:
        return cmd_check(args)
    children = resolve_children(args)
    state = args.state
    if not state and args.log:
        state = os.path.splitext(args.log)[0] + ".state"
    sup = Supervisor(children, log_path=args.log or None,
                     state_path=state or None)

    def on_signal(signum, frame):
        sup.request_stop()

    for sig in (signal.SIGINT, signal.SIGTERM):
        signal.signal(sig, on_signal)
    sup.run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
