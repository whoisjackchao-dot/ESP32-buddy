"""M9：单进程桥（HTTP 入口 → 串口），收敛原来的两进程管道。

原用法需要 `pet_entry_server | pet_sender`：管道一端崩了另一端不知道，
且串口断开后没有恢复路径。本模块把两者合到一个进程，并给 sink 加上
"写失败 → 重连一次 → 仍失败则丢弃并记账"的降级策略。

用法：
    python3 host/pet_bridge.py --serial COM5 [--port 8765]

设计边界（M9 明确不做）：
- 不做守护化（进程崩溃自动重启）——交给外部 supervisor；
- 不做断线缓存重放（丢就丢，但必须记账，不静默）。
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import pet_entry_server  # noqa: E402
import pet_transport  # noqa: E402


class ReconnectingSink:
    """可调用 sink：把一行事件写进串口；失败重连一次，仍失败则丢弃并记账。

    open_serial(port, baud) 可注入（测试用伪造串口）；默认惰性导入 pyserial。
    """

    def __init__(self, port, baud=pet_transport.PET_RX_BAUD,
                 open_serial=None, log=None):
        self.port = port
        self.baud = baud
        self._open = open_serial or self._open_real
        self._serial = None
        self._log = log or (lambda msg: print(msg, file=sys.stderr, flush=True))
        self.sent = 0
        self.dropped = 0
        self.reconnects = 0

    @staticmethod
    def _open_real(port, baud):
        import serial  # 惰性：只有真串口路径需要 pyserial
        return serial.Serial(port, baud)

    def _close(self):
        if self._serial is not None:
            try:
                self._serial.close()
            except Exception:  # noqa: BLE001 - 关闭失败不应影响后续
                pass
            self._serial = None

    def __call__(self, line):
        data = pet_transport.ensure_line(line)
        for attempt in (1, 2):
            try:
                if self._serial is None:
                    self._serial = self._open(self.port, self.baud)
                self._serial.write(data)
                self.sent += 1
                return True
            except Exception as exc:  # noqa: BLE001 - 串口异常面很宽（OSError/serial.SerialException）
                self._close()
                if attempt == 1:
                    self.reconnects += 1
                    self._log(f"bridge: 串口写入失败，重连一次：{exc}")
        self.dropped += 1
        self._log("bridge: 丢弃一行（串口不可用）")
        return False

    def close(self):
        self._close()


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="pet_bridge",
        description="单进程桥：HTTP 事件入口 → 串口（设备）",
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=None,
                        help="HTTP 端口；默认 PET_EVENT_PORT 或 8765 附近的值")
    parser.add_argument("--serial", default="COM5", help="串口（默认 COM5）")
    parser.add_argument("--baud", type=int, default=pet_transport.PET_RX_BAUD)
    args = parser.parse_args(argv)

    sink = ReconnectingSink(args.serial, args.baud)
    server = pet_entry_server.make_server(args.host, args.port, sink=sink)
    addr, port = server.server_address[:2]
    print(f"bridge: http://{addr}:{port}/v1/events → {args.serial}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        sink.close()
        print(f"bridge: 发送 {sink.sent}，丢弃 {sink.dropped}，重连 {sink.reconnects}",
              flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
