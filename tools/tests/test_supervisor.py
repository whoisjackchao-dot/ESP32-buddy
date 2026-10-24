"""入口守护 tests (T entry): supervisor + poller + bridge + up/down/status scripts.

TDD RED→GREEN per evidence/2026-10-24-入口守护与命令-T.md. All logic is
stdlib-only and testable without Windows: build_children() constructs the
powershell command as pure data, --check only stats paths, scripts accept
BUDDY_PIDFILE/BUDDY_LOG/BUDDY_STATE/BUDDY_SUPERVISOR env overrides, and the
supervisor/tail loops take injected fake children. No test kills a real
process: pidfiles live in temp dirs and "real" pids are either this test
process itself or throwaway `sleep`/`true` children spawned by the test.
"""

import contextlib
import io
import json
import os
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, "host"))

import file_to_serial  # noqa: E402
import oc_poll  # noqa: E402
import pet_supervisor  # noqa: E402

TOOLS = os.path.join(ROOT, "tools")

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
    return tempfile.mkdtemp(prefix="buddy-sup-test-")


def wfile(path, data, mode="wb"):
    with open(path, mode) as f:
        f.write(data)


def rfile(path, mode="r"):
    with open(path, mode) as f:
        return f.read()


# ---------------------------------------------------------------------------
# pet_supervisor: backoff + path mapping + build_children (pure, no spawn)
# ---------------------------------------------------------------------------


class TestBackoff(unittest.TestCase):
    def test_sequence_doubles_to_cap(self):
        d, seq = 1.0, []
        for _ in range(7):
            seq.append(d)
            d = pet_supervisor.next_delay(d, cap=30.0)
        self.assertEqual(seq, [1.0, 2.0, 4.0, 8.0, 16.0, 30.0, 30.0])

    def test_small_base_sequence(self):
        d, seq = 0.05, []
        for _ in range(3):
            seq.append(d)
            d = pet_supervisor.next_delay(d, cap=0.2)
        self.assertEqual(seq, [0.05, 0.1, 0.2])


class TestWslToWin(unittest.TestCase):
    def test_mnt_d_maps_to_drive(self):
        self.assertEqual(
            pet_supervisor.wsl_to_win("/mnt/d/buddy_flash/oc_events.log"),
            "D:\\buddy_flash\\oc_events.log")

    def test_drive_root(self):
        self.assertEqual(pet_supervisor.wsl_to_win("/mnt/c"), "C:\\")

    def test_non_mnt_path_unchanged(self):
        self.assertEqual(pet_supervisor.wsl_to_win("/home/x/y"),
                         "/home/x/y")
        self.assertEqual(pet_supervisor.wsl_to_win("D:\\x"), "D:\\x")


class TestBuildChildren(unittest.TestCase):
    def test_poll_child_shape(self):
        kids = pet_supervisor.build_children(
            out="/mnt/d/buddy_flash/oc_events.log",
            win_python=WIN_PY, host_dir="/repo/host")
        self.assertEqual(kids[0][:3],
                         [sys.executable, "/repo/host/oc_poll.py", "--out"])
        self.assertEqual(kids[0][3], "/mnt/d/buddy_flash/oc_events.log")

    def test_bridge_child_powershell_quoting(self):
        kids = pet_supervisor.build_children(
            out="/mnt/d/buddy_flash/oc_events.log",
            win_python=WIN_PY, host_dir="/repo/host")
        self.assertEqual(kids[1][:3],
                         ["powershell.exe", "-NoProfile", "-Command"])
        cmd = kids[1][3]
        self.assertTrue(cmd.startswith("& '%s' '" % WIN_PY), cmd)
        self.assertTrue(cmd.endswith("' --log 'D:\\buddy_flash\\oc_events.log'"
                                     " --port COM5 --stop-file"
                                     " 'D:\\buddy_flash\\oc_events.log.stop'"), cmd)

    def test_win_python_env_override(self):
        old = os.environ.get("BUDDY_WIN_PYTHON")
        os.environ["BUDDY_WIN_PYTHON"] = "E:\\py\\python.exe"
        try:
            kids = pet_supervisor.build_children(host_dir="/repo/host")
        finally:
            if old is None:
                del os.environ["BUDDY_WIN_PYTHON"]
            else:
                os.environ["BUDDY_WIN_PYTHON"] = old
        self.assertIn("E:\\py\\python.exe", kids[1][3])

    def test_port_flag_changes_bridge_port(self):
        kids = pet_supervisor.build_children(
            out="/mnt/d/b/x.log", win_python=WIN_PY,
            host_dir="/repo/host", port="COM7")
        self.assertIn("--port COM7", kids[1][3])
        self.assertTrue(kids[1][3].endswith("--stop-file 'D:\\b\\x.log.stop'"))


