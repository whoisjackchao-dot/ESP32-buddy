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

WiFi control channel: the device mirrors its control lines (voice.text /
send) over WiFi TCP to the host, so they arrive even with USB unplugged.
This process discovers the Windows LAN IPv4 at startup (UDP-connect
trick, ipconfig fallback) and records it in <log>.hostaddr for
oc_poll.py to announce to the device, and listens on
0.0.0.0:<--control-port> (default 8767). Listener lines run through the
same handlers as the console reader; a shared 500 ms dedup drops the
duplicate copy when both transports deliver the same line.

The serial object is used from two threads in opposite directions only
(reader reads, main loop writes), which pyserial supports.

stdlib-only; pyserial is imported lazily only on the real-serial path,
so importing this module and running the tests never requires it.
"""

import argparse
import json
import os
import re
import socket
import sys
import threading
import time
import urllib.request

# Nominal baud only: USB-Serial-JTAG CDC ignores it (M4 §9.4).
BAUD = 115200

# Control listener port: the device connects here once per control line
# (contract: evidence/2026-10-25-WiFi控制通道-T.md).
CONTROL_PORT_DEFAULT = 8767

# USB+WiFi double delivery: identical control line inside this window is
# applied once (both transports carry the same line within milliseconds).
DEDUP_WINDOW_S = 0.5

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


IPCONFIG_IPV4_RE = re.compile(r"IPv4[^\r\n:]*:\s*(\d+(?:\.\d+){3})")


def _parse_ipconfig(text):
    """First IPv4 address from `ipconfig` output, or None.

    Locale-tolerant: anchored on "IPv4" only, so English
    ("IPv4 Address") and Chinese ("IPv4 地址") output both match.
    """
    m = IPCONFIG_IPV4_RE.search(text or "")
    return m.group(1) if m else None


def _udp_local_ip():
    """Local IPv4 the OS would route to the internet (UDP connect trick:
    no packet is sent). None on failure or an unspecified address."""
    s = None
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        return ip if ip and ip != "0.0.0.0" else None
    except OSError:
        return None
    finally:
        if s is not None:
            s.close()


def _run_ipconfig():
    """Raw `ipconfig` output (Windows only; caller checks the platform)."""
    import subprocess
    r = subprocess.run(["ipconfig"], capture_output=True, timeout=3,
                       check=False)
    return (r.stdout or b"").decode("utf-8", "replace")


_IP_UNSET = object()  # sentinel: "udp_ip argument not provided"


_warn_state = {}


def warn_rate_limited(key, msg, period=5.0, clock=time.monotonic):
    """Rate-limited stderr warning (missing USB must not spam)."""
    now = clock()
    if now - _warn_state.get(key, -1e9) >= period:
        _warn_state[key] = now
        print("[file_to_serial] %s" % msg, file=sys.stderr, flush=True)


def get_lan_ip(udp_ip=_IP_UNSET, ipconfig_fn=None):
    """Windows LAN IPv4 (best-effort), or None.

    Default stages: UDP-connect trick first, then a Windows-only
    `ipconfig` parse. udp_ip / ipconfig_fn inject each stage for tests;
    udp_ip=None explicitly marks the UDP stage failed, ipconfig_fn=None
    marks it unavailable. Non-Windows without injection returns None so
    the suite stays hermetic.
    """
    if udp_ip is _IP_UNSET:
        udp_ip = _udp_local_ip()
    if udp_ip:
        return udp_ip
    if ipconfig_fn is None:
        if os.name != "nt":
            return None
        ipconfig_fn = _run_ipconfig
    try:
        return _parse_ipconfig(ipconfig_fn())
    except Exception:
        return None


def write_hostaddr_file(path, ip):
    """Atomically record the host LAN IP for oc_poll.py (tmp + rename)."""
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


def extract_send_cmd(raw_line):
    """True for a `{"cmd":"send"}` console line (KEY1 short = submit)."""
    try:
        obj = json.loads(raw_line.decode("utf-8", "replace"))
    except ValueError:
        return False
    return isinstance(obj, dict) and obj.get("cmd") == "send"


def _focus_opencode():
    try:
        import win_focus
        return bool(win_focus.focus_opencode_window())
    except Exception:
        return False


def focus_and_ctrl_v():
    """Focus the OpenCode window and paste the clipboard into its input box."""
    _focus_opencode()
    try:
        import win_keys
        return bool(win_keys.send_ctrl_v())
    except Exception:
        return False


def focus_and_enter():
    """Focus the OpenCode window and submit (Enter)."""
    _focus_opencode()
    try:
        import win_keys
        return bool(win_keys.send_enter())
    except Exception:
        return False


def voice_clipboard_sink(text, auto_paste=True):
    """Copy the transcript, then (default) focus + Ctrl+V into the prompt."""
    ok = copy_to_clipboard(text)
    if ok and auto_paste:
        focus_and_ctrl_v()
    return ok


class LineDedup:
    """Drop identical control lines inside DEDUP_WINDOW_S.

    The device sends every control line on both transports (USB and
    WiFi); the host applies it once. allow(line) takes the stripped
    line bytes and returns True the first time within the window, False
    for repeats; seen-lines memory is pruned on each call.
    """

    def __init__(self, window=DEDUP_WINDOW_S, clock=time.monotonic):
        self._window = window
        self._clock = clock
        self._seen = {}

    def allow(self, line):
        now = self._clock()
        for k in [k for k, t in self._seen.items()
                  if now - t >= self._window]:
            del self._seen[k]
        if line in self._seen:
            return False
        self._seen[line] = now
        return True


def make_control_handlers(voice_url=VOICE_DEFAULT_URL, voice_enabled=True,
                          auto_paste=True, voice_post=None, send_action=None):
    """Per-line handler shared by the USB console reader and the WiFi
    control listener: voice.text -> clipboard(+paste), send -> Enter.
    Same kwargs semantics as console_reader; raw is stripped line bytes.
    """
    if voice_post is None:
        voice_post = (post_voice_text if voice_url
                      else (lambda url, text, timeout:
                            voice_clipboard_sink(text, auto_paste)))

    def handle_line(raw):
        if voice_enabled:
            text = extract_voice_text(raw)
            if text:
                try:
                    voice_post(voice_url, text, VOICE_POST_TIMEOUT)
                except Exception:
                    pass
        if extract_send_cmd(raw):
            try:
                (send_action or focus_and_enter)()
            except Exception:
                pass

    return handle_line


def open_control_listener(port):
    """Listening TCP socket on 0.0.0.0:port; the device connects here
    once per control line (same pattern as the :8766 event channel)."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", port))
    sock.listen(4)
    return sock


