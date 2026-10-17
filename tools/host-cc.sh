#!/usr/bin/env bash
# 选出可用的 host C 编译器：系统 cc 优先，其次用户态 zig cc。
# 用法：CC=tools/host-cc.sh bash tests/run.sh
# （背景见 evidence/2026-10-16-新机器环境基线.md 与缺口 B15）
set -eu
if command -v cc >/dev/null 2>&1; then
    exec cc "$@"
fi
if command -v zig >/dev/null 2>&1; then
    exec zig cc "$@"
fi
for z in "$HOME/.local/share/zig/zig" /tmp/opencode/zb/zig-*/zig; do
    if [ -x "$z" ]; then
        exec "$z" cc "$@"
    fi
done
echo "ERROR: 找不到 C 编译器（cc / zig）。见 evidence/2026-10-16-新机器环境基线.md" >&2
exit 127
