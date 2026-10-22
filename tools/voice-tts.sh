#!/usr/bin/env bash
# M5 真实语音资产生成：edge-tts（免费、无需 API Key）→ 16kHz/16bit/单声道 PCM。
#
# 依赖（用户态即可，无需 sudo）：
#   pip install --break-system-packages edge-tts imageio-ffmpeg
# 如所在网络需要代理：
#   export https_proxy=http://<你的代理>:<端口>
#
# 用法：
#   tools/voice-tts.sh [输出目录]        # 默认 firmware/components/pet/assets
#   VOICE=zh-CN-XiaoshuangNeural tools/voice-tts.sh   # 换音色（童声）
set -eu

VOICE="${VOICE:-zh-CN-XiaoyiNeural}"   # 女声、活泼年轻
OUT="${1:-firmware/components/pet/assets}"

PROXY_ARG=()
if [ -n "${https_proxy:-}" ]; then
    PROXY_ARG=(--proxy "$https_proxy")
elif [ -n "${HTTPS_PROXY:-}" ]; then
    PROXY_ARG=(--proxy "$HTTPS_PROXY")
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

to_pcm() { # <in.mp3> <out.pcm>
    python3 - "$1" "$2" <<'PY'
import sys
try:
    import imageio_ffmpeg
except ImportError:
    sys.exit("需要 imageio-ffmpeg：pip install --break-system-packages imageio-ffmpeg")
import subprocess
ff = imageio_ffmpeg.get_ffmpeg_exe()
subprocess.run([ff, "-y", "-hide_banner", "-loglevel", "error",
                "-i", sys.argv[1], "-f", "s16le", "-ac", "1", "-ar", "16000",
                sys.argv[2]], check=True)
PY
}

gen() { # <name> <text>
    echo "合成 $1: $2"
    edge-tts "${PROXY_ARG[@]}" --voice "$VOICE" --text "$2" --write-media "$TMP/$1.mp3"
    to_pcm "$TMP/$1.mp3" "$OUT/$1.pcm"
}

mkdir -p "$OUT"
gen input  "需要你确认一下"
gen done   "都做完啦"
gen failed "出错啦"

echo
echo "音色：$VOICE（如需更可爱，可试 zh-CN-XiaoshuangNeural 童声）"
ls -l "$OUT"/input.pcm "$OUT"/done.pcm "$OUT"/failed.pcm
python3 - "$OUT" <<'PY'
import os, sys
d = sys.argv[1]
for n in ("input", "done", "failed"):
    p = os.path.join(d, n + ".pcm")
    sz = os.path.getsize(p)
    print(f"{n}.pcm: {sz} bytes, {sz/2/16000:.2f}s")
PY
