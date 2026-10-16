#!/usr/bin/env bash
# ESP32-buddy ESP-IDF 环境包装（WSL）—— 借用 StickS3×OpenCode 项目的既有验证经验。
#
# 用法（必须 source）：
#   source tools/idf-env.sh
#   idf.py -B ~/esp-build/buddy set-target esp32s3
#   idf.py -B ~/esp-build/buddy build
#
# 注意（来自既有项目的教训，见 note）：
#   1) 构建目录必须放 WSL 原生盘（~/esp-build/...）；放 /mnt/c 会慢约 30 倍。
#   2) 系统 python3 是 3.14，需显式指定 ESP-IDF 的 venv。
#   3) 本机走企业代理；组件下载需要 http(s)_proxy。
#   4) 未走 git submodule（GitHub 直连不可达），用官方变量跳过子模块检查。

BUDDY_IDF_PATH="${BUDDY_IDF_PATH:-$HOME/esp/esp-idf}"
BUDDY_LIBUSB_DIR="${BUDDY_LIBUSB_DIR:-$HOME/.local/share/sticks3-esp-libs/usr/lib/x86_64-linux-gnu}"
# 企业代理：如在需要代理的网络里构建/下载组件，请自行导出 BUDDY_PROXY（不写死内网地址）
BUDDY_PROXY="${BUDDY_PROXY:-}"

if [ ! -f "$BUDDY_IDF_PATH/export.sh" ]; then
    echo "ERROR: 找不到 ESP-IDF：$BUDDY_IDF_PATH/export.sh" >&2
    return 1 2>/dev/null || exit 1
fi

# 自动探测 ESP-IDF python venv（优先 idf5.5）
if [ -z "${IDF_PYTHON_ENV_PATH:-}" ]; then
    for d in "$HOME"/.espressif/python_env/idf5.5_py*_env "$HOME"/.espressif/python_env/idf6.*_env; do
        if [ -d "$d" ]; then IDF_PYTHON_ENV_PATH="$d"; break; fi
    done
fi
[ -n "${IDF_PYTHON_ENV_PATH:-}" ] && export IDF_PYTHON_ENV_PATH

# 子模块内容已由源文件补齐，跳过无法完成的 git 检查
export IDF_SKIP_CHECK_SUBMODULES=1

# 用户态 libusb（openocd 工具校验需要；本机无免密 sudo）
if [ -e "$BUDDY_LIBUSB_DIR/libusb-1.0.so.0" ]; then
    export LD_LIBRARY_PATH="$BUDDY_LIBUSB_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

# 企业代理：让 idf.py 的组件下载能走通（未设置则不动）
if [ -n "$BUDDY_PROXY" ]; then
    export http_proxy="$BUDDY_PROXY"
    export https_proxy="$BUDDY_PROXY"
fi

# shellcheck disable=SC1090
. "$BUDDY_IDF_PATH/export.sh" >/dev/null
echo "idf-env: IDF_PATH=$IDF_PATH  python_env=${IDF_PYTHON_ENV_PATH:-<none>}  proxy=${BUDDY_PROXY:-<none>}"
