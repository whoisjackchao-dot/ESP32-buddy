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
up for the WiFi TCP path.

Voice-to-draft: the same reader also detects the device's voice
transcript line `{"v":1,"cmd":"voice.text","text":"…"}` and best-effort
POSTs {"text": ...} to the local OpenCode draft-append plugin (default
http://127.0.0.1:8790/v1/draft/append, see .opencode/plugin/voice-draft.ts).
Failures are swallowed: the serial bridge never depends on the plugin
being up. --no-voice disables this path. Appending only ever fills the
prompt draft; submission stays with the user.

The serial object is used from two threads in opposite directions only
(reader reads, main loop writes), which pyserial supports.

stdlib-only; pyserial is imported lazily only on the real-serial path,
so importing this module and running the tests never requires it.
"""

import argparse
import json
import os
import re
import sys
import threading
import time
import urllib.request

# Nominal baud only: USB-Serial-JTAG CDC ignores it (M4 §9.4).
BAUD = 115200

# OpenCode draft-append plugin endpoint (esp32-s3-m5 adapted contract).
VOICE_DEFAULT_URL = ""  # 空 = 写入 Windows 剪贴板（OpenCode 1.19 槽语义不兼容后的方案）
VOICE_POST_TIMEOUT = 1.0

# Console line announcing the station IP (firmware net bring-up log).
# 两种来源：连接瞬间的公告，以及 `wifi.status` 查询的应答（桥启动时主动发一次）
IP_LINE_RE = re.compile(
    r"(?:net: wifi connected ip=|net: status\b.*?\bip=)(\d+\.\d+\.\d+\.\d+)")

# Loopback-only target: never route through an environment proxy.
_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


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


def extract_voice_text(raw_line):
    """Transcript text from a `{"cmd":"voice.text","text":…}` console line.

    Tolerates any invalid line: non-JSON, JSON that is not an object,
    other cmd values, and missing/empty/non-string text all yield None.
    """
    try:
        obj = json.loads(raw_line.decode("utf-8", "replace"))
    except ValueError:
        return None
    if not isinstance(obj, dict) or obj.get("cmd") != "voice.text":
        return None
    text = obj.get("text")
    if not isinstance(text, str) or not text:
        return None
    return text


def _post_json(url, text, timeout):
    req = urllib.request.Request(
        url, method="POST",
        data=json.dumps({"text": text}, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json; charset=utf-8"})
    with _OPENER.open(req, timeout=timeout) as resp:
        return 200 <= resp.status < 300


def post_voice_text(url, text, timeout=VOICE_POST_TIMEOUT, post_fn=None):
    """Best-effort POST {"text": text} to the plugin; never raises.

    Returns True only when the endpoint answered 2xx. A dead plugin is
    an expected state (OpenCode not running): swallow and keep bridging.
    """
    if post_fn is None:
        post_fn = _post_json
    try:
        return bool(post_fn(url, text, timeout))
    except Exception:
        return False


def copy_to_clipboard(text):
    """Best-effort: put text on the Windows clipboard (PowerShell Set-Clipboard).

    WSL/Non-Windows returns False without raising. Used as the default voice
    sink: the user presses Ctrl+V in the OpenCode prompt (no submit).
    """
    import subprocess
    import tempfile
    try:
        fd, path = tempfile.mkstemp(prefix="buddy_voice_", suffix=".txt")
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(text)
        try:
            subprocess.run(
                ["powershell.exe", "-NoProfile", "-Command",
                 "Set-Clipboard -Value (Get-Content -Raw -Encoding UTF8 '%s')" % path],
                timeout=VOICE_POST_TIMEOUT, check=False)
        finally:
            try:
                os.unlink(path)
            except OSError:
                pass
        return True
    except Exception:
        return False


def console_reader(ser, ip_file, stop_event,
                   voice_url=VOICE_DEFAULT_URL, voice_post=None,
                   voice_enabled=True):
    """Reader-thread body: scan console lines for the wifi IP announcement
    and the device's voice.text transcript.

    Exits when stop_event is set or the port errors/closes. Writes the
    IP to ip_file (tmp+rename) on each announcement. On a voice.text
    line, POSTs the text to the local OpenCode draft plugin (best-effort,
    never raises). Reads only - the main thread keeps sole write access.
    """
    if voice_post is None:
        voice_post = (post_voice_text if voice_url
                      else (lambda url, text, timeout: copy_to_clipboard(text)))
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
        if not voice_enabled:
            continue
        text = extract_voice_text(raw)
        if text:
            try:
                voice_post(voice_url, text, VOICE_POST_TIMEOUT)
            except Exception:
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
    parser.add_argument("--voice-url", default=VOICE_DEFAULT_URL,
                        help="OpenCode draft-append endpoint for device"
                             " voice.text lines (default: %(default)s)")
    parser.add_argument("--no-voice", action="store_true",
                        help="do not forward voice.text lines to the plugin")
    return parser


def main(argv=None, serial_factory=None, sleep_fn=time.sleep,
         stop_after=None, clock=time.monotonic, voice_post=None):
    args = build_parser().parse_args(argv)
    ip_file = args.ip_file if args.ip_file else args.log + ".ip"
    print("[file_to_serial] pid=%d log=%s port=%s interval=%ss ip-file=%s"
          " voice=%s" % (os.getpid(), args.log, args.port, args.interval,
                         ip_file, "off" if args.no_voice else (args.voice_url or "clipboard")),
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
                        args=(ser, ip_file, stop_reader),
                        kwargs={"voice_url": args.voice_url,
                                "voice_enabled": not args.no_voice,
                                "voice_post": voice_post},
                        daemon=True)
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
