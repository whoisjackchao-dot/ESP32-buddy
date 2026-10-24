#!/usr/bin/env bash
# ESP32-buddy entry supervisor: idempotent stop (T entry).
#
# Sends TERM to the pidfile pid (the supervisor, which terminates its own
# children), waits for exit, removes the pidfile. Never signals anything
# unless the pidfile exists and the pid is alive.
set -eu
cd "$(dirname "$0")/.."

PIDFILE="${BUDDY_PIDFILE:-/tmp/esp32-buddy.pid}"
STATE="${BUDDY_STATE:-/tmp/esp32-buddy.state}"

if [ ! -f "$PIDFILE" ]; then
  echo "not running"
  exit 0
fi

PID="$(cat "$PIDFILE")"
if kill -0 "$PID" 2>/dev/null; then
  kill -TERM "$PID" 2>/dev/null || true
  i=0
  while kill -0 "$PID" 2>/dev/null && [ "$i" -lt 50 ]; do
    sleep 0.1
    i=$((i+1))
  done
  if kill -0 "$PID" 2>/dev/null; then
    echo "warn: pid $PID did not exit after TERM" >&2
  else
    echo "stopped (pid $PID)"
  fi
else
  echo "stale pidfile removed (pid $PID)"
fi
rm -f "$PIDFILE" "$STATE"
exit 0