def _serve_control_conn(conn, handle_line, line_filter, conn_timeout):
    """Split one connection's bytes into lines and dispatch each."""
    conn.settimeout(conn_timeout)
    buf = b""
    while True:
        try:
            chunk = conn.recv(4096)
        except (socket.timeout, OSError):
            return
        if not chunk:
            return
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            raw = raw.strip()
            if not raw:
                continue
            if line_filter is not None and not line_filter(raw):
                continue
            handle_line(raw)


def control_listener(lsock, stop_event, handle_line, line_filter=None,
                     poll_s=0.1, conn_timeout=5.0):
    """Listener-thread body: accept device control connections until
    stop_event is set (the device opens one connection per line and
    closes). Each complete line is dispatched to handle_line unless
    line_filter (the shared dedup) rejects it. Socket errors only drop
    the offending connection or end the loop - never raise.
    """
    lsock.settimeout(poll_s)
    while not stop_event.is_set():
        try:
            conn, _ = lsock.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        try:
            _serve_control_conn(conn, handle_line, line_filter, conn_timeout)
        except Exception:
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass


def console_reader(ser, ip_file, stop_event,
                   voice_url=VOICE_DEFAULT_URL, voice_post=None,
                   voice_enabled=True, auto_paste=True, send_action=None,
                   line_filter=None):
    """Reader-thread body: scan console lines for the wifi IP announcement
    and the device's voice.text transcript.

    Exits when stop_event is set or the port errors/closes. Writes the
    IP to ip_file (tmp+rename) on each announcement. On a voice.text
    line, POSTs the text to the local OpenCode draft plugin (best-effort,
    never raises). Reads only - the main thread keeps sole write access.

    line_filter (the shared LineDedup) drops control lines already
    delivered over WiFi; IP discovery runs before the filter and is
    unaffected. None keeps the pre-dedup behavior.
    """
    handlers = make_control_handlers(
        voice_url=voice_url, voice_enabled=voice_enabled,
        auto_paste=auto_paste, voice_post=voice_post,
        send_action=send_action)
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
        if line_filter is not None and not line_filter(raw.strip()):
            continue  # same line already handled via the WiFi listener
        handlers(raw)


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
    parser.add_argument("--no-auto-paste", action="store_true",
                        help="copy the transcript but do not focus+Ctrl+V it")
    parser.add_argument("--control-port", type=int,
                        default=CONTROL_PORT_DEFAULT,
                        help="TCP control listener port (device -> host"
                             " control lines over WiFi); 0 disables"
                             " (default: %(default)s)")
    return parser


