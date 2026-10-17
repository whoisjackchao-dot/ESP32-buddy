#!/usr/bin/env python3
"""生成 12 帧原创"毛茸茸头像"立绘（Muse/Jolly 风格**启发**，不复制任何素材）。

只画**一个蓬松的圆头**：无身体、无四肢、无耳朵；顶部有一撮会**左右摆动**的毛。
12 帧 = 6 态 × 2 个毛发相位（左/右），帧序：
    0/1 IDLE(L/R)  2/3 CLOSED  4/5 HAPPY  6/7 SAD  8/9 WORKING  10/11 INPUT
渲染时按 `frame*2 + hair_right` 取帧；相位由 pet_state 按 ~1s 周期切换（与眨眼同频）。

格式（LOOK 包，与固件 pet_render/pet_look_parse 一致）：
    0 magic "LOOK"；4 u8 w(48) h(64) frames(12) zero；8 u16[16] palette(RGB565 LE)；
   40 frames，4bpp，行优先，左像素在低半字节。

用法：python3 tools/make-look.py > firmware/components/pet/assets/robot.look
"""
import math
import struct
import sys

W, H, FRAMES = 48, 64, 12


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
HAIR = c(200, 170, 136)  # 浅棕发色（比 INK 浅很多）

PALETTE = [0, CREAM, SHADE, INK, BLUSH, PEACH, GRAY, DGRAY,
           SCREEN, CYAN, YELLOW, WHITE, BLUE, ORANGE, HAIR, 0]
assert len(PALETTE) == 16

(T, BODY, SHADOW, OUT, BL, BELLY, LP, LPB, SCR, TXT, SPK, WH, DROP, MK, HAIR, _v) = range(16)

CX, CY, RX, RY = 24, 32, 19, 26


def blank():
    return [[T] * W for _ in range(H)]


def setp(g, x, y, col):
    if 0 <= x < W and 0 <= y < H:
        g[y][x] = col


def in_fluff(x, y, cx, cy, rx, ry):
    t = (y + 0.5 - cy) / ry
    if t < -1.0 or t > 1.0:
        return False
    base = (1.0 - abs(t) ** 3) ** (1.0 / 2.5)  # 下巴更饱满/圆润
    half = base * (1.0 + 0.35 * t)             # 拉长的梨形（下宽上窄）
    if half <= 0.0:
        return False
    u = ((x + 0.5 - cx) / rx) / half
    r = math.hypot(u, t)
    if r == 0.0:
        return True
    ang = math.atan2(t, u)
    return r <= 1.0 + 0.07 * abs(math.sin(4.5 * ang))


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

    for (x, y) in ((9, 16), (11, 13), (14, 11), (18, 10)):
        tuft(x, y, WH)
    for (x, y) in ((9, 46), (13, 50), (18, 53), (24, 54), (30, 53), (35, 50), (39, 46)):
        tuft(x, y, SHADOW)


def hair(g, off):
    # 头顶一撮毛：更长、下宽上窄，越高摆动越大（off = -1 / +1）
    rows = [
        (8, -2, 4),
        (7, -2, 4),
        (6, -2, 4),
        (5, -3, 3),
        (4, -3, 3),
        (3, -3, 3),
        (2, -3, 2),
        (1, -3, 2),
    ]
    for (y, xs, w) in rows:
        shift = off * min(3, (8 - y) // 2)
        for k in range(w):
            setp(g, 24 + xs + k + shift, y, HAIR)


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


def face_idle(g):
    for ex in (17, 31):
        disc(g, ex, 32, 2.2, 3.2, OUT)
        setp(g, ex - 1, 30, WH)
    disc(g, 10, 39, 2.5, 1.5, BL)
    disc(g, 38, 39, 2.5, 1.5, BL)
    for (dx, dy) in ((-2, 0), (-1, 1), (0, 1), (1, 0)):
        setp(g, 24 + dx, 41 + dy, OUT)


def face_closed(g):
    for ex in (17, 31):
        for (dx, dy) in ((-2, 0), (-1, 1), (0, 1), (1, 0)):
            setp(g, ex + dx, 33 + dy, OUT)
    disc(g, 10, 39, 2.5, 1.5, BL)
    disc(g, 38, 39, 2.5, 1.5, BL)
    for dx in (-1, 0, 1):
        setp(g, 24 + dx, 41, OUT)


def face_happy(g):
    for ex in (17, 31):
        for (dx, dy) in ((-2, 1), (-1, 0), (0, 0), (1, 1)):
            setp(g, ex + dx, 33 + dy, OUT)
    disc(g, 10, 39, 2.5, 1.5, BL)
    disc(g, 38, 39, 2.5, 1.5, BL)
    disc(g, 24, 41, 2.5, 2, OUT)
    setp(g, 24, 42, BL)
    sparkle(g, 2, 15, SPK)
    sparkle(g, 46, 17, SPK)


def face_sad(g):
    for (x, y) in ((14, 26), (15, 27), (16, 28)):
        setp(g, x, y, OUT)
        setp(g, 47 - x + 1, y, OUT)
    for ex in (17, 31):
        disc(g, ex, 33, 2, 2.5, OUT)
        setp(g, ex - 1, 35, WH)
    disc(g, 10, 39, 2.5, 1.5, BL)
    disc(g, 38, 39, 2.5, 1.5, BL)
    for (dx, dy) in ((-2, 1), (-1, 0), (0, 1), (1, 0), (2, 1)):
        setp(g, 24 + dx, 41 + dy, OUT)
    for (x, y) in ((42, 17), (42, 18), (41, 19), (42, 19), (43, 19), (41, 20), (42, 20), (43, 20)):
        setp(g, x, y, DROP)
    setp(g, 41, 19, WH)


def face_working(g):
    for ex in (17, 31):
        disc(g, ex, 34, 2.2, 1.4, OUT)
        setp(g, ex - 1, 32, WH)
    for dx in (-1, 0, 1):
        setp(g, 24 + dx, 41, OUT)


def face_input(g):
    for ex in (17, 31):
        disc(g, ex, 32, 2.4, 3.4, OUT)
        setp(g, ex - 1, 30, WH)
    disc(g, 10, 39, 2.5, 1.5, BL)
    disc(g, 38, 39, 2.5, 1.5, BL)
    disc(g, 24, 41, 1.5, 1.5, OUT)
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


FACES = [face_idle, face_closed, face_happy, face_sad, face_working, face_input]


def build(state, off):
    g = blank()
    fluff_head(g)
    FACES[state](g)
    hair(g, off)
    return g


DRAW = [lambda s=s, o=o: build(s, o) for s in range(6) for o in (-1, 1)]


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
