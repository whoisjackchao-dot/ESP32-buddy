#!/usr/bin/env python3
"""生成占位立绘（自绘：只有一个"蓝色机器人头"，风格参考 vibebuddy README 实机截图）。

净室说明：只按**外观**重画（蓝头 / 深蓝脸屏 / 青色方块眼 / 浅蓝耳 / 天线），
不复制上游任何代码或其立绘数据文件。

格式（与上游 firmware-rs/core/src/look.rs 的**格式规格**一致）：
   0  magic "LOOK"
   4  u8 width(48) u8 height(64) u8 frames(4) u8 zero
   8  u16[16] palette (RGB565, little-endian)；index 0 = 透明
  40  frames，顺序 [Normal, EyesClosed, Happy, Sad]；4bpp，行优先，左像素在低半字节。

用法：python3 tools/make-look.py > firmware/components/pet/assets/robot.look
"""
import struct
import sys

W, H, FRAMES = 48, 64, 4


def c(r, g, b):
    """RGB888 → RGB565"""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# 参考实机截图的配色
BLUE = c(74, 123, 200)     # 头壳
LBLUE = c(143, 184, 232)   # 耳朵 / 高光
NAVY = c(22, 34, 59)       # 脸屏（深）
CYAN = c(63, 200, 232)     # 眼 / 嘴
DBLUE = c(36, 64, 110)     # 描边

PALETTE = [
    0x0000,  # 0 透明
    BLUE,    # 1
    LBLUE,   # 2
    NAVY,    # 3
    CYAN,    # 4
    DBLUE,   # 5
    0x0000,  # 6
    0x0000,  # 7
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,  # 8–15 预留
]
assert len(PALETTE) == 16

T, BODY, EAR, SCR, EYE, OUT = 0, 1, 2, 3, 4, 5

HEAD = (4, 14, 43, 49)     # 宽 40 × 高 36（宽扁，参考实机头型）
SCREEN = (9, 20, 38, 43)   # 宽 30 × 高 24
EYE_L = (13, 27, 16, 32)
EYE_R = (31, 27, 34, 32)
MOUTH_Y = 38


def blank():
    return [[T] * W for _ in range(H)]


def rect(g, x0, y0, x1, y1, col):
    for y in range(max(0, y0), min(H, y1 + 1)):
        for x in range(max(0, x0), min(W, x1 + 1)):
            g[y][x] = col


def hline(g, x0, x1, y, col):
    rect(g, x0, y, x1, y, col)


def clear(g, x, y):
    if 0 <= x < W and 0 <= y < H:
        g[y][x] = T


def rounded(g, x0, y0, x1, y1, col, cut=2):
    rect(g, x0, y0, x1, y1, col)
    for d in range(cut):
        for e in range(cut - d):
            clear(g, x0 + e, y0 + d)
            clear(g, x1 - e, y0 + d)
            clear(g, x0 + e, y1 - d)
            clear(g, x1 - e, y1 - d)


def robot_head(frame):
    g = blank()

    # 天线：短杆 + 小球
    rect(g, 23, 9, 24, 13, OUT)
    rect(g, 21, 4, 26, 9, EAR)
    rect(g, 22, 5, 25, 8, BODY)

    # 两侧耳朵（浅蓝）：比头稍凸出
    rounded(g, 0, 25, 4, 38, EAR, cut=1)
    rounded(g, 43, 25, 47, 38, EAR, cut=1)

    # 头壳（蓝）+ 描边
    rounded(g, *HEAD, BODY, cut=4)
    hline(g, HEAD[0] + 4, HEAD[2] - 4, HEAD[1], OUT)
    hline(g, HEAD[0] + 4, HEAD[2] - 4, HEAD[3], OUT)
    for y in range(HEAD[1] + 4, HEAD[3] - 3):
        g[y][HEAD[0]] = OUT
        g[y][HEAD[2]] = OUT

    # 脸屏（深蓝）
    rounded(g, *SCREEN, SCR, cut=3)

    # 眼 + 嘴（按帧）
    if frame == 0:      # Normal：方眼 + 平嘴（两端微上扬）
        rect(g, *EYE_L, EYE)
        rect(g, *EYE_R, EYE)
        hline(g, 21, 26, MOUTH_Y, EYE)
    elif frame == 1:    # EyesClosed：横线眼
        hline(g, EYE_L[0], EYE_L[2], 29, EYE)
        hline(g, EYE_R[0], EYE_R[2], 29, EYE)
        hline(g, 21, 26, MOUTH_Y, EYE)
    elif frame == 2:    # Happy：^ 眼 + 微笑（两端上翘）
        for (x0, x1) in ((EYE_L[0], EYE_L[2]), (EYE_R[0], EYE_R[2])):
            mid = (x0 + x1) // 2
            g[29][mid - 1] = EYE
            g[29][mid] = EYE
            g[29][mid + 1] = EYE
            g[28][mid - 2] = EYE
            g[28][mid + 2] = EYE
        hline(g, 21, 26, MOUTH_Y + 1, EYE)
        g[MOUTH_Y][20] = EYE
        g[MOUTH_Y][27] = EYE
    else:               # Sad：垂眼 + 下撇嘴
        hline(g, EYE_L[0], EYE_L[2], 29, EYE)
        g[28][EYE_L[0]] = EYE
        hline(g, EYE_R[0], EYE_R[2], 29, EYE)
        g[28][EYE_R[2]] = EYE
        hline(g, 21, 26, MOUTH_Y - 1, EYE)
        g[MOUTH_Y][20] = EYE
        g[MOUTH_Y][27] = EYE
    return g


def pack_frames():
    out = bytearray()
    for f in range(FRAMES):
        g = robot_head(f)
        flat = [g[y][x] for y in range(H) for x in range(W)]
        assert all(0 <= v < 16 for v in flat), "像素值必须是调色板索引 0..15（不是 RGB565 颜色值）"
        for i in range(0, len(flat), 2):
            lo = flat[i] & 0x0F
            hi = (flat[i + 1] & 0x0F) if i + 1 < len(flat) else 0
            out.append(lo | (hi << 4))
    return bytes(out)


def main():
    pal = b"".join(struct.pack("<H", v) for v in PALETTE)
    data = b"LOOK" + bytes([W, H, FRAMES, 0]) + pal + pack_frames()
    expect = 8 + 32 + FRAMES * (W * H // 2)
    assert len(data) == expect, (len(data), expect)
    sys.stdout.buffer.write(data)


if __name__ == "__main__":
    main()