# ---------------------------------------------------------------------------
# pet_supervisor: runtime loop (fake children only)
# ---------------------------------------------------------------------------


class TestSupervisorRun(unittest.TestCase):
    def test_restart_after_child_exit_with_backoff(self):
        d = tempdir()
        log = os.path.join(d, "sup.log")
        sup = pet_supervisor.Supervisor(
            children=[[sys.executable, "-c", "exit(1)"]],
            log_path=log, base=0.05, cap=0.2, tick=0.01)
        summary = sup.run(stop_after=1.0)
        self.assertGreaterEqual(summary["starts"], 3)
        self.assertGreaterEqual(summary["restarts"], 2)
        text = rfile(log)
        self.assertIn("start", text)
        self.assertIn("exit", text)
        # every logged line carries a timestamp prefix
        for line in text.strip().splitlines():
            self.assertRegex(line, r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2} ")

    def test_stop_terminates_running_children(self):
        d = tempdir()
        log = os.path.join(d, "sup.log")
        sup = pet_supervisor.Supervisor(
            children=[[sys.executable, "-c", "import time; time.sleep(30)"]],
            log_path=log, base=1.0, cap=2.0, tick=0.01)
        sup.run(stop_after=0.4)
        # gone within 0.4 s proves TERM, not the 30 s sleep ending
        pids = [int(line.split("pid=")[1].split()[0])
                for line in rfile(log).splitlines() if "start slot=" in line]
        self.assertEqual(len(pids), 1)
        self.assertTrue(process_gone(pids[0]))

    def test_no_restart_once_stopped_during_backoff(self):
        d = tempdir()
        sup = pet_supervisor.Supervisor(
            children=[[sys.executable, "-c", "exit(1)"]],
            log_path=os.path.join(d, "sup.log"), base=5.0, cap=10.0, tick=0.01)
        summary = sup.run(stop_after=0.6)
        self.assertEqual(summary["starts"], 1)
        self.assertEqual(summary["restarts"], 0)

    def test_state_file_tracks_children(self):
        d = tempdir()
        state = os.path.join(d, "sup.state")
        sup = pet_supervisor.Supervisor(
            children=[[sys.executable, "-c", "import time; time.sleep(30)"]],
            log_path=os.path.join(d, "sup.log"), state_path=state,
            base=1.0, cap=2.0, tick=0.01)
        sup.run(stop_after=0.4)
        data = json.loads(rfile(state))
        self.assertEqual(data["children"][0]["state"], "stopped")
        self.assertEqual(data["children"][0]["argv"],
                         [sys.executable, "-c", "import time; time.sleep(30)"])


class TestResolveChildren(unittest.TestCase):
    def test_children_json_override(self):
        kids = pet_supervisor.resolve_children(
            type("A", (), {"children": '[["echo","hi"]]', "child": [],
                           "out": "/mnt/d/b/x.log", "win_python": None,
                           "host_dir": "/repo/host", "port": "COM5"}))
        self.assertEqual(kids, [["echo", "hi"]])

    def test_child_flag_shlex(self):
        kids = pet_supervisor.resolve_children(
            type("A", (), {"children": None, "child": ['echo a b'],
                           "out": "/mnt/d/b/x.log", "win_python": None,
                           "host_dir": "/repo/host", "port": "COM5"}))
        self.assertEqual(kids, [["echo", "a", "b"]])


