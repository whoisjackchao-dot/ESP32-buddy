#!/usr/bin/env bash
# ESP32-buddy entry supervisor: idempotent start (T entry).
#
# Env overrides (tests use these; defaults match /tmp):
#   BUDDY_PIDFILE  (default /tmp/esp32-buddy.pid)
#   BUDDY_LOG      (default /tmp/esp32-buddy.log)
#   BUDDY_STATE    (default /tmp/esp32-buddy.state)
#   BUDDY_SUPERVISOR  full command line override (default:
#                  python3 host/pet_supervisor.py --log $LOG --state $STATE)
# Paths must not contain spaces (unquoted word splitting by design).
set -eu
cd "$(dirname "$0")/.."

PIDFILE="${BUDDY_PIDFILE:-/tmp/esp32-buddy.pid}"
LOG="${BUDDY_LOG:-/tmp/esp32-buddy.log}"
STATE="${BUDDY_STATE:-/tmp/esp32-buddy.state}"

if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
  echo "already running (pid $(cat "$PIDFILE"))"
  exit 0
fi

# Windows 桥需要 Windows 可读的脚本路径：优先同步到 D: 副本
HOSTDIR="${BUDDY_HOST_DIR:-}"
if [ -z "$HOSTDIR" ]; then
  if [ -d /mnt/d/buddy_flash ]; then
    mkdir -p /mnt/d/buddy_flash/host
    cp -a host/*.py /mnt/d/buddy_flash/host/ 2>/dev/null || true
    HOSTDIR=/mnt/d/buddy_flash/host
  else
    HOSTDIR="$PWD/host"
  fi
fi

if [ -n "${BUDDY_SUPERVISOR:-}" ]; then
  CMD="$BUDDY_SUPERVISOR"
else
  CMD="python3 host/pet_supervisor.py --log $LOG --state $STATE --host-dir $HOSTDIR"
fi
nohup $CMD >/dev/null 2>&1 &
echo $! > "$PIDFILE"
echo "started (pid $(cat "$PIDFILE")) log=$LOG"
exec bash tools/esp32-buddy-status.sh
