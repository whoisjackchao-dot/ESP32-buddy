#!/usr/bin/env python3
"""B5：BLE 控制兜底桥。

连接设备 BLE（GATT 0xFFE0 / TX 0xFFE2 notify），把控制行
（voice.text / send / back_window）转发到本机 :8767（file_to_serial 的 ControlServer），
与 USB / WiFi 两条通道共用同一消费者与 500ms 去重。

依赖：Windows Python 3.12 + winrt 包（winrt-Windows.Devices.Bluetooth*）。
用法：python ble_bridge.py --addr 0x70041DD621D2 --local 127.0.0.1:8767 --log D:\\...\\ble.log
"""
import argparse
import asyncio
import os
import socket
import sys
import time

DEVICE_SERVICE = "0000ffe0"
DEVICE_TX = "0000ffe2"


def log(msg, path):
    line = "%s %s\n" % (time.strftime("%Y-%m-%dT%H:%M:%S"), msg)
    if path:
        try:
            with open(path, "a", encoding="utf-8") as f:
                f.write(line)
        except OSError:
            pass
    print(line, end="", flush=True)


def forward(line, local):
    host, port = local.rsplit(":", 1)
    try:
        with socket.create_connection((host, int(port)), timeout=2) as s:
            s.sendall(line.encode("utf-8") + b"\n")
        return True
    except OSError:
        return False


async def main_async(args, stop_file):
    # 延迟导入：非 Windows 环境不影响 import 检查
    from winrt.windows.devices.bluetooth import BluetoothLEDevice
    from winrt.windows.devices.bluetooth.genericattributeprofile import (
        GattClientCharacteristicConfigurationDescriptorValue,
    )

    addr = int(args.addr, 16)
    got = {"n": 0}

    def on_value(sender, event_args):
        try:
            buf = event_args.characteristic_value
            data = bytes(buf) if buf is not None else b""
        except Exception:
            return
        if not data:
            return
        text = data.decode("utf-8", "replace").strip()
        if not text:
            return
        got["n"] += 1
        ok = forward(text, args.local)
        log("ble->8767 ok=%s %s" % (ok, text[:80]), args.log)

    while not (stop_file and os.path.exists(stop_file)):
        try:
            dev = await BluetoothLEDevice.from_bluetooth_address_async(addr)
            if dev is None:
                log("ble: device not found, retry", args.log)
                await asyncio.sleep(3)
                continue
            sres = await dev.get_gatt_services_async()
            found = False
            for svc in sres.services:
                if not str(svc.uuid).lower().startswith(DEVICE_SERVICE):
                    continue
                chars = (await svc.get_characteristics_async()).characteristics
                for ch in chars:
                    if not str(ch.uuid).lower().startswith(DEVICE_TX):
                        continue
                    ch.add_value_changed(on_value)
                    status = await ch.write_client_characteristic_configuration_descriptor_async(
                        GattClientCharacteristicConfigurationDescriptorValue.NOTIFY)
                    log("ble: subscribed status=%s" % status, args.log)
                    found = True
            if not found:
                log("ble: service not found, retry", args.log)
                await asyncio.sleep(3)
                continue
            while not (stop_file and os.path.exists(stop_file)):
                await asyncio.sleep(1)
        except Exception as exc:  # 断线/设备重启/权限 → 退避重连
            log("ble: error %s: %s (retry)" % (type(exc).__name__, exc), args.log)
            await asyncio.sleep(3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--addr", default="0x70041DD621D2")
    ap.add_argument("--local", default="127.0.0.1:8767")
    ap.add_argument("--log", default="")
    ap.add_argument("--stop-file", default="")
    args = ap.parse_args()
    try:
        asyncio.run(main_async(args, args.stop_file))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
