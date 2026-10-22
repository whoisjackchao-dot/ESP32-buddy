#!/usr/bin/env python3
"""生成 M5 **占位**播报音频（合成音，非语音；无许可问题）。

格式（契约 M5 §5.1）：16-bit signed little-endian、单声道、16000 Hz。

用法（在仓库根目录）：
  python3 tools/make-voice.py firmware/components/pet/assets
"""
import math
import os
import struct
import sys

RATE = 16000


def tone(freq, secs, amp=0.45):
    n = int(RATE * secs)
    out = []
    for i in range(n):
        # 简单线性淡入淡出，避免爆音
        env = min(1.0, i / (RATE * 0.01), (n - i) / (RATE * 0.02))
        out.append(int(32767 * amp * env * math.sin(2 * math.pi * freq * i / RATE)))
    return out


def silence(secs):
    return [0] * int(RATE * secs)


# 三段各自可辨的短音
SAMPLES = {
    # 需要确认：两声上行
    "input.pcm": tone(740, 0.12) + silence(0.06) + tone(990, 0.14),
    # 完成：三声上行（大三和弦式）
    "done.pcm": tone(523, 0.10) + tone(659, 0.10) + tone(784, 0.18),
    # 失败：两声下行
    "failed.pcm": tone(440, 0.14) + silence(0.05) + tone(311, 0.22),
}


def main():
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(outdir, exist_ok=True)
    for name, samples in SAMPLES.items():
        data = b"".join(struct.pack("<h", max(-32768, min(32767, s))) for s in samples)
        path = os.path.join(outdir, name)
        with open(path, "wb") as f:
            f.write(data)
        secs = len(samples) / RATE
        print(f"{path}: {len(data)} bytes, {secs:.2f}s")
        assert 0.1 <= secs <= 3.0, "时长必须在 0.1–3.0s（契约 M5 §5.1）"


if __name__ == "__main__":
    main()
