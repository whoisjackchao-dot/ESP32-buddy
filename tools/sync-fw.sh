#!/usr/bin/env bash
# 同步仓库固件 → Windows 构建副本（默认 D:\buddy_flash\firmware），保留 build/ 与 sdkconfig。
# 背景：副本是独立目录，无自动同步；曾因此把"多卡之前"的旧固件刷进设备（B16）。
# 用法：tools/sync-fw.sh   （DST=/path/to/firmware 可覆盖）
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/firmware"
DST="${DST:-/mnt/d/buddy_flash/firmware}"

if [ ! -d "$DST" ]; then
    echo "ERROR: 目标不存在：$DST" >&2
    exit 1
fi

# 只同步源码与工程文件；build 产物、本地 sdkconfig、依赖锁保留原样
if command -v rsync >/dev/null 2>&1; then
    rsync -a --exclude build --exclude 'sdkconfig*' --exclude dependencies.lock "$SRC/" "$DST/"
else
    ( cd "$SRC" && find . -path ./build -prune -o -name 'sdkconfig*' -prune -o -name dependencies.lock -prune -o -type f -print ) |
    while IFS= read -r f; do
        mkdir -p "$DST/$(dirname "$f")"
        cp -a "$SRC/$f" "$DST/$f"
    done
fi

# 断言：本版特性已在副本里（防止再次刷旧固件）
grep -q "parse_tasks_array" "$DST/components/pet/pet_link.c" ||
    { echo "ERROR: pet_link.c 不是多卡版（副本仍旧）" >&2; exit 1; }

echo "synced: $SRC -> $DST"