# ---------------------------------------------------------------------------
# pet_supervisor: --check (path checks only, no children)
# ---------------------------------------------------------------------------


class TestCheck(unittest.TestCase):
    def _argv(self, d, **over):
        a = ["--check", "--host-dir", d, "--db", os.path.join(d, "db.sqlite"),
             "--out", os.path.join(d, "out", "x.log"),
             "--win-python", os.path.join(d, "fakepython.exe")]
        return a + over.pop("extra", [])

    def _run_check(self, argv):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = pet_supervisor.main(argv)
        return rc, buf.getvalue()

    def test_all_present_exit_0(self):
        d = tempdir()
        for name in ("oc_poll.py", "file_to_serial.py", "fakepython.exe"):
            open(os.path.join(d, name), "w").close()
        open(os.path.join(d, "db.sqlite"), "w").close()
        os.mkdir(os.path.join(d, "out"))
        rc, out = self._run_check(self._argv(d))
        self.assertEqual(rc, 0)
        self.assertIn("check: OK", out)
        self.assertNotIn("MISSING", out)

    def test_missing_db_exit_1(self):
        d = tempdir()
        for name in ("oc_poll.py", "file_to_serial.py", "fakepython.exe"):
            open(os.path.join(d, name), "w").close()
        os.mkdir(os.path.join(d, "out"))
        rc, out = self._run_check(self._argv(d))
        self.assertEqual(rc, 1)
        self.assertIn("opencode db", out)
        self.assertIn("MISSING", out)

    def test_missing_scripts_exit_1(self):
        d = tempdir()
        open(os.path.join(d, "db.sqlite"), "w").close()
        os.mkdir(os.path.join(d, "out"))
        rc, out = self._run_check(self._argv(d))
        self.assertEqual(rc, 1)
        self.assertIn("oc_poll.py", out)

    def test_check_starts_no_children(self):
        # --check with a default children config must not spawn anything:
        # it only stats paths (checked implicitly: it returns before run()).
        d = tempdir()
        for name in ("oc_poll.py", "file_to_serial.py", "fakepython.exe"):
            open(os.path.join(d, name), "w").close()
        open(os.path.join(d, "db.sqlite"), "w").close()
        os.mkdir(os.path.join(d, "out"))
        t0 = time.monotonic()
        rc, _ = self._run_check(self._argv(d))
        self.assertEqual(rc, 0)
        self.assertLess(time.monotonic() - t0, 5.0)


# ---------------------------------------------------------------------------
# oc_poll: FileSink NDJSON + injectable sink via --once
# ---------------------------------------------------------------------------


class TestOcPoll(unittest.TestCase):
    def test_default_out_is_windows_mount(self):
        self.assertEqual(oc_poll.DEFAULT_OUT, "/mnt/d/buddy_flash/oc_events.log")

    def test_filesink_writes_ndjson_and_flushes(self):
        d = tempdir()
        path = os.path.join(d, "oc_events.log")
        sink = oc_poll.FileSink(path)
        sink({"version": 1, "event": "device.heartbeat"})
        # readable immediately: flushed per line
        with open(path, "rb") as f:
            self.assertEqual(f.read(), b'{"version":1,"event":"device.heartbeat"}\n')
        sink.close()

    def test_filesink_appends_across_instances(self):
        d = tempdir()
        path = os.path.join(d, "oc_events.log")
        s1 = oc_poll.FileSink(path)
        s1({"event": "a"})
        s1.close()
        s2 = oc_poll.FileSink(path)
        s2({"event": "b"})
        s2.close()
        lines = rfile(path, "rb").splitlines()
        self.assertEqual(len(lines), 2)

    def test_main_once_with_injected_sink(self):
        d = tempdir()
        fd, db = tempfile.mkstemp(suffix=".db")
        os.close(fd)
        con = sqlite3.connect(db)
        con.executescript(OC_SCHEMA)
        con.execute("insert into session values ('s1','t','/p',1,null)")
        con.execute("insert into message values ('s1',1,?)",
                    (json.dumps({"role": "assistant", "time": {"created": 1}}),))
        con.commit()
        con.close()
        got, events = [], []
        with contextlib.redirect_stderr(io.StringIO()):
            rc = oc_poll.main(
                ["--db", db, "--once"],
                sink_factory=lambda port, baud: (
                    got.append((port, baud)),
                    events.append) and (lambda obj: events.append(obj)))
        self.assertEqual(rc, 0)
        self.assertEqual(got, [("COM5", 115200)])
        self.assertEqual([e["event"] for e in events], ["task.start"])

    def test_main_once_writes_event_line_to_out(self):
        d = tempdir()
        fd, db = tempfile.mkstemp(suffix=".db")
        os.close(fd)
        con = sqlite3.connect(db)
        con.executescript(OC_SCHEMA)
        con.execute("insert into session values ('s1','t','/p',1,null)")
        con.execute("insert into message values ('s1',1,?)",
                    (json.dumps({"role": "assistant", "time": {"created": 1}}),))
        con.commit()
        con.close()
        out = os.path.join(d, "oc_events.log")
        with contextlib.redirect_stderr(io.StringIO()):
            rc = oc_poll.main(["--db", db, "--out", out, "--once"])
        self.assertEqual(rc, 0)
        line = json.loads(rfile(out))
        self.assertEqual(line["event"], "task.start")


