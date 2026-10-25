#!/usr/bin/env python3
"""生成 16×16 CJK 点阵字库头文件（firmware/components/pet/assets/cjk16.h）。

用法：
    python3 tools/make-cjk-font.py                  # 无本地字体则按 URL 下载缓存
    python3 tools/make-cjk-font.py --font /path.ttf # 指定本地字体文件

设计要点（M15：中文点阵字库）：
- 字源 **Noto Sans SC**（SIL Open Font License 1.1，© Adobe），
  与 Source Han Sans SC 同源。来源与哈希：
    URL    https://github.com/google/fonts/raw/main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf
    sha256 a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da
    许可   https://github.com/google/fonts/raw/main/ofl/notosanssc/OFL.txt
  许可登记见 NOTICE.md；OFL 要求保留版权行，本脚本把它写进生成头文件。
- 覆盖：ASCII 0x20..0x7E（95）+ GB2312 一级汉字（3755）+ 常用中文标点（12）
  = 3862 字。一级汉字 = 分区 16..55（首字节 B0..D7、次字节 A1..FE）。
- 光栅化：变量字重 560（Medium/SemiBold 之间，为 16px 无 hint 可读性选定）、
  4× 超采样（64px 渲染）、**50% 覆盖率阈值**（正文字重下笔画约占超采样块
  面积的一半，阈值取 50% 配合加重字重稳定出整笔画）；字集全体字形的
  ink 包络等比映射到 16×16 格并居中 → 各字形共享基线，标点沉底。
- 位序：行主序 16 行 × 2 字节/行，MSB = 最左列像素。
- 输出确定性：无时间戳、固定参数 → 同一字体文件 + 同一 Pillow/FreeType
  生成字节级一致（跨 Pillow 版本边缘 AA 可能微差）。
- 依赖：Pillow（含 FreeType）。仅在生成期联网下载字体；固件无网络依赖。
"""
import argparse
import hashlib
import os
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

FONT_URL = ("https://github.com/google/fonts/raw/main/ofl/notosanssc/"
            "NotoSansSC%5Bwght%5D.ttf")
FONT_SHA256 = "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da"
FONT_LICENSE = ("SIL Open Font License 1.1, Copyright 2014-2021 Adobe "
                "(Noto Sans SC; OFL.txt at "
                "https://github.com/google/fonts/raw/main/ofl/notosanssc/OFL.txt)")

CELL = 16          # 目标字号（像素）
SS = 4             # 超采样倍数（渲染尺寸 = CELL*SS 映射）
REF = 100          # 标定用参考字号
WGHT = 560         # 变量字重（Noto Sans SC wght 轴）
THRESH = 0.50      # 超采样块覆盖率阈值

# 常用中文标点（GB2312 A1 符号区，一级汉字区之外，逐个显式列出）
PUNCT = "，。！？：；、（）《》…"

DEFAULT_OUT = os.path.join(ROOT, "firmware", "components", "pet", "assets", "cjk16.h")
CACHE_DIR = os.path.join(os.path.expanduser("~"), ".cache", "esp32-buddy-fonts")


