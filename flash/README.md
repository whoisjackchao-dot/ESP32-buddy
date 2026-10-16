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