# ---------------------------------------------------------------------------
# file_to_serial: tail logic (pure) + main loop with injected serial
# ---------------------------------------------------------------------------


class TestTail(unittest.TestCase):
    def test_read_from_start(self):
        d = tempdir()
        path = os.path.join(d, "x.log")
        wfile(path, b"abc\n")
        self.assertEqual(file_to_serial.tail(path, 0), (b"abc\n", 4))

    def test_incremental_read(self):
        d = tempdir()
        path = os.path.join(d, "x.log")
        wfile(path, b"abc\n")
        data, pos = file_to_serial.tail(path, 0)
        wfile(path, b"de\n", mode="ab")
        self.assertEqual(file_to_serial.tail(path, pos), (b"de\n", 7))

    def test_no_new_bytes(self):
        d = tempdir()
        path = os.path.join(d, "x.log")
        wfile(path, b"abc\n")
        self.assertEqual(file_to_serial.tail(path, 4), (b"", 4))

    def test_truncation_resets_position(self):
        d = tempdir()
        path = os.path.join(d, "x.log")
        wfile(path, b"abcdefgh\n")
        _, pos = file_to_serial.tail(path, 0)
        self.assertEqual(pos, 9)
        wfile(path, b"xy")  # truncated/rotated: size 2 < pos 9
        self.assertEqual(file_to_serial.tail(path, pos), (b"xy", 2))

    def test_missing_file_returns_empty(self):
        d = tempdir()
        path = os.path.join(d, "gone.log")
        self.assertEqual(file_to_serial.tail(path, 0), (b"", 0))
        self.assertEqual(file_to_serial.tail(path, 5), (b"", 5))


class TestFileToSerialMain(unittest.TestCase):
    def test_streams_new_bytes_with_banner(self):
        d = tempdir()
        log = os.path.join(d, "x.log")
        wfile(log, b'{"event":"a"}\n')  # history: skipped
        got = []
        appended = []

        class FakeSerial:
            def write(self, b):
                got.append(b)

            def close(self):
                pass

        def fake_sleep(s):
            if not appended:  # bytes arrive while the loop is running
                appended.append(1)
                wfile(log, b'{"event":"b"}\n', mode="ab")

        rc = file_to_serial.main(
            ["--log", log, "--port", "COM5", "--interval", "0.01"],
            serial_factory=lambda port: got.append(port) or FakeSerial(),
            sleep_fn=fake_sleep,
            stop_after=0.3)
        self.assertEqual(rc, 0)
        self.assertEqual(got, ["COM5", b'{"event":"b"}\n'])

    def test_banner_is_single_line(self):
        import contextlib
        import io
        d = tempdir()
        log = os.path.join(d, "x.log")
        wfile(log, b"x\n")
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            file_to_serial.main(["--log", log], serial_factory=lambda p: None,
                                sleep_fn=lambda s: None, stop_after=0.05)
        self.assertEqual(len(buf.getvalue().strip().splitlines()), 1)
        self.assertIn("[file_to_serial]", buf.getvalue())


