"""T entry: Windows-side bridge, log file -> serial port (repo copy of
D:\\buddy_flash\\file_to_serial.py).

Tails an NDJSON event log produced by oc_poll.py and writes every new
byte to the serial port, so a poller crash/restart on the WSL side never
touches the device directly. Position tracking survives truncation and
rotation: if the file shrinks below the stored position, reading resets
to 0.

Console IP discovery: a daemon reader thread in this same process scans
the device's console output (USB CDC carries both directions) for the
wifi announcement line `net: wifi connected ip=<ip>` and atomically
writes it to --ip-file (tmp+rename), which oc_poll.py --tcp auto picks
up for the WiFi TCP path. All other console lines are ignored. The
serial object is used from two threads in opposite directions only
(reader reads, main loop writes), which pyserial supports.

stdlib-only; pyserial is imported lazily only on the real-serial path,
so importing this module and running the tests never requires it.
"""

import argparse
import os
import re
import sys
import threading
import time

# Nominal baud only: USB-Serial-JTAG CDC ignores it (M4 §9.4).
BAUD = 115200

# Console line announcing the station IP (firmware net bring-up log).
# 两种来源：连接瞬间的公告，以及 `wifi.status` 查询的应答（桥启动时主动发一次）
IP_LINE_RE = re.compile(
    r"(?:net: wifi connected ip=|net: status\b.*?\bip=)(\d+\.\d+\.\d+\.\d+)")


def tail(path, pos):
    """New bytes at/after pos as (data, new_pos).

    Missing file -> no bytes, position unchanged. Truncation or rotation
    (size < pos) -> reset position to 0 and read what is there now.
    """
    try:
        size = os.path.getsize(path)
    except OSError:
        return b"", pos
    if size < pos:
        pos = 0
    if size <= pos:
        return b"", pos
    with open(path, "rb") as f:
        f.seek(pos)
        data = f.read()
    return data, pos + len(data)


def _open_serial(port):
    import serial
    return serial.Serial(port, BAUD, timeout=1.0)


def extract_ip(raw_line):
    """Device IP from a `net: wifi connected ip=<ip>` console line, or None."""
    m = IP_LINE_RE.search(raw_line.decode("utf-8", "replace"))
    return m.group(1) if m else None


def write_ip_file(path, ip):
    """Atomically record the device IP (tmp + rename)."""
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(ip + "\n")
    os.replace(tmp, path)


def console_reader(ser, ip_file, stop_event):
    """Reader-thread body: scan console lines for the wifi IP announcement.

    Exits when stop_event is set or the port errors/closes. Writes the
    IP to ip_file (tmp+rename) on each announcement; every other line is
    ignored. Reads only - the main thread keeps sole write access.
    """
    while not stop_event.is_set():
        try:
            raw = ser.readline()
        except (OSError, ValueError):
            break
        if not raw:
            continue
        ip = extract_ip(raw)
        if ip:
            try:
                write_ip_file(ip_file, ip)
            except OSError:
                pass


def build_parser():
    parser = argparse.ArgumentParser(
        prog="file_to_serial",
        description="Tail an NDJSON event log and write new bytes to serial.",
    )
    parser.add_argument("--log", required=True,
                        help="NDJSON event log to tail")
    parser.add_argument("--port", default="COM5",
                        help="serial port (default: %(default)s)")
    parser.add_argument("--interval", type=float, default=0.5,
                        help="tail cadence seconds (default: %(default)s)")
    parser.add_argument("--stop-file", default="",
                        help="exit when this file appears (supervisor shutdown)")
    parser.add_argument("--ip-file", default=None,
                        help="where to record the device IP announced on the"
                             " console (default: <log>.ip)")
    return parser


def main(argv=None, serial_factory=None, sleep_fn=time.sleep,
         stop_after=None, clock=time.monotonic):
    args = build_parser().parse_args(argv)
    ip_file = args.ip_file if args.ip_file else args.log + ".ip"
    print("[file_to_serial] pid=%d log=%s port=%s interval=%ss ip-file=%s"
          % (os.getpid(), args.log, args.port, args.interval, ip_file),
          flush=True)
    # Skip history: only bytes arriving after startup go to the device.
    try:
        pos = os.path.getsize(args.log)
    except OSError:
        pos = 0
    ser = None
    reader = None
    stop_reader = threading.Event()
    if serial_factory is None:
        serial_factory = _open_serial
    start = clock()
    try:
        while stop_after is None or clock() - start < stop_after:
            if args.stop_file and os.path.exists(args.stop_file):
                break  # cooperative shutdown (Windows child cannot be killed via SIGTERM)
            data, pos = tail(args.log, pos)
            if data:
                if ser is None:
                    ser = serial_factory(args.port)
                    try:
                        # 设备已连网时不会重发 "wifi connected"，主动查一次 wifi.status 取 IP
                        ser.write(b'{"version":1,"cmd":"wifi.status"}\n')
                    except (OSError, ValueError):
                        pass
                    reader = threading.Thread(
                        target=console_reader,
                        args=(ser, ip_file, stop_reader), daemon=True)
                    reader.start()
                ser.write(data)
            sleep_fn(args.interval)
    except KeyboardInterrupt:
        pass  # clean SIGINT exit
    finally:
        stop_reader.set()
        if ser is not None:
            close = getattr(ser, "close", None)
            if close is not None:
                close()
        if reader is not None:
            reader.join(timeout=1.0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
