"""Unified host service: HTTP entry + OpenCode poller + serial read/write.

One process owns the device serial port and exposes the local event entry:

    script / curl --> HTTP :8787 --> queue --> serial --> device
    OpenCode DB   --> poller ------> queue --> serial --> device
    device control line -----------> focus OpenCode window

The queue keeps HTTP responses fast and lets the writer retry transient
serial errors. This module intentionally uses only the Python stdlib;
pyserial is imported lazily on the real-port path.
"""

import argparse
import os
import queue
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import opencode_source
import pet_devio
import pet_entry_server
import pet_ingest
import pet_transport


class QueueSink:
    """Bounded queue used as the HTTP sink; drops the oldest line when full."""

    def __init__(self, maxsize=256):
        self._q = queue.Queue(maxsize=maxsize)
        self.dropped = 0

    def __call__(self, line):
        try:
            self._q.put_nowait(line)
        except queue.Full:
            self.dropped += 1
            try:
                self._q.get_nowait()
            except queue.Empty:
                pass
            try:
                self._q.put_nowait(line)
            except queue.Full:
                pass

    def get(self, timeout=0.1):
        return self._q.get(timeout=timeout)

    def qsize(self):
        return self._q.qsize()


class PetService:
    """Own one serial port and all host-side event producers/consumers."""

    def __init__(self, serial_obj, source=None, focus=None, host="127.0.0.1",
                 port=None, token=None, poll_interval=1.0, heartbeat=5.0,
                 serial_factory=None, log=None):
        self.serial = serial_obj
        self.source = source
        self.focus = focus or (lambda: False)
        self.serial_factory = serial_factory
        self.poll_interval = poll_interval
        self.heartbeat = heartbeat
        self._log = log or (lambda msg: print(msg, file=sys.stderr))
        self._queue = QueueSink()
        self._stop = threading.Event()
        self._threads = []
        self._started = False
        self.server = pet_entry_server.make_server(
            host=host, port=port, sink=self._queue, token=token)

    # ---- lifecycle -----------------------------------------------------

    def start(self):
        if self._started:
            return
        self._started = True
        self._threads = [
            threading.Thread(target=self.server.serve_forever,
                             name="pet-http", daemon=True),
            threading.Thread(target=self._writer_loop,
                             name="pet-writer", daemon=True),
            threading.Thread(target=self._reader_loop,
                             name="pet-reader", daemon=True),
            threading.Thread(target=self._poll_loop,
                             name="pet-poller", daemon=True),
        ]
        for thread in self._threads:
            thread.start()

    def stop(self):
        if not self._started:
            return
        self._stop.set()
        try:
            self.server.shutdown()
        except Exception:
            pass
        try:
            self.server.server_close()
        except Exception:
            pass
        self._close_serial()
        for thread in self._threads:
            if thread is not threading.current_thread():
                thread.join(timeout=2.0)
        self._started = False

    def wait(self):
        self._stop.wait()

    def run(self):
        self.start()
        try:
            self.wait()
        finally:
            self.stop()

    # ---- serial --------------------------------------------------------

    def _close_serial(self):
        close = getattr(self.serial, "close", None)
        if close is not None:
            try:
                close()
            except Exception:
                pass

    def _reopen_serial(self):
        if self.serial_factory is None:
            return False
        try:
            self._close_serial()
            self.serial = self.serial_factory()
            self._log("[pet_service] serial reopened")
            return True
        except Exception as exc:
            self._log("[pet_service] serial reopen failed: %s" % exc)
            return False

    def _writer_loop(self):
        while not self._stop.is_set():
            try:
                item = self._queue.get(timeout=0.1)
            except queue.Empty:
                continue
            line = pet_transport.ensure_line(item)
            while not self._stop.is_set():
                try:
                    self.serial.write(line)
                    break
                except Exception as exc:
                    self._log("[pet_service] serial write failed: %s" % exc)
                    self._reopen_serial()
                    if self._stop.wait(1.0):
                        return

    def _reader_loop(self):
        while not self._stop.is_set():
            try:
                raw = self.serial.readline()
            except Exception as exc:
                self._log("[pet_service] serial read failed: %s" % exc)
                self._reopen_serial()
                if self._stop.wait(1.0):
                    return
                continue
            if not raw:
                self._stop.wait(0.01)
                continue
            text = raw.decode("utf-8", errors="replace") if isinstance(raw, bytes) else raw
            cmd = pet_devio.parse_device_line(text)
            if cmd and cmd.get("cmd") == "back_window":
                try:
                    ok = bool(self.focus())
                except Exception as exc:
                    ok = False
                    self._log("[pet_service] focus failed: %s" % exc)
                self._log("[pet_service] back_window -> focus %s"
                          % ("ok" if ok else "FAILED"))

    # ---- event sources -------------------------------------------------

    def _poll_loop(self):
        last_beat = None
        while not self._stop.is_set():
            event = None
            if self.source is not None:
                try:
                    event = self.source.poll()
                except Exception as exc:
                    self._log("[pet_service] opencode poll failed: %s" % exc)
            try:
                if event is not None:
                    self._queue(event)
                elif opencode_source.heartbeat_due(
                        last_beat, time.monotonic(), self.heartbeat):
                    self._queue({"version": 1, "event": "device.heartbeat"})
                    last_beat = time.monotonic()
            except Exception as exc:
                self._log("[pet_service] queue failed: %s" % exc)
            self._stop.wait(self.poll_interval)


def build_parser():
    parser = argparse.ArgumentParser(
        prog="pet_service",
        description="ESP32-buddy unified host service (HTTP + OpenCode + serial).")
    parser.add_argument("--port", default=os.environ.get("PET_SERIAL_PORT", "COM5"),
                        help="serial port (default: COM5)")
    parser.add_argument("--baud", type=int, default=pet_transport.PET_RX_BAUD)
    parser.add_argument("--db", default=opencode_source.DEFAULT_DB,
                        help="OpenCode SQLite path")
    parser.add_argument("--no-opencode", action="store_true",
                        help="do not poll the OpenCode DB")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--http-port", type=int,
                        default=int(os.environ.get("PET_EVENT_PORT",
                                                   pet_ingest.EVENT_INGEST_PORT)))
    parser.add_argument("--token", default=os.environ.get("PET_EVENT_TOKEN"),
                        help="optional X-Pet-Token value")
    parser.add_argument("--interval", type=float, default=1.0,
                        help="OpenCode poll cadence seconds")
    parser.add_argument("--heartbeat", type=float, default=5.0,
                        help="device heartbeat cadence seconds")
    return parser


def main(argv=None):
    args = build_parser().parse_args(argv)

    import serial  # lazy: --help / tests do not need pyserial

    def serial_factory():
        return serial.Serial(args.port, args.baud, timeout=0.5)

    source = None if args.no_opencode else opencode_source.OpenCodeSource(args.db)
    try:
        import win_focus
        focus = win_focus.focus_opencode_window
    except Exception:
        focus = lambda: False

    service = PetService(
        serial_factory(), source=source, focus=focus, host=args.host,
        port=args.http_port, token=args.token, poll_interval=args.interval,
        heartbeat=args.heartbeat, serial_factory=serial_factory)
    print("[pet_service] serial=%s http=%s:%d opencode=%s"
          % (args.port, args.host, args.http_port,
             "off" if source is None else args.db), file=sys.stderr)
    try:
        service.run()
    except KeyboardInterrupt:
        service.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
