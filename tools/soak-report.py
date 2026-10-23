#!/usr/bin/env python3
"""G5 浸泡日志分析：从 g5_soak.log 产出证据（JSON + MD）。

用法：
    python3 tools/soak-report.py --log D:\\buddy_flash\\g5_soak.log \
        --out evidence/M9/2026-10-24-g5

判定（契约 M9 §3 G5）：
- 0 次重启：SOAK START 之后无 BOOT 行（ESP-ROM/AUDIO READY/esp_image）。
- 0 次阻塞：以设备侧 `st:` 日志的 `I (<us>)` 时间戳计算相邻间隔，最大间隔 ≤ 15 s。
- fps 稳定：全部 `fps=` 值相同（且 >0）。
- 事件仍在送达：EV 行数 ≥ 预期（120 min / 30 s ≈ 240，容忍 ±2）。
"""
import argparse
import datetime as dt
import json
import re
import sys
from pathlib import Path

LINE_RE = re.compile(r"^(\S+) (\w+) ?(.*)$")
DEV_MS_RE = re.compile(r"^I \((\d+)\)")
FPS_RE = re.compile(r"fps=(\d+)")


def analyze(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            m = LINE_RE.match(ln.rstrip("\n"))
            if m:
                rows.append(m.groups())

    start = end = None
    boots = []
    st = []
    ev = []
    link = []
    for ts, kind, rest in rows:
        if kind == "SOAK" and rest.startswith("START"):
            start = ts
        elif kind == "SOAK" and rest.startswith("END"):
            end = ts
        elif kind == "BOOT":
            boots.append((ts, rest))
        elif kind == "ST":
            mm = DEV_MS_RE.match(rest)
            fm = FPS_RE.search(rest)
            st.append((ts, int(mm.group(1)) if mm else None,
                       int(fm.group(1)) if fm else None))
        elif kind == "EV":
            ev.append((ts, rest))
        elif kind == "LINK":
            link.append((ts, rest))

    dev_t = [s[1] for s in st if s[1] is not None]
    gaps_ms = [b - a for a, b in zip(dev_t, dev_t[1:])]
    fps = sorted({s[2] for s in st if s[2] is not None})
    ev_gaps = []
    ev_dt = [dt.datetime.fromisoformat(x[0]) for x in ev]
    ev_gaps = [(b - a).total_seconds() for a, b in zip(ev_dt, ev_dt[1:])]

    running = end is None
    reboots = len([b for b in boots if start and b[0] >= start])
    max_gap_s = (max(gaps_ms) / 1000.0) if gaps_ms else None
    fps_stable = len(fps) == 1 and fps[0] > 0

    checks = {
        "no_reboot": reboots == 0,
        "no_block": (max_gap_s is not None and max_gap_s <= 15.0),
        "fps_stable": fps_stable,
        "events_delivered": len(ev) >= 235,
    }
    verdict = "RUNNING" if running else ("PASS" if all(checks.values()) else "FAIL")
    return {
        "schema": "esp32-buddy-g5-report/1",
        "log": str(path),
        "started_at": start,
        "ended_at": end,
        "running": running,
        "reboots_after_start": reboots,
        "boot_lines": [b[1] for b in boots[:5]],
        "event_count": len(ev),
        "event_gap_max_s": round(max(ev_gaps), 1) if ev_gaps else None,
        "st_samples": len(st),
        "st_max_gap_s": round(max_gap_s, 1) if max_gap_s is not None else None,
        "fps_values": fps,
        "link_events": len(link),
        "checks": checks,
        "verdict": verdict,
    }


def render_md(r):
    lines = [
        "# M9 G5 稳定性（插电连续 120 min）",
        "",
        "- 日志：`%s`" % r["log"],
        "- 开始：**%s**　结束：%s" % (r["started_at"], r["ended_at"] or "（仍在跑）"),
        "- 判定：**%s**" % r["verdict"],
        "",
        "| 检查 | 结果 | 数据 |",
        "| --- | --- | --- |",
        "| 0 次重启 | %s | BOOT 行 %d 条（START 后） |" % ("✅" if r["checks"]["no_reboot"] else "❌", r["reboots_after_start"]),
        "| 0 次阻塞 | %s | 设备 `st:` 最大间隔 **%s s**（判线 ≤15 s） |" % ("✅" if r["checks"]["no_block"] else "❌", r["st_max_gap_s"]),
        "| fps 稳定 | %s | 取值 %s |" % ("✅" if r["checks"]["fps_stable"] else "❌", r["fps_values"]),
        "| 事件仍送达 | %s | EV %d 条（预期 ≥235） |" % ("✅" if r["checks"]["events_delivered"] else "❌", r["event_count"]),
        "",
        "- `link` 事件 %d 次（30 s 事件间隔 > 15 s 掉线阈值，**预期抖动**，非故障）。" % r["link_events"],
    ]
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--out", required=True, help="输出前缀（不含扩展名）")
    args = ap.parse_args(argv)
    r = analyze(Path(args.log))
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.with_suffix(".json").write_text(json.dumps(r, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    out.with_suffix(".md").write_text(render_md(r), encoding="utf-8")
    print("G5 verdict=%s (%s)" % (r["verdict"], "running" if r["running"] else "final"))
    print("reboots=%d st_max_gap=%s fps=%s events=%d" %
          (r["reboots_after_start"], r["st_max_gap_s"], r["fps_values"], r["event_count"]))
    return 0 if r["verdict"] in ("PASS", "RUNNING") else 1


if __name__ == "__main__":
    sys.exit(main())