def gb2312_level1():
    """GB2312 一级汉字（分区 16..55）→ Unicode 码点列表。"""
    out = []
    for hi in range(0xB0, 0xD8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            out.append(ch)
    return out


def charset():
    """ASCII + GB2312 一级 + 标点，去重升序。"""
    chars = [chr(c) for c in range(0x20, 0x7F)]
    chars += gb2312_level1()
    chars += list(PUNCT)
    seen = set()
    uniq = []
    for ch in chars:
        if ch not in seen:
            seen.add(ch)
            uniq.append(ch)
    return sorted(uniq)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def ensure_font(path=None):
    if path:
        if not os.path.isfile(path):
            sys.exit(f"ERROR: --font 不存在: {path}")
        return path
    cached = os.path.join(CACHE_DIR, "NotoSansSC-wght.ttf")
    if os.path.isfile(cached) and sha256_file(cached) == FONT_SHA256:
        return cached
    os.makedirs(CACHE_DIR, exist_ok=True)
    print(f"downloading {FONT_URL}")
    urllib.request.urlretrieve(FONT_URL, cached)
    got = sha256_file(cached)
    if got != FONT_SHA256:
        os.remove(cached)
        sys.exit(f"ERROR: 字体 sha256 不符（{got}），已删除；请人工核对来源")
    return cached


def render_glyphs(font_path, chars):
    from PIL import Image, ImageDraw, ImageFont

    probe = "".join(chars)
    # 两遍标定：REF 字号估渲染字号 → 实际渲染字号下重测 ink 包络
    f0 = ImageFont.truetype(font_path, REF)
    f0.set_variation_by_axes([WGHT])
    u0 = v0 = 10 ** 9
    u1 = v1 = -(10 ** 9)
    for ch in probe:
        b = f0.getbbox(ch)
        if not b:
            continue
        u0, v0 = min(u0, b[0]), min(v0, b[1])
        u1, v1 = max(u1, b[2]), max(v1, b[3])
    size = round(REF * CELL * SS / max(u1 - u0, v1 - v0))

    font = ImageFont.truetype(font_path, size)
    font.set_variation_by_axes([WGHT])
    u0 = v0 = 10 ** 9
    u1 = v1 = -(10 ** 9)
    for ch in probe:
        b = font.getbbox(ch)
        if not b:
            continue
        u0, v0 = min(u0, b[0]), min(v0, b[1])
        u1, v1 = max(u1, b[2]), max(v1, b[3])
    target = CELL * SS
    w, h = u1 - u0, v1 - v0
    k = target / max(w, h)
    ox = round(-u0 * k + (target - w * k) / 2)
    oy = round(-v0 * k + (target - h * k) / 2)

    glyphs = {}
    img = Image.new("L", (target, target), 0)
    draw = ImageDraw.Draw(img)
    for ch in chars:
        img.paste(0, (0, 0, target, target))
        draw.text((ox, oy), ch, font=font, fill=255)
        px = img.load()
        rows = []
        for y in range(CELL):
            b0 = b1 = 0
            for x in range(CELL):
                cov = sum(px[x * SS + dx, y * SS + dy]
                          for dx in range(SS) for dy in range(SS))
                if cov >= THRESH * 255 * SS * SS:
                    if x < 8:
                        b0 |= 1 << (7 - x)
                    else:
                        b1 |= 1 << (15 - x)
            rows.extend((b0, b1))
        glyphs[ch] = rows
    return glyphs, size


def emit_header(chars, glyphs, font_sha, out_path, size):
    n = len(chars)
    lines = []
    lines.append("#pragma once")
    lines.append("/* 16×16 CJK 点阵字库（自动生成，勿手改）。")
    lines.append(" * 生成：python3 tools/make-cjk-font.py")
    lines.append(f" * 字源：Noto Sans SC（变量字重 {WGHT}）")
    lines.append(f" *   {FONT_URL}")
    lines.append(f" *   sha256 {font_sha}")
    lines.append(f" * 许可：{FONT_LICENSE}")
    lines.append(" * 覆盖：ASCII 0x20..0x7E + GB2312 一级汉字(3755) + 常用中文标点")
    lines.append(f" * 光栅化：{SS}x 超采样、{int(THRESH * 100)}% 覆盖率阈值；"
                 f"渲染字号 {size}")
    lines.append(" * 位序：行主序 16 行 × 2 字节/行，MSB = 最左列像素。")
    lines.append(" * 查表：pet_font16_glyph()（pet_text.c，二分）。 */")
    lines.append("#include <stdint.h>")
    lines.append("")
    lines.append(f"#define CJK16_COUNT {n}u")
    lines.append(f"static const uint32_t cjk16_index[CJK16_COUNT] = {{")
    for i in range(0, n, 8):
        row = ", ".join(f"0x{ord(chars[j]):04X}u" for j in range(i, min(i + 8, n)))
        lines.append("    " + row + ",")
    lines.append("};")
    lines.append("")
    lines.append("static const uint8_t cjk16_bitmap[CJK16_COUNT][32] = {")
    for ch in chars:
        g = glyphs[ch]
        hexes = ", ".join(f"0x{b:02X}" for b in g)
        lines.append(f"    {{ {hexes} }},  /* U+{ord(ch):04X} {ch} */")
    lines.append("};")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--font", help="本地 Noto Sans SC TTF（缺省按 URL 下载缓存）")
    ap.add_argument("--out", default=DEFAULT_OUT, help="输出头文件路径")
    args = ap.parse_args()

    font_path = ensure_font(args.font)
    font_sha = sha256_file(font_path)
    chars = charset()
    glyphs, size = render_glyphs(font_path, chars)

    # 自检：索引严格升序（二分前提）；每个码点恰一字形
    cps = [ord(c) for c in chars]
    assert all(a < b for a, b in zip(cps, cps[1:])), "index not sorted"
    assert len(glyphs) == len(chars)

    emit_header(chars, glyphs, font_sha, args.out, size)
    n = len(chars)
    idx_bytes = n * 4
    bmp_bytes = n * 32
    print(f"font: {font_path}")
    print(f"font sha256: {font_sha}")
    print(f"render size: {size}px (SS={SS}, weight={WGHT}, thresh={THRESH})")
    print(f"glyphs: {n} (ascii 95 + gb2312-l1 "
          f"{len(gb2312_level1())} + punct {len(PUNCT)})")
    print(f"index: {idx_bytes} bytes, bitmap: {bmp_bytes} bytes, "
          f"total: {idx_bytes + bmp_bytes} bytes")
    print(f"wrote: {args.out}")


if __name__ == "__main__":
    main()
