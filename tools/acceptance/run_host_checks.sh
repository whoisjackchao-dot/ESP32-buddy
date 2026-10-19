#!/usr/bin/env bash
# M9 host-side acceptance checks. These are T/C checks only; they do NOT
# replace any G1-G7 device (H) evidence.
set -eu

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

STAMP="$(date -Iseconds)"
COMMIT="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
OUT_DIR="evidence/M9"
OUT="$OUT_DIR/$(date +%F)-host-acceptance.md"
mkdir -p "$OUT_DIR"

status=0

run() {
    local name="$1"
    shift
    echo "== $name ==" | tee -a "$OUT"
    if "$@" >> "$OUT" 2>&1; then
        echo "PASS: $name" | tee -a "$OUT"
    else
        echo "FAIL: $name" | tee -a "$OUT"
        status=1
    fi
}

{
    echo "# M9 host 验收记录（自动生成）"
    echo
    echo "> 日期：$STAMP；commit：\`$COMMIT\`；类别：**T/C（host）**"
    echo "> 本记录**不包含** G1–G7 的真机（H）结论；H 必须另按 runbook 执行。"
    echo
    echo "## 环境"
    echo
    echo '```'
    uname -a || true
    "${CC:-cc}" --version 2>/dev/null | head -1 || true
    python3 --version 2>/dev/null || true
    echo '```'
    echo
    echo "## 结果"
    echo
} > "$OUT"

run "host C tests" bash tests/run.sh
run "host Python tests" ./host/run-tests.sh
run "simulator scenes" bash tools/sim.sh

{
    echo
    echo "## 结论"
    echo
    if [ "$status" -eq 0 ]; then
        echo "**host T/C：PASS**。G1–G7 的 H 类仍为 \`NOT_RUN\`，需真机执行。"
    else
        echo "**host T/C：FAIL**。修复后再重跑；不得用本记录替代 H。"
    fi
} >> "$OUT"

echo
echo "wrote $OUT"
exit "$status"
