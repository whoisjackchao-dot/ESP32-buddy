"""M9 acceptance harness tests (contract M9 §3/§4).

Pure-logic unit tests (line parsing, rf matching, stats with timeouts in
the denominator, verdict boundaries, g7 duplicate detection) plus
dry-run integration tests that drive the whole gate loop against a
simulated device (DryRunDevice/DryRunLink). No test touches pyserial or
real hardware: the serial layer is only constructed in SerialLink, which
imports serial lazily.
"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import acceptance as acc


def ev(recv, kind, **kw):
    return (recv, dict(kind=kind, **kw), "raw-%s" % kind)


# ---------------------------------------------------------------------------
# Device console line parsing (M9 §2 formats)
# ---------------------------------------------------------------------------


class TestParseDeviceLine(unittest.TestCase):
    def test_rf_full_line(self):
        got = acc.parse_device_line("rf t=123456 st=2 n=1 top=G300001")
        self.assertEqual(
            got, {"kind": "rf", "t_us": 123456, "st": 2, "n": 1, "top": "G300001"})

    def test_rf_strips_crlf_and_spaces(self):
        got = acc.parse_device_line("  rf t=7 st=0 n=0 top=\r\n")
        self.assertEqual(got["top"], "")
        self.assertEqual(got["t_us"], 7)

    def test_off_line(self):
        self.assertEqual(acc.parse_device_line("off t=99"),
                         {"kind": "off", "t_us": 99})

    def test_on_line(self):
        self.assertEqual(acc.parse_device_line("on t=100"),
                         {"kind": "on", "t_us": 100})

    def test_voice_line(self):
        self.assertEqual(acc.parse_device_line("voice id=2 t=55"),
                         {"kind": "voice", "id": 2, "t_us": 55})

    def test_st_heartbeat_ignored(self):
        self.assertIsNone(acc.parse_device_line("st: up=12s heap=200K fps=28"))

    def test_arbitrary_log_ignored(self):
        self.assertIsNone(acc.parse_device_line("I (3200) pet: hello"))

    def test_empty_line_ignored(self):
        self.assertIsNone(acc.parse_device_line(""))
        self.assertIsNone(acc.parse_device_line("   \r\n"))

    def test_voice_bad_id_ignored(self):
        self.assertIsNone(acc.parse_device_line("voice id=x t=55"))

    def test_rf_missing_field_ignored(self):
        self.assertIsNone(acc.parse_device_line("rf t=5 st=1 n=1"))

    def test_off_extra_garbage_ignored(self):
        self.assertIsNone(acc.parse_device_line("off t=5 boom"))


# ---------------------------------------------------------------------------
# Matching by title / kind windows (pure, over (recv, ev, raw) triples)
# ---------------------------------------------------------------------------

    def test_parse_prefixed_idf_log_lines(self):
        """Real device lines carry the ESP_LOG prefix: 'I (us) tag: rf ...'."""
        got = acc.parse_device_line(
            "I (18735) pet-engine: rf t=17847677 st=1 n=3 top=ALPHA")
        self.assertEqual(got["kind"], "rf")
        self.assertEqual(got["st"], 1)
        self.assertEqual(got["n"], 3)
        self.assertEqual(got["top"], "ALPHA")
        self.assertEqual(
            acc.parse_device_line("I (99) pet-engine: off t=123")["kind"], "off")
        self.assertEqual(
            acc.parse_device_line("I (99) pet-engine: on t=124")["kind"], "on")
        self.assertEqual(
            acc.parse_device_line("I (99) pet-engine: voice id=2 t=125")["id"], 2)
        self.assertIsNone(acc.parse_device_line("I (3200) pet-engine: st: fps=7"))



class TestFindRf(unittest.TestCase):
    def setUp(self):
        self.events = [
            ev(1.0, "rf", t_us=1, st=2, n=1, top="OLD"),
            ev(2.0, "rf", t_us=2, st=2, n=1, top="G300001"),
            ev(2.5, "voice", id=1, t_us=3),
            ev(3.0, "rf", t_us=4, st=3, n=1, top="G300002"),
            ev(4.0, "rf", t_us=5, st=3, n=1, top="G300001X"),
        ]

    def test_first_matching_after_since(self):
        m = acc.find_rf(self.events, "G300001", since=1.5)
        self.assertEqual(m[0], 2.0)
        self.assertEqual(m[1]["top"], "G300001")

    def test_ignores_rf_before_since(self):
        self.assertIsNone(acc.find_rf(self.events, "G300001", since=2.1))

    def test_ignores_other_top(self):
        m = acc.find_rf(self.events, "G300002", since=0.0)
        self.assertEqual(m[0], 3.0)

    def test_none_when_absent(self):
        self.assertIsNone(acc.find_rf(self.events, "NOPE", since=0.0))

    def test_matches_long_title_by_8char_prefix(self):
        # device stores the full title and prints its first <=8 chars:
        # title "G300001XYZ..." shows as top=G300001X
        m = acc.find_rf(self.events, "G300001XYZLONG", since=3.5)
        self.assertEqual(m[0], 4.0)
        # the 7-char top at 2.0 differs from the 8-char key: no match
        self.assertIsNone(acc.find_rf(self.events[:2], "G300001XYZLONG", since=0.0))


class TestWindowsAndKind(unittest.TestCase):
    def test_voices_in_window_bounds(self):
        events = [
            ev(1.0, "voice", id=1, t_us=1),
            ev(1.5, "voice", id=2, t_us=2),
            ev(2.0, "voice", id=3, t_us=3),
        ]
        ids = [t[1]["id"] for t in acc.voices_in(events, 1.0, 2.0)]
        self.assertEqual(ids, [1, 2, 3])

    def test_voices_outside_window_excluded(self):
        events = [
            ev(0.9, "voice", id=1, t_us=1),
            ev(1.0, "voice", id=2, t_us=2),
            ev(2.1, "voice", id=3, t_us=3),
        ]
        ids = [t[1]["id"] for t in acc.voices_in(events, 1.0, 2.0)]
        self.assertEqual(ids, [2])

    def test_find_kind_off_after_since(self):
        events = [ev(1.0, "off", t_us=1), ev(5.0, "off", t_us=2)]
        m = acc.find_kind(events, "off", since=2.0)
        self.assertEqual(m[0], 5.0)
        self.assertIsNone(acc.find_kind(events, "off", since=6.0))


# ---------------------------------------------------------------------------
# Stats: timeouts stay in the denominator (M9 §1)
# ---------------------------------------------------------------------------


class TestStats(unittest.TestCase):
    def test_summarize_counts_timeouts_in_total(self):
        rows = [{"result": "pass"}, {"result": "fail"},
                {"result": "timeout"}, {"result": "pass"}]
        s = acc.summarize(rows)
        self.assertEqual(s, {"total": 4, "pass": 2, "fail": 1, "timeout": 1})

    def test_summarize_empty(self):
        self.assertEqual(acc.summarize([]),
                         {"total": 0, "pass": 0, "fail": 0, "timeout": 0})

    def test_percentile_nearest_rank(self):
        values = [float(i) for i in range(1, 21)]  # 1..20
        self.assertEqual(acc.percentile(values, 50), 10.0)
        self.assertEqual(acc.percentile(values, 95), 19.0)
        self.assertEqual(acc.percentile(values, 100), 20.0)

    def test_percentile_empty_none(self):
        self.assertIsNone(acc.percentile([], 95))


# ---------------------------------------------------------------------------
# G3/G6 verdict boundaries: all trials <= 1000 ms (M9 §3 G3/G6)
# ---------------------------------------------------------------------------


class TestVerdictG36(unittest.TestCase):
    def test_classify_boundary_1000ms_inclusive(self):
        self.assertEqual(acc.classify_g36(1000.0, 1000.0), "pass")
        self.assertEqual(acc.classify_g36(999.9, 1000.0), "pass")
        self.assertEqual(acc.classify_g36(1000.1, 1000.0), "fail")

    def test_classify_no_match_is_timeout(self):
        self.assertEqual(acc.classify_g36(None, 1000.0), "timeout")

    def test_all_within_budget_pass(self):
        rows = [{"result": "pass"} for _ in range(20)]
        self.assertTrue(acc.verdict_g36(acc.summarize(rows), 20))

    def test_one_over_budget_fails(self):
        rows = [{"result": "pass"} for _ in range(19)] + [{"result": "fail"}]
        self.assertFalse(acc.verdict_g36(acc.summarize(rows), 20))

    def test_timeout_row_fails_and_stays_in_total(self):
        rows = [{"result": "pass"} for _ in range(19)] + [{"result": "timeout"}]
        s = acc.summarize(rows)
        self.assertEqual(s["total"], 20)
        self.assertFalse(acc.verdict_g36(s, 20))


# ---------------------------------------------------------------------------
# G4 verdict boundaries: >=19/20 deltas in [15.0, 17.0] s (M9 §3 G4)
# ---------------------------------------------------------------------------


class TestVerdictG4(unittest.TestCase):
    def test_classify_bounds_inclusive(self):
        self.assertEqual(acc.classify_g4(15.0, 15.0, 17.0), "pass")
        self.assertEqual(acc.classify_g4(17.0, 15.0, 17.0), "pass")
        self.assertEqual(acc.classify_g4(14.99, 15.0, 17.0), "fail")
        self.assertEqual(acc.classify_g4(17.01, 15.0, 17.0), "fail")

    def test_classify_no_off_is_timeout(self):
        self.assertEqual(acc.classify_g4(None, 15.0, 17.0), "timeout")

    def test_19of20_pass(self):
        rows = [{"result": "pass"} for _ in range(19)] + [{"result": "fail"}]
        self.assertTrue(acc.verdict_g4(acc.summarize(rows), 20, 19))

    def test_18of20_fail(self):
        rows = [{"result": "pass"} for _ in range(18)] + [{"result": "fail"}] * 2
        self.assertFalse(acc.verdict_g4(acc.summarize(rows), 20, 19))

    def test_19pass_1_timeout_pass(self):
        rows = [{"result": "pass"} for _ in range(19)] + [{"result": "timeout"}]
        s = acc.summarize(rows)
        self.assertEqual(s["total"], 20)
        self.assertTrue(acc.verdict_g4(s, 20, 19))

    def test_18pass_2_timeout_fail(self):
        rows = [{"result": "pass"} for _ in range(18)] + [{"result": "timeout"}] * 2
        self.assertFalse(acc.verdict_g4(acc.summarize(rows), 20, 19))


# ---------------------------------------------------------------------------
# G7: exactly 1 matching voice id; duplicates/missing/wrong id fail;
# control must be silent (M9 §3 G7)
# ---------------------------------------------------------------------------


class TestVerdictG7(unittest.TestCase):
    def test_trial_one_correct_id_pass(self):
        self.assertEqual(acc.classify_g7_trial([2], 2), "pass")

    def test_trial_duplicate_fails(self):
        self.assertEqual(acc.classify_g7_trial([2, 2], 2), "duplicate")

    def test_trial_extra_same_id_is_duplicate(self):
        self.assertEqual(acc.classify_g7_trial([1, 1, 1], 1), "duplicate")

    def test_trial_wrong_id_fails(self):
        self.assertEqual(acc.classify_g7_trial([3], 2), "wrong_id")

    def test_trial_missing_fails(self):
        self.assertEqual(acc.classify_g7_trial([], 1), "missing")

    def test_control_silent_pass(self):
        self.assertEqual(acc.classify_g7_control([]), "pass")

    def test_control_any_voice_fails(self):
        self.assertEqual(acc.classify_g7_control([2]), "fail")
        self.assertEqual(acc.classify_g7_control([1, 1]), "fail")

    def test_trial_summary_then_verdict(self):
        pos = acc.summarize([{"result": "pass"}] * 20)
        ctrl = acc.summarize([{"result": "pass"}] * 15)
        self.assertTrue(acc.verdict_g7(pos, ctrl, 20, 15))
        pos["pass"] = 19
        self.assertFalse(acc.verdict_g7(pos, ctrl, 20, 15))

    def test_sequence_counts_7_7_6(self):
        seq = acc.g7_sequence(20)
        self.assertEqual(len(seq), 20)
        self.assertEqual(seq.count("input_required"), 7)
        self.assertEqual(seq.count("done"), 7)
        self.assertEqual(seq.count("failed"), 6)

    def test_control_sequence_counts(self):
        seq = acc.g7_control_sequence(5)
        self.assertEqual(len(seq), 15)
        for kind in ("working", "idle", "heartbeat"):
            self.assertEqual(seq.count(kind), 5)

    def test_expected_voice_ids_match_pet_voice_h(self):
        # pet_voice.h: PET_VOICE_INPUT=1, PET_VOICE_DONE=2, PET_VOICE_FAILED=3
        self.assertEqual(acc.VOICE_ID,
                         {"input_required": 1, "done": 2, "failed": 3})


# ---------------------------------------------------------------------------
# Event builders (M4 §6.1 wire format, distinct <=8 char titles)
# ---------------------------------------------------------------------------


class TestEventBuilders(unittest.TestCase):
    def test_titles_distinct_and_short(self):
        titles = [acc.make_title("G3", i) for i in range(20)]
        self.assertEqual(len(set(titles)), 20)
        for t in titles:
            self.assertLessEqual(len(t), acc.LOG_TITLE_MAX)

    def test_g3_event_shape(self):
        e = acc.card_event("G3", 0, 75)
        self.assertEqual(e["version"], 1)
        self.assertEqual(e["event"], "task.start")
        self.assertEqual(e["title"], acc.make_title("G3", 0))
        self.assertEqual(e["elapsed_s"], 75)
        acc.validate_event(e)  # must not raise

    def test_g6_rotation_uses_known_events(self):
        kinds = {acc.g6_event("G6", i, i)["event"] for i in range(20)}
        self.assertTrue(kinds <= acc.KNOWN_EVENTS)
        self.assertNotIn("device.heartbeat", kinds)  # heartbeat never shows rf

    def test_validate_rejects_unknown_event(self):
        with self.assertRaises(ValueError):
            acc.validate_event({"version": 1, "event": "nope"})

    def test_send_line_over_512_raises(self):
        e = {"version": 1, "event": "task.start",
             "title": "T" * 600, "project": "P" * 600, "elapsed_s": 1}
        with self.assertRaises(ValueError):
            acc.validate_event(e)


# ---------------------------------------------------------------------------
# Serial layer stays lazy: importing the harness never imports pyserial
# ---------------------------------------------------------------------------


class TestNoSerialImport(unittest.TestCase):
    def test_serial_not_imported(self):
        self.assertNotIn("serial", sys.modules)


# ---------------------------------------------------------------------------
# Dry-run integration: whole gate loop against the simulated device
# ---------------------------------------------------------------------------


FAST_CFG = acc.GateConfig(
    trials=5,
    rf_timeout_s=1.0,
    budget_ms=1000.0,
    beat_interval_s=0.05,
    beat_seconds=0.25,
    off_wait_s=2.0,
    off_low_s=0.40,
    off_high_s=0.80,
    g4_min_pass=5,
    settle_s=0.10,
    observe_s=0.30,
    idle_settle_s=0.30,
    control_each=1,
    poll_s=0.005,
)

FAST_DEV = {
    "latency_s": 0.03,
    "latency_jitter_s": 0.01,
    "link_timeout_s": 0.60,
    "debounce_s": 0.10,
    "dwell_s": 0.50,
}


def run_fast(gate, **dev_kw):
    dev_kw.setdefault("noise", False)
    dev = acc.DryRunDevice(**{**FAST_DEV, **dev_kw})
    link = acc.DryRunLink(dev)
    return acc.run_gate(link, gate, FAST_CFG, mode="dry-run", port=None,
                        device_desc=dev.describe())


class TestDryRunGates(unittest.TestCase):
    def test_g3_fast_pass_and_reports_written(self):
        payload = run_fast("g3")
        self.assertEqual(payload["verdict"], "PASS")
        self.assertEqual(payload["summary"]["total"], 5)
        self.assertEqual(payload["summary"]["pass"], 5)
        self.assertIsNotNone(payload["p50_ms"])
        with tempfile.TemporaryDirectory() as td:
            jp, mp = acc.write_reports(payload, Path(td))
            self.assertTrue(jp.exists() and mp.exists())
            loaded = json.loads(jp.read_text(encoding="utf-8"))
            self.assertEqual(loaded["gate"], "g3")
            self.assertEqual(loaded["summary"]["pass"], 5)
            self.assertEqual(len(loaded["rows"]), 5)
            self.assertIn("delta_ms", loaded["rows"][0])

    def test_g6_fast_pass(self):
        payload = run_fast("g6")
        self.assertEqual(payload["verdict"], "PASS")
        events = {r["event"] for r in payload["rows"]}
        self.assertTrue(events <= acc.KNOWN_EVENTS - {"device.heartbeat"})

    def test_g4_fast_pass_within_bounds(self):
        payload = run_fast("g4")
        self.assertEqual(payload["verdict"], "PASS")
        for r in payload["rows"]:
            self.assertEqual(r["result"], "pass")
            self.assertGreaterEqual(r["delta_s"], FAST_CFG.off_low_s)
            self.assertLessEqual(r["delta_s"], FAST_CFG.off_high_s)

    def test_g7_fast_pass_positive_and_control(self):
        payload = run_fast("g7")
        self.assertEqual(payload["verdict"], "PASS")
        pos = [r for r in payload["rows"] if r["group"] == "positive"]
        ctrl = [r for r in payload["rows"] if r["group"] == "control"]
        self.assertEqual(len(pos), FAST_CFG.trials)
        self.assertEqual(len(ctrl), FAST_CFG.control_each * 3)
        for r in pos:
            self.assertEqual(r["result"], "pass")
            self.assertEqual(r["voice_ids"], [acc.VOICE_ID[r["kind"]]])
        for r in ctrl:
            self.assertEqual(r["result"], "pass")
            self.assertEqual(r["voice_ids"], [])

    def test_g3_dropped_rf_is_timeout_in_denominator(self):
        # device never shows the 3rd title: trial must become timeout,
        # stay in the denominator and fail the gate
        drop = acc.make_title("G3", 2)
        payload = run_fast("g3", drop_titles={drop})
        self.assertEqual(payload["verdict"], "FAIL")
        self.assertEqual(payload["summary"]["total"], 5)
        self.assertEqual(payload["summary"]["timeout"], 1)
        self.assertEqual(payload["summary"]["pass"], 4)

    def test_g4_silent_device_times_out(self):
        # device that never goes offline: every trial is a timeout, all
        # stay in the denominator, gate fails
        payload = run_fast("g4", link_timeout_s=999.0)
        self.assertEqual(payload["verdict"], "FAIL")
        self.assertEqual(payload["summary"]["total"], FAST_CFG.trials)
        self.assertEqual(payload["summary"]["timeout"], FAST_CFG.trials)

    def test_junk_lines_do_not_break_gate(self):
        dev_kw = dict(FAST_DEV)
        dev_kw["noise"] = True
        dev = acc.DryRunDevice(**dev_kw)
        payload = acc.run_gate(acc.DryRunLink(dev), "g3", FAST_CFG,
                               mode="dry-run", port=None,
                               device_desc=dev.describe())
        self.assertEqual(payload["verdict"], "PASS")


if __name__ == "__main__":
    unittest.main()
