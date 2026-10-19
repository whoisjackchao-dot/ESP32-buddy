#!/usr/bin/env bash
# 生成"可直接带去别的机器烧录"的烧录包：flash/
#   flash/bootloader.bin / partition-table.bin / esp32_buddy.bin
#   flash/flash.sh    （Linux/macOS/WSL，esptool）
#   flash/flash.ps1   （Windows，esptool）
#   flash/MANIFEST.txt（大小 + sha256，权威偏移）
#   flash/README.md   （三种烧录方式的说明与排错）
#
# 用法：
#   tools/package-flash.sh                 # 用默认构建目录 ~/esp-build/buddy
#   BUILD_DIR=/path/to/build tools/package-flash.sh
#   tools/package-flash.sh --build         # 先构建再打包
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BUILD_DIR="${BUILD_DIR:-$HOME/esp-build/buddy}"
OUT="$ROOT/flash"

if [ "${1:-}" = "--build" ]; then
    ( cd firmware && source ../tools/idf-env.sh >/dev/null && idf.py -B "$BUILD_DIR" build )
fi

for f in bootloader/bootloader.bin partition_table/partition-table.bin esp32_buddy.bin; do
    if [ ! -f "$BUILD_DIR/$f" ]; then
        echo "ERROR: 找不到 $BUILD_DIR/$f —— 先构建，或用 BUILD_DIR 指定构建目录。" >&2
        exit 1
    fi
done

mkdir -p "$OUT"
cp "$BUILD_DIR/bootloader/bootloader.bin"          "$OUT/bootloader.bin"
cp "$BUILD_DIR/partition_table/partition-table.bin" "$OUT/partition-table.bin"
cp "$BUILD_DIR/esp32_buddy.bin"                     "$OUT/esp32_buddy.bin"

# 偏移取自构建目录的 flasher_args.json（权威来源），避免手抄出错
OFF_BOOT=$(python3 -c "import json;d=json.load(open('$BUILD_DIR/flasher_args.json'));print(d['flash_files'] and [k for k,v in d['flash_files'].items() if 'bootloader' in v][0])")
OFF_PART=$(python3 -c "import json;d=json.load(open('$BUILD_DIR/flasher_args.json'));print([k for k,v in d['flash_files'].items() if 'partition' in v][0])")
OFF_APP=$(python3 -c "import json;d=json.load(open('$BUILD_DIR/flasher_args.json'));print([k for k,v in d['flash_files'].items() if v.endswith('esp32_buddy.bin')][0])")

cat > "$OUT/MANIFEST.txt" <<EOF
ESP32-buddy 烧录包
生成时间: $(date -Iseconds)
构建目录: $BUILD_DIR
芯片: esp32s3    flash: 8MB  模式: dio  频率: 80m

偏移（来自构建目录 flasher_args.json）:
  bootloader.bin        @ $OFF_BOOT
  partition-table.bin   @ $OFF_PART
  esp32_buddy.bin       @ $OFF_APP

文件（大小 / sha256）:
EOF
for f in bootloader.bin partition-table.bin esp32_buddy.bin; do
    printf '  %-20s %8s  %s\n' "$f" "$(stat -c%s "$OUT/$f")" "$(sha256sum "$OUT/$f" | cut -d' ' -f1)" >> "$OUT/MANIFEST.txt"
done

cat > "$OUT/flash.sh" <<'EOF'
#!/usr/bin/env bash
# Linux / macOS / WSL 烧录（需要 esptool：pip install esptool）
set -eu
PORT="${1:?用法: ./flash.sh <串口，如 /dev/ttyACM0 或 /dev/cu.usbmodem8401>}"
cd "$(dirname "$0")"
python3 -m esptool --chip esp32s3 --port "$PORT" --baud 921600 \
  write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 esp32_buddy.bin
EOF
chmod +x "$OUT/flash.sh"

cat > "$OUT/flash.ps1" <<'EOF'
# Windows 烧录（需要 Python + esptool：py -m pip install esptool）
param([string]$Port = "COM5")
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
py -m esptool --chip esp32s3 --port $Port --baud 921600 `
  write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 esp32_buddy.bin
EOF

cat > "$OUT/README.md" <<'EOF'
# ESP32-buddy 烧录包

> 目标板：**M5Stack StickS3**（ESP32-S3）。把本目录整个拷到要烧录的机器上。

## 文件

| 文件 | 作用 |
| --- | --- |
| `bootloader.bin` / `partition-table.bin` / `esp32_buddy.bin` | 三件套 |
| `flash.sh` | Linux/macOS/WSL 一条命令烧录 |
| `flash.ps1` | Windows 一条命令烧录 |
| `MANIFEST.txt` | 大小、sha256、**权威偏移**（核对用） |

**偏移固定为：** bootloader `0x0` / 分区表 `0x8000` / app `0x10000`。

## 方式 A：有 ESP-IDF 的机器（推荐）

在**仓库**里：

```bash
cd firmware
source ../tools/idf-env.sh
idf.py -p <串口> flash monitor
```

`monitor` 里应能看到 `DISPLAY READY M1` / `PIXEL CARD READY`，之后**每 10 秒一行** `hb: uptime=... heap=...`。

## 方式 B：只有 esptool 的机器

先 `pip install esptool`，然后：

- Linux/macOS/WSL：`./flash.sh /dev/ttyACM0`
- Windows：`powershell -ExecutionPolicy Bypass -File flash.ps1 -Port COM5`
- 手动等价命令：
  ```
  esptool --chip esp32s3 --port <串口> --baud 921600 write_flash \
    0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 esp32_buddy.bin
  ```

看日志（115200）：`python3 -m serial.tools.miniterm <串口> 115200`

## 方式 C：浏览器

https://espressif.github.io/esptool-js/ → 连接 → 按 MANIFEST 的三个偏移分别写入（可选，较慢）。

## 排错

| 现象 | 处理 |
| --- | --- |
| `Failed to connect ... No serial data received` | 按住板上的 **KEY1** 再插 USB（进下载模式），重跑 |
| 找不到串口 | 换数据线（要能传数据）、换 USB 口；Linux 需在 `dialout`/`uucp` 组 |
| 烧完黑屏 | 确认烧的是 **USB-SLAVE** 口；确认三件套偏移与 MANIFEST 一致 |
| 开头的启动日志看不到 | 正常：USB-Serial-JTAG 会重新枚举；**心跳**能证明程序在跑 |

## 说明

- 本包**不含出厂固件备份**（用户决定不备份；刷机会覆盖现有固件）。
- 刷完屏幕应显示**一个蓝色机器人头**（M1）——这是 H 类验收的观察点。
EOF

echo "已生成烧录包：$OUT"
ls -l "$OUT"
echo
cat "$OUT/MANIFEST.txt"
