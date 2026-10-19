# 可复现构建（固件）

> 目标：同一 commit 在干净环境构建出可核对的固件；**不需要连接设备**。

## 固定工具链

| 项 | 值 |
| --- | --- |
| ESP-IDF | **v5.5.4**（release/v5.5） |
| 目标芯片 | esp32s3 |
| Python | ESP-IDF 自带 venv（系统 Python 3.14 不直接用于 IDF） |
| 构建目录 | WSL/Linux 原生盘：`$HOME/esp-build/buddy`（不要放 `/mnt/c`） |

`tools/build-firmware.sh` 会读取 `IDF_PATH/tools/cmake/version.cmake` 并**拒绝非 v5.5.4**：

```bash
tools/build-firmware.sh --check
# ESP-IDF: v5.5.4 (...)
# toolchain check: PASS

tools/build-firmware.sh
```

## 容器（推荐给 CI / 干净机器）

```bash
docker run --rm -it \
  -v "$PWD":/project -w /project/firmware \
  espressif/idf:v5.5.4 \
  idf.py -B /tmp/esp-build/buddy build
```

CI 使用同一镜像：`.github/workflows/ci.yml` 的 `firmware` job。

## 本机现状（2026-10-19）

- `~/esp/esp-idf` 是 `release/v5.5`，`version.cmake` 为 **5.5.5**；
- `~/esp/esp-idf-v6` 为 **6.0.1**；
- 两者都**不是**项目固定的 5.5.4，因此 `tools/build-firmware.sh` 会明确报错，而不是“能构建就算通过”。

## 生成发布清单

```bash
BUILD_DIR="$HOME/esp-build/buddy" tools/release-manifest.sh
# 或先打包再生成：
BUILD_DIR="$HOME/esp-build/buddy" tools/release-manifest.sh --package
```

输出 `flash/MANIFEST.json`：

- commit / branch / IDF 版本；
- 每个镜像的偏移、大小、sha256；
- 与 `flash/MANIFEST.txt` 并用，便于人工核对。

`flash/MANIFEST.json` 是生成物，已加入 `.gitignore`；发布时随烧录包一起分发。

## 已知限制

- 当前机器没有 v5.5.4 的本地安装；本地完整构建需容器或另装 v5.5.4。
- `tools/idf-env.sh` 仍可用于 5.5.x 的本机实验，但不能替代版本锁定检查。
- 构建成功不等于真机 H 通过；H 见 `tools/acceptance/README.md`。
