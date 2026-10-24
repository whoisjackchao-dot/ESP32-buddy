#!/usr/bin/env python3
"""G1 六态目视驱动（20 观测；用户 2026-10-24 决定的缩减口径）。

运行在**有设备的一侧**（Windows；pyserial 直连 COM5）：
    python g1-drive.py --port COM5 --out evidence/M9/2026-10-24-g1.json

序列（20 步，6 态全覆盖；每步 = 一次"进入"观测，上一步的"退出"同时可判）：
  1-6   WORKING/INPUT/FAILED/IDLE/DONE/IDLE(驻留)
  7-12  同 1-6（第二轮）
  13-16 WORKING/INPUT/FAILED/IDLE
  17    OFFLINE（停发 16 s）
  18    WORKING（恢复）
  19    OFFLINE（停发 16 s）
  20    DONE（恢复并进入 DONE）

操作者：屏幕每步应显示对应形态；结束时把 20/20（或异常步号）回报给记录者。
"""
import argparse
import datetime as dt
import json
import time

import serial

# (期望态, 发送事件, 发送后等待秒数) —— None 表示停发（等掉线）
STEPS = [
    ("WORKING", {"event": "task.start", "title": "G1-W1"}, 2.0),
    ("INPUT", {"event": "agent.input_required", "title": "G1-I1"}, 2.0),
    ("FAILED", {"event": "task.error", "title": "G1-F1"}, 2.0),
    ("IDLE", {"event": "agent.idle"}, 2.0),
    ("DONE", {"event": "task.done", "title": "G1-D1"}, 2.0),
    ("IDLE", None, 6.5),  # DONE 驻留 5s 后回 IDLE
    ("WORKING", {"event": "task.start", "title": "G1-W2"}, 2.0),
    ("INPUT", {"event": "agent.input_required", "title": "G1-I2"}, 2.0),
    ("FAILED", {"event": "task.error", "title": "G1-F2"}, 2.0),
    ("IDLE", {"event": "agent.idle"}, 2.0),
    ("DONE", {"event": "task.done", "title": "G1-D2"}, 2.0),
    ("IDLE", None, 6.5),
    ("WORKING", {"event": "task.start", "title": "G1-W3"}, 2.0),
    ("INPUT", {"event": "agent.input_required", "title": "G1-I3"}, 2.0),
    ("FAILED", {"event": "task.error", "title": "G1-F3"}, 2.0),
    ("IDLE", {"event": "agent.idle"}, 2.0),
    ("OFFLINE", None, 16.5),  # 停发 16 s → 灰化
    ("WORKING", {"event": "task.start", "title": "G1-R1"}, 2.0),  # 恢复
    ("OFFLINE", None, 16.5),
    ("DONE", {"event": "task.done", "title": "G1-R2"}, 2.0),  # 恢复并进 DONE
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    ser = serial.Serial(args.port, 115200, timeout=0.05)
    rows = []
    t0 = time.monotonic()
    for i, (expect, ev, wait_s) in enumerate(STEPS, 1):
        ts = dt.datetime.now().isoformat(timespec="seconds")
        if ev is not None:
            payload = {"version": 1, "event": ev["event"]}
            if "title" in ev:
                payload["title"] = ev["title"]
            ser.write((json.dumps(payload) + "\n").encode("utf-8"))
            ser.flush()
        print("STEP %2d  expect=%-8s  %s" % (i, expect,
                                             "send " + ev["event"] if ev else "silence"), flush=True)
        time.sleep(wait_s)
        rows.append({"step": i, "expect": expect, "sent": ev["event"] if ev else None,
                     "at": ts, "t_rel_s": round(time.monotonic() - t0, 1)})
    ser.close()
    print("DONE 20 steps. Operator verdict: ___/20", flush=True)
    if args.out:
        out = {"schema": "esp32-buddy-g1-drive/1", "date": dt.date.today().isoformat(),
               "port": args.port, "steps": rows,
               "operator_verdict": None, "note": "operator must record 20/20 or failing steps"}
        with open(args.out, "w", encoding="utf-8") as f:
            json.dump(out, f, ensure_ascii=False, indent=2)
        print("wrote", args.out, flush=True)


if __name__ == "__main__":
    main()
