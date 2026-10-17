#!/usr/bin/env python3
"""生成 6 帧原创"毛茸茸头像"立绘（Muse/Jolly 风格**启发**，不复制任何素材）。

只画**一个蓬松的头**：无身体、无四肢、无耳朵；用蓬松剪影 + 柔和明暗表达"毛茸茸"。
帧序（与 pet_frame_t 0..5 一致）：
    0 NORMAL / 1 EYES_CLOSED / 2 HAPPY / 3 SAD / 4 WORKING / 5 INPUT

格式（LOOK 包，与固件 pet_render/pet_look_parse 一致）：
    0 magic "LOOK"；4 u8 w(48) h(64) frames(6) zero；8 u16[16] palette(RGB565 LE)；
   40 frames，4bpp，行优先，左像素在低半字节。

用法：python3 tools/make-look.py > firmware/components/pet/assets/robot.look
"""
import math
import struct
import sys

W, H, FRAMES = 48, 64, 6


def c(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


CREAM = c(246, 237, 220)
SHADE = c(214, 190, 162)
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

CX, CY, RX, RY = 24, 32, 21, 23


def blank():
    return [[T] * W for _ in range(H)]


def setp(g, x, y, col):
    if 0 <= x < W and 0 <= y < H:
        g[y][x] = col


def in_fluff(x, y, cx, cy, rx, ry):
    dx = (x + 0.5 - cx) / rx
    dy = (y + 0.5 - cy) / ry
    d = math.hypot(dx, dy)
    if d == 0.0:
        return True
    ang = math.atan2(dy, dx)
    return d <= 1.0 + 0.075 * abs(math.sin(4.5 * ang))


def fluff_head(g):
    for y in range(H):
        for x in range(W):
            if in_fluff(x, y, CX, CY, RX, RY):
                g[y][x] = BODY
    for y in range(H):
        for x in range(W):
            if g[y][x] != BODY:
                continue
            for (dx, dy) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if not (0 <= nx < W and 0 <= ny < H) or g[ny][nx] == T:
                    g[y][x] = OUT
                    break
    def tuft(x, y, col):
        if 0 <= x < W and 0 <= y < H and g[y][x] == BODY:
            g[y][x] = col

    # 顶部左侧高光毛簇 / 底部右侧阴影毛簇（少量，避免噪点）
    for (x, y) in ((9, 16), (11, 13), (14, 11), (18, 10)):
        tuft(x, y, WH)
    for (x, y) in ((9, 46), (13, 50), (18, 53), (24, 54), (30, 53), (35, 50), (39, 46)):
        tuft(x, y, SHADOW)


def disc(g, cx, cy, rx, ry, fill):
    for y in range(H):
        for x in range(W):
            dx = (x + 0.5 - cx) / rx
            dy = (y + 0.5 - cy) / ry
            if dx * dx + dy * dy <= 1.0:
                g[y][x] = fill


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


def eyes_open(g, dy=0, wide=False):
    for ex in (17, 31):
        disc(g, ex, 30 + dy, 2.4 if wide else 2.2, 3.4 if wide else 3.2, OUT)
        setp(g, ex - 1, 28 + dy, WH)


def eyes_closed(g, dy=0, happy=False):
    for ex in (17, 31):
        if happy:
            for (dx, ddy) in ((-2, 1), (-1, 0), (0, 0), (1, 1)):
                setp(g, ex + dx, 31 + dy + ddy, OUT)
        else:
            for (dx, ddy) in ((-2, 0), (-1, 1), (0, 1), (1, 0)):
                setp(g, ex + dx, 31 + dy + ddy, OUT)


def mouth_smile(g, dy=0):
    for (dx, ddy) in ((-2, 0), (-1, 1), (0, 1), (1, 0)):
        setp(g, 24 + dx, 39 + dy + ddy, OUT)


def mouth_line(g, dy=0):
    for dx in (-1, 0, 1):
        setp(g, 24 + dx, 39 + dy, OUT)


def mouth_o(g, dy=0):
    disc(g, 24, 39 + dy, 1.5, 1.5, OUT)


def blush(g, dy=0):
    disc(g, 10, 37 + dy, 2.5, 1.5, BL)
    disc(g, 38, 37 + dy, 2.5, 1.5, BL)


def frame_idle():
    g = blank()
    fluff_head(g)
    eyes_open(g)
    blush(g)
    mouth_smile(g)
    return g


def frame_closed():
    g = blank()
    fluff_head(g)
    eyes_closed(g)
    blush(g)
    mouth_line(g)
    return g


def frame_happy():
    g = blank()
    fluff_head(g)
    eyes_closed(g, happy=True)
    blush(g)
    disc(g, 24, 39, 2.5, 2, OUT)
    setp(g, 24, 40, BL)
    sparkle(g, 2, 13, SPK)
    sparkle(g, 46, 15, SPK)
    return g


def frame_sad():
    g = blank()
    fluff_head(g)
    for (x, y) in ((14, 24), (15, 25), (16, 26)):
        setp(g, x, y, OUT)
        setp(g, 47 - x + 1, y, OUT)
    for ex in (17, 31):
        disc(g, ex, 31, 2, 2.5, OUT)
        setp(g, ex - 1, 33, WH)
    blush(g)
    for (dx, ddy) in ((-2, 1), (-1, 0), (0, 1), (1, 0), (2, 1)):
        setp(g, 24 + dx, 39 + ddy, OUT)
    for (x, y) in ((42, 15), (42, 16), (41, 17), (42, 17), (43, 17), (41, 18), (42, 18), (43, 18)):
        setp(g, x, y, DROP)
    setp(g, 41, 17, WH)
    return g


def frame_working():
    g = blank()
    fluff_head(g)
    for ex in (17, 31):
        disc(g, ex, 32, 2.2, 1.4, OUT)
        setp(g, ex - 1, 30, WH)
    mouth_line(g)
    return g


def frame_input():
    g = blank()
    fluff_head(g)
    eyes_open(g, wide=True)
    blush(g)
    mouth_o(g)
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
