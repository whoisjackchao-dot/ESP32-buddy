#!/usr/bin/env python3
"""生成 6 帧原创毛绒吉祥物立绘（Muse/Jolly 风格**启发**，不复制任何上游/Meta 素材）。

帧序（与 pet_frame_t 0..5 一致）：
    0 NORMAL / 1 EYES_CLOSED / 2 HAPPY / 3 SAD / 4 WORKING / 5 INPUT

格式（LOOK 包，与固件 pet_render/pet_look_parse 一致）：
    0  magic "LOOK"
    4  u8 width(48) u8 height(64) u8 frames(6) u8 zero
    8  u16[16] palette (RGB565, little-endian)；index 0 = 透明
   40  frames，4bpp，行优先，左像素在低半字节

用法：python3 tools/make-look.py > firmware/components/pet/assets/robot.look
"""
import struct
import sys

W, H, FRAMES = 48, 64, 6


def c(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


CREAM = c(246, 237, 220)
SHADE = c(221, 199, 169)
INK = c(84, 58, 46)
BLUSH = c(247, 157, 160)
PEACH = c(240, 205, 178)
GRAY = c(170, 175, 188)
DGRAY = c(104, 110, 126)
SCREEN = c(40, 50, 68)
CYAN = c(112, 225, 235)
YELLOW = c(255, 214, 64)
WHITE = c(255, 255, 255)
BLUE = c(120, 186, 240)
ORANGE = c(255, 140, 64)

PALETTE = [0, CREAM, SHADE, INK, BLUSH, PEACH, GRAY, DGRAY,
           SCREEN, CYAN, YELLOW, WHITE, BLUE, ORANGE, 0, 0]
assert len(PALETTE) == 16

(T, BODY, SHADOW, OUT, BL, BELLY, LP, LPB, SCR, TXT, SPK, WH, DROP, MK, _u, _v) = range(16)


def blank():
    return [[T] * W for _ in range(H)]


def setp(g, x, y, col):
    if 0 <= x < W and 0 <= y < H:
        g[y][x] = col


def in_ell(x, y, cx, cy, rx, ry):
    dx = (x + 0.5 - cx) / rx
    dy = (y + 0.5 - cy) / ry
    return dx * dx + dy * dy <= 1.0


def disc(g, cx, cy, rx, ry, fill, outline=None):
    for y in range(H):
        for x in range(W):
            if in_ell(x, y, cx, cy, rx, ry):
                g[y][x] = fill
    if outline is not None:
        for y in range(H):
            for x in range(W):
                if in_ell(x, y, cx, cy, rx + 1, ry + 1) and not in_ell(x, y, cx, cy, rx, ry):
                    g[y][x] = outline


def rect(g, x0, y0, x1, y1, col):
    for y in range(max(0, y0), min(H, y1 + 1)):
        for x in range(max(0, x0), min(W, x1 + 1)):
            g[y][x] = col


def sparkle(g, cx, cy, col):
    setp(g, cx, cy, col)
    for d in (1, 2):
        setp(g, cx + d, cy, col)
        setp(g, cx - d, cy, col)
        setp(g, cx, cy + d, col)
        setp(g, cx, cy - d, col)


def base(g, dy=0, head_dy=0, arms="side"):
    for ex in (11, 37):
        disc(g, ex, 9 + dy, 5, 5, BODY, OUT)
        disc(g, ex, 8 + dy, 2, 2, BELLY)
    if arms == "side":
        disc(g, 9, 44 + dy, 4, 4, BODY, OUT)
        disc(g, 39, 44 + dy, 4, 4, BODY, OUT)
    elif arms == "up":
        for (x, y) in ((16, 41), (13, 36), (10, 32)):
            disc(g, x, y + dy, 3.5, 3.5, BODY, OUT)
        for (x, y) in ((32, 41), (35, 36), (38, 32)):
            disc(g, x, y + dy, 3.5, 3.5, BODY, OUT)
    disc(g, 24, 47 + dy, 12, 10, BODY, OUT)
    disc(g, 24, 48 + dy, 7, 7, BELLY)
    disc(g, 24, 21 + dy + head_dy, 16, 13, BODY, OUT)


def eyes_open(g, hy, wide=False):
    for ex in (17, 31):
        disc(g, ex, 23 + hy, 2.2 if wide else 2, 3.2 if wide else 3, OUT)
        setp(g, ex - 1, 21 + hy, WH)


def eyes_closed(g, hy, happy=False):
    for ex in (17, 31):
        if happy:
            for (dx, dy) in ((-2, 1), (-1, 0), (0, 0), (1, 1)):
                setp(g, ex + dx, 24 + hy + dy, OUT)
        else:
            for (dx, dy) in ((-2, 0), (-1, 1), (0, 1), (1, 0)):
                setp(g, ex + dx, 24 + hy + dy, OUT)


def mouth_smile(g, hy):
    for (dx, dy) in ((-2, 0), (-1, 1), (0, 1), (1, 0)):
        setp(g, 24 + dx, 30 + hy + dy, OUT)


def mouth_line(g, hy):
    for dx in (-1, 0, 1):
        setp(g, 24 + dx, 31 + hy, OUT)


def mouth_o(g, hy):
    disc(g, 24, 31 + hy, 1.5, 1.5, OUT)


def blush(g, hy):
    disc(g, 11, 28 + hy, 2.5, 1.5, BL)
    disc(g, 37, 28 + hy, 2.5, 1.5, BL)


def frame_idle():
    g = blank()
    base(g)
    eyes_open(g, 0)
    blush(g, 0)
    mouth_smile(g, 0)
    return g


def frame_closed():
    g = blank()
    base(g)
    eyes_closed(g, 0)
    blush(g, 0)
    mouth_line(g, 0)
    return g


def frame_happy():
    g = blank()
    base(g, dy=-2, arms="up")
    eyes_closed(g, -2, happy=True)
    blush(g, -2)
    disc(g, 24, 31 - 2, 2.5, 2, OUT)
    setp(g, 24, 32 - 2, BL)
    sparkle(g, 3, 10, SPK)
    sparkle(g, 45, 12, SPK)
    return g


def frame_sad():
    g = blank()
    base(g, dy=1)
    hy = 1
    for (x, y) in ((14, 18), (15, 19), (16, 20)):
        setp(g, x, y, OUT)
        setp(g, 47 - x + 1, y, OUT)
    for ex in (17, 31):
        disc(g, ex, 24 + hy, 2, 2.5, OUT)
        setp(g, ex - 1, 26 + hy, WH)
    blush(g, hy)
    for (dx, dy) in ((-2, 1), (-1, 0), (0, 1), (1, 0), (2, 1)):
        setp(g, 24 + dx, 31 + hy + dy, OUT)
    for (x, y) in ((43, 11), (43, 12), (42, 13), (43, 13), (44, 13), (42, 14), (43, 14), (44, 14)):
        setp(g, x, y, DROP)
    setp(g, 42, 13, WH)
    return g


def frame_working():
    g = blank()
    base(g, head_dy=3)
    hy = 3
    for ex in (17, 31):
        disc(g, ex, 26, 2, 1.5, OUT)
    mouth_line(g, hy)
    rect(g, 15, 43, 32, 51, LP)
    rect(g, 16, 44, 31, 50, SCR)
    rect(g, 18, 45, 27, 45, TXT)
    rect(g, 18, 47, 23, 47, TXT)
    rect(g, 12, 52, 35, 53, LPB)
    disc(g, 16, 51, 2, 2, BODY, OUT)
    disc(g, 32, 51, 2, 2, BODY, OUT)
    return g


def frame_input():
    g = blank()
    base(g, arms="side")
    eyes_open(g, 0, wide=True)
    blush(g, 0)
    mouth_o(g, 0)
    disc(g, 42, 30, 3.5, 3.5, BODY, OUT)
    rect(g, 1, 2, 7, 10, OUT)
    rect(g, 2, 3, 6, 9, WH)
    setp(g, 1, 2, T)
    setp(g, 7, 2, T)
    setp(g, 1, 10, T)
    setp(g, 7, 10, T)
    rect(g, 4, 4, 4, 7, MK)
    setp(g, 4, 9, MK)
    setp(g, 7, 11, OUT)
    setp(g, 8, 12, OUT)
    return g


DRAW = [frame_idle, frame_closed, frame_happy, frame_sad, frame_working, frame_input]


def pack_frames():
    out = bytearray()
    for f in range(FRAMES):
        g = DRAW[f]()
        flat = [g[y][x] for y in range(H) for x in range(W)]
        assert all(0 <= v < 16 for v in flat), "像素值必须是调色板索引 0..15"
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
