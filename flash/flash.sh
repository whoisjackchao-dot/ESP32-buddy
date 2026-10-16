#!/usr/bin/env bash
# Linux / macOS / WSL 烧录（需要 esptool：pip install esptool）
set -eu
PORT="${1:?用法: ./flash.sh <串口，如 /dev/ttyACM0 或 /dev/cu.usbmodem8401>}"
cd "$(dirname "$0")"
python3 -m esptool --chip esp32s3 --port "$PORT" --baud 921600 \
  write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 esp32_buddy.bin
