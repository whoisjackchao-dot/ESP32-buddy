#!/usr/bin/env python3
"""M9 门槛预检（host 半段，无需设备）。

覆盖：
  G6-host  事件入口 20/20 接受，且 sink 收到 20/20 正确事件线；单次延迟上界
  FAULT    畸形请求被拒、sink 不被污染（不崩溃、不写脏行）

注意：G6 的**完整判定**要求"设备 1s 内显示"，必须有设备；本脚本只证明
"主机侧 20/20 送达且格式正确"，结果记为 G6-host，不替代 G6。

用法：
    python3 tools/gate-host-smoke.py           # 人读输出
    python3 tools/gate-host-smoke.py --json    # 机器可读（供 manifest/CI）
退出码：0 全通过；1 有失败。
"""
import argparse
import json
import os
import sys
import threading
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "host"))

import pet_entry_server  # noqa: E402
import pet_ingest  # noqa: E402

N = 20


def _post(url, payload, content_type="application/json"):
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Content-Type": content_type})
    t0 = time.perf_counter()
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            body = resp.read()
            return resp.status, body, (time.perf_counter() - t0) * 1000.0
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read(), (time.perf_counter() - t0) * 1000.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args()

    lines = []
    srv = pet_entry_server.make_server(port=0, sink=lines.append)
    thread = threading.Thread(target=srv.serve_forever, daemon=True)
    thread.start()
    host, port = srv.server_address[:2]
    url = f"http://{host}:{port}{pet_ingest.ENTRY_PATH}"

    results = []
    latencies = []

    # --- G6-host：20 个合法事件 ---
    events = [
        ("task.start", "写登录页", "web"),
        ("task.done", "修 timer 泄漏", "firmware"),
        ("task.error", "构建失败", "ci"),
        ("agent.input_required", "等你确认", "opencode"),
        ("agent.idle", None, "opencode"),
        ("device.heartbeat", None, "pet"),
    ]
    ok = 0
    for i in range(N):
        ev, title, project = events[i % len(events)]
        payload = {"version": 1, "event": ev, "project": project}
        if title:
            payload["title"] = title
        status, body, ms = _post(url, payload)
        latencies.append(ms)
        if status == 200 and b"OK" in body:
            ok += 1
    got = len(lines)
    results.append({
        "id": "G6-host",
        "pass": ok == N and got == N,
        "detail": f"{ok}/{N} HTTP 200，sink {got}/{N} 行",
    })

    # --- FAULT：畸形请求不得污染 sink ---
    before = len(lines)
    faults = [
        ("非 JSON", b"not json", "application/json", 400),
        ("顶层数组", b"[1,2,3]", "application/json", 400),
        ("错误 version", b'{"version":2,"event":"agent.idle"}', "application/json", 400),
        ("未知 event", b'{"version":1,"event":"nope"}', "application/json", 400),
        ("错误 Content-Type", b'{"version":1,"event":"agent.idle"}', "text/plain", 415),
        ("空 body", b"", "application/json", 400),
        ("超长 body", b'{"version":1,"event":"agent.idle","x":"' + b"a" * 5000 + b'"}',
         "application/json", 413),
    ]
    fault_ok = 0
    for label, data, ctype, want in faults:
        req = urllib.request.Request(url, data=data, method="POST",
                                     headers={"Content-Type": ctype})
        try:
            with urllib.request.urlopen(req, timeout=5) as resp:
                status = resp.status
        except urllib.error.HTTPError as exc:
            status = exc.code
        if status == want:
            fault_ok += 1
    after = len(lines)
    results.append({
        "id": "FAULT",
        "pass": fault_ok == len(faults) and after == before,
        "detail": f"{fault_ok}/{len(faults)} 按预期拒绝；sink {before}→{after}（应不变）",
    })

    srv.shutdown()
    srv.server_close()

    all_pass = all(r["pass"] for r in results)
    if args.json:
        print(json.dumps({
            "pass": all_pass,
            "results": results,
            "latency_ms": {
                "min": round(min(latencies), 2),
                "max": round(max(latencies), 2),
                "avg": round(sum(latencies) / len(latencies), 2),
            },
        }, ensure_ascii=False, indent=2))
    else:
        for r in results:
            print(f"[{'PASS' if r['pass'] else 'FAIL'}] {r['id']}: {r['detail']}")
        print(f"      延迟 ms: min {min(latencies):.2f} / avg "
              f"{sum(latencies)/len(latencies):.2f} / max {max(latencies):.2f}")
        print("注意：G6 完整判定还需设备侧 1s 内显示；本脚本只证明 host 半段。")
    return 0 if all_pass else 1


if __name__ == "__main__":
    raise SystemExit(main())