# ---------------------------------------------------------------------------
# tools/*.sh: up/down/status with env-overridden pidfile/log/state
# ---------------------------------------------------------------------------


def run_sh(script, env_extra):
    env = os.environ.copy()
    env.update(env_extra)
    return subprocess.run(["bash", os.path.join(TOOLS, script)], env=env,
                          capture_output=True, text=True, timeout=60)


def pid_alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def process_gone(pid):
    """True once the pid is dead or a reaped-pending zombie (container init
    may not reap instantly; a zombie can no longer be signalled usefully)."""
    if not pid_alive(pid):
        return True
    try:
        with open("/proc/%d/stat" % pid) as f:
            return f.read().split(") ", 1)[1].split()[0] == "Z"
    except OSError:
        return True


class TestScripts(unittest.TestCase):
    def setUp(self):
        self.d = tempdir()
        self.pidfile = os.path.join(self.d, "b.pid")
        self.log = os.path.join(self.d, "b.log")
        self.state = os.path.join(self.d, "b.state")
        self.env = {"BUDDY_PIDFILE": self.pidfile, "BUDDY_LOG": self.log,
                    "BUDDY_STATE": self.state}

    def test_up_already_running_is_idempotent(self):
        # this test process is the "alive" pid: nothing gets signalled
        with open(self.pidfile, "w") as f:
            f.write(str(os.getpid()))
        r = run_sh("esp32-buddy-up.sh", self.env)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("already running", r.stdout)
        with open(self.pidfile) as f:
            self.assertEqual(f.read().strip(), str(os.getpid()))

    def test_up_start_down_roundtrip_with_fake_supervisor(self):
        r = run_sh("esp32-buddy-up.sh",
                   dict(self.env, BUDDY_SUPERVISOR="sleep 30"))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertNotIn("already running", r.stdout)
        pid = int(rfile(self.pidfile).strip())
        self.assertTrue(pid_alive(pid))
        r2 = run_sh("esp32-buddy-down.sh", self.env)
        self.assertEqual(r2.returncode, 0, r2.stderr)
        self.assertFalse(os.path.exists(self.pidfile))
        # wait for the fake sleep to be gone (killed only this fake child)
        for _ in range(50):
            if process_gone(pid):
                break
            time.sleep(0.1)
        self.assertTrue(process_gone(pid))

    def test_down_without_pidfile_is_idempotent(self):
        r = run_sh("esp32-buddy-down.sh", self.env)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("not running", r.stdout)

    def test_down_stale_pidfile_just_removes_it(self):
        p = subprocess.Popen(["true"])
        p.wait()  # exited pid: kill -0 will fail, nothing to signal
        with open(self.pidfile, "w") as f:
            f.write(str(p.pid))
        r = run_sh("esp32-buddy-down.sh", self.env)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertFalse(os.path.exists(self.pidfile))

    def test_status_alive_tails_log(self):
        with open(self.pidfile, "w") as f:
            f.write(str(os.getpid()))
        with open(self.log, "w") as f:
            f.write("line-one\n2026-10-24T00:00:00 latest-entry\n")
        r = run_sh("esp32-buddy-status.sh", self.env)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("running", r.stdout)
        self.assertIn("latest-entry", r.stdout)

    def test_status_without_pidfile_exit_1(self):
        r = run_sh("esp32-buddy-status.sh", self.env)
        self.assertEqual(r.returncode, 1)
        self.assertIn("not running", r.stdout)

    def test_status_dead_pidfile_reports_stale(self):
        p = subprocess.Popen(["true"])
        p.wait()
        with open(self.pidfile, "w") as f:
            f.write(str(p.pid))
        r = run_sh("esp32-buddy-status.sh", self.env)
        self.assertEqual(r.returncode, 1)
        self.assertIn("stale", r.stdout)


if __name__ == "__main__":
    unittest.main()
