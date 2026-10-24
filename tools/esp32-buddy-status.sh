#!/usr/bin/env bash
# ESP32-buddy entry supervisor: status view (T entry).
#
# Prints pid liveness, the supervisor's child-state JSON (if present) and
# the last log lines. Exit 0 = running, 1 = not running / stale pidfile.
set -eu
cd "$(dirname "$0")/.."

PIDFILE="${BUDDY_PIDFILE:-/tmp/esp32-buddy.pid}"
LOG="${BUDDY_LOG:-/tmp/esp32-buddy.log}"
STATE="${BUDDY_STATE:-/tmp/esp32-buddy.state}"

RC=0
if [ -f "$PIDFILE" ]; then
  PID="$(cat "$PIDFILE")"
  if kill -0 "$PID" 2>/dev/null; then
    echo "running (pid $PID)"
  else
    echo "stale (pid $PID not running; clear with tools/esp32-buddy-down.sh)"
    RC=1
  fi
else
  echo "not running (no $PIDFILE)"
  RC=1
fi

if [ "$RC" -eq 0 ] && [ -f "$STATE" ]; then
  echo "--- child states ($STATE)"
  cat "$STATE"
  echo
fi
if [ -f "$LOG" ]; then
  echo "--- last log lines ($LOG)"
  tail -n "${BUDDY_STATUS_LINES:-5}" "$LOG"
fi
exit "$RC"