def main(argv=None, serial_factory=None, sleep_fn=time.sleep,
         stop_after=None, clock=time.monotonic, voice_post=None,
         send_action=None, ip_detect=None):
    args = build_parser().parse_args(argv)
    ip_file = args.ip_file if args.ip_file else args.log + ".ip"
    hostaddr_file = args.log + ".hostaddr"
    lan_ip = (ip_detect or get_lan_ip)()
    if lan_ip:
        try:
            write_hostaddr_file(hostaddr_file, lan_ip)
        except OSError:
            pass
    print("[file_to_serial] pid=%d log=%s port=%s interval=%ss ip-file=%s"
          " voice=%s hostaddr=%s ctl-port=%d"
          % (os.getpid(), args.log, args.port, args.interval, ip_file,
             "off" if args.no_voice else (args.voice_url or "clipboard"),
             lan_ip or "-", args.control_port),
          flush=True)
    dedup = LineDedup(clock=clock)
    stop_reader = threading.Event()
    ctl_sock = None
    ctl_thread = None
    if args.control_port > 0:
        try:
            ctl_sock = open_control_listener(args.control_port)
            ctl_thread = threading.Thread(
                target=control_listener,
                args=(ctl_sock, stop_reader,
                      make_control_handlers(
                          voice_url=args.voice_url,
                          voice_enabled=not args.no_voice,
                          auto_paste=not args.no_auto_paste,
                          voice_post=voice_post,
                          send_action=send_action)),
                kwargs={"line_filter": dedup.allow},
                name="ctl-listener", daemon=True)
            ctl_thread.start()
        except OSError as exc:
            print("[file_to_serial] control listener disabled: %s" % exc,
                  flush=True)
            if ctl_sock is not None:
                ctl_sock.close()
                ctl_sock = None
                ctl_thread = None
    # Skip history: only bytes arriving after startup go to the device.
    try:
        pos = os.path.getsize(args.log)
    except OSError:
        pos = 0
    ser = None
    reader = None
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
                    try:
                        ser = serial_factory(args.port)
                    except (OSError, ValueError) as exc:
                        # USB 不在：保持 WiFi 控制监听运行，稍后重试
                        warn_rate_limited("usb", "serial open failed: %s" % exc)
                        sleep_fn(args.interval)
                        continue
                    try:
                        # 设备已连网时不会重发 "wifi connected"，主动查一次 wifi.status 取 IP
                        ser.write(b'{"version":1,"cmd":"wifi.status"}\n')
                    except (OSError, ValueError):
                        pass
                    if reader is not None:
                        reader.join(timeout=1.0)  # 旧串口线程自行退出
                    reader = threading.Thread(
                        target=console_reader,
                        args=(ser, ip_file, stop_reader),
                        kwargs={"voice_url": args.voice_url,
                                "voice_enabled": not args.no_voice,
                                "voice_post": voice_post,
                                "auto_paste": not args.no_auto_paste,
                                "send_action": send_action,
                                "line_filter": dedup.allow},
                        daemon=True)
                    reader.start()
                try:
                    ser.write(data)
                except (OSError, ValueError) as exc:
                    warn_rate_limited("usb", "serial write failed: %s" % exc)
                    ser = None
            sleep_fn(args.interval)
    except KeyboardInterrupt:
        pass  # clean SIGINT exit
    finally:
        stop_reader.set()
        if ctl_sock is not None:
            try:
                ctl_sock.close()
            except OSError:
                pass
        if ctl_thread is not None:
            ctl_thread.join(timeout=1.0)
        if ser is not None:
            close = getattr(ser, "close", None)
            if close is not None:
                close()
        if reader is not None:
            reader.join(timeout=1.0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
