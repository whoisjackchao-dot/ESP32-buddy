/* M15 T 类：16×16 中文点阵（cjk16.h + pet_text_draw16）的 RED→GREEN。
 * 断言体现业务规则：字库覆盖与有序（二分前提）、UTF-8 解码格数语义
 * （4 字节跳过 / 非法画 '?'）、按格绘制与 scale、折行与 "…" 截断。
 * 黄金期望由测试自己数位图位（popcount）或独立画期望串得出，
 * 不复用待测实现。契约：evidence/2026-10-25-中文点阵字库-T.md。 */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "pet_text.h"

/* 直读生成表：有序性 + 查表内容一致（测试侧独立于 pet_text.c 的副本） */
#include "firmware/components/pet/assets/cjk16.h"

#define SW 80
#define SH 80
#define GUARD 16
#define INK 0x07E0u   /* 绿：与哨兵底色区分 */
#define SENTINEL 0xF81Fu

static uint8_t       fb[SW * SH * 2 + GUARD];
static uint8_t       exp_fb[SW * SH * 2];
static pet_surface_t surface;
static pet_surface_t expect;

static void put_px(pet_surface_t *s, int x, int y, uint16_t c)
{
    uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF);
    p[1] = (uint8_t)(c >> 8);
}

static uint16_t get_px(pet_surface_t *s, int x, int y)
{
    const uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void fill(pet_surface_t *s, uint16_t c)
{
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            put_px(s, x, y, c);
        }
    }
}

static int ink_in(int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x >= 0 && y >= 0 && x < SW && y < SH && get_px(&surface, x, y) == INK) {
                n++;
            }
        }
    }
    return n;
}

static int glyph_popcount(const uint8_t *g)
{
    int n = 0;
    for (int i = 0; i < 32; i++) {
        for (int b = 0; b < 8; b++) {
            n += (g[i] >> b) & 1;
        }
    }
    return n;
}

static void arm_guard(void)
{
    memset(fb + SW * SH * 2, 0xA5, GUARD);
}

static int guard_intact(void)
{
    for (int i = 0; i < GUARD; i++) {
        if (fb[SW * SH * 2 + i] != 0xA5) {
            return 0;
        }
    }
    return 1;
}

/* 两个画布的整行带是否一致（折行断言用） */
static int band_equals_expected(int y0, int y1)
{
    return memcmp(fb + (size_t)y0 * surface.stride,
                  exp_fb + (size_t)y0 * expect.stride,
                  (size_t)(y1 - y0 + 1) * surface.stride) == 0;
}

/* ---- 字库查表 ---- */

static void font_covers_ascii(void)
{
    CHECK(pet_font16_glyph('A') != NULL);
    CHECK(pet_font16_glyph('?') != NULL);
    CHECK(pet_font16_glyph('0') != NULL);
    CHECK(pet_font16_glyph('~') != NULL);
    CHECK(pet_font16_glyph(' ') != NULL); /* 空格：占格、全零位图 */
    CHECK(pet_font16_glyph(' ') != pet_font16_glyph('A'));
    for (int i = 0; i < 32; i++) {
        CHECK(pet_font16_glyph(' ')[i] == 0);
    }
}

static void font_covers_cjk_and_punct(void)
{
    const uint32_t cps[] = {
        0x4E2D, 0x4E00, 0x7684, 0x9910, 0x597D, 0x5B57, /* 中 一 的 餐 好 字 */
        0xFF0C, 0x3002, 0xFF01, 0xFF1F, 0xFF1A, 0xFF1B, /* ，。！？：； */
        0x3001, 0xFF08, 0xFF09, 0x300A, 0x300B, 0x2026, /* 、（）《》… */
    };
    for (size_t i = 0; i < sizeof cps / sizeof cps[0]; i++) {
        CHECK(pet_font16_glyph(cps[i]) != NULL);
    }
    CHECK(memcmp(pet_font16_glyph(0x4E2D), pet_font16_glyph(0x4E00), 32) != 0);
}

static void font_missing_returns_null(void)
{
    CHECK(pet_font16_glyph(0x3042) == NULL);    /* 平假名：GB2312 外 */
    CHECK(pet_font16_glyph(0xE000) == NULL);    /* 私用区 */
    CHECK(pet_font16_glyph(0x1F600) == NULL);   /* BMP 外 */
    CHECK(pet_font16_glyph(0) == NULL);
}

static void font_table_sorted_and_complete(void)
{
    CHECK(CJK16_COUNT == 3862u); /* 95 ASCII + 3755 GB2312 一级 + 12 标点 */
    for (uint32_t i = 0; i + 1 < CJK16_COUNT; i++) {
        CHECK(cjk16_index[i] < cjk16_index[i + 1]); /* 二分前提：严格升序 */
        if (cjk16_index[i] >= cjk16_index[i + 1]) {
            return;
        }
    }
    for (uint32_t i = 0; i < CJK16_COUNT; i++) {
        const uint8_t *g = pet_font16_glyph(cjk16_index[i]);
        if (g == NULL || memcmp(g, cjk16_bitmap[i], 32) != 0) {
            CHECK(g != NULL && memcmp(g, cjk16_bitmap[i], 32) == 0);
            return;
        }
    }
    CHECK(1);
}

/* ---- pet_text_draw16：UTF-8 解码与按格绘制 ---- */

static void draw16_counts_cells_and_pixels(void)
{
    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16(&surface, 0, 0, "Hi", -1, INK, 1) == 2);
    CHECK(ink_in(0, 0, 15, 15) > 0);
    CHECK(ink_in(16, 0, 31, 15) > 0);
    CHECK(ink_in(32, 0, SW - 1, 15) == 0); /* 2 格之外无墨 */

    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16(&surface, 0, 0, "中", -1, INK, 1) == 1);
    CHECK(ink_in(0, 0, 15, 15) == glyph_popcount(pet_font16_glyph(0x4E2D)));
    CHECK(ink_in(16, 0, SW - 1, 15) == 0);

    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16(&surface, 0, 0, "A中b", -1, INK, 1) == 3);
    CHECK(ink_in(32, 0, 47, 15) > 0);
}

static void draw16_unknown_and_invalid_draw_question(void)
{
    const int want = glyph_popcount(pet_font16_glyph('?'));

    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16(&surface, 0, 0, "\xE3\x81\x82", -1, INK, 1) == 1); /* あ */
    CHECK(ink_in(0, 0, 15, 15) == want);

    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16(&surface, 0, 0, "\xFF", -1, INK, 1) == 1); /* 非法前导 */
    CHECK(ink_in(0, 0, 15, 15) == want);

    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16(&surface, 0, 0, "a\xC3", -1, INK, 1) == 2); /* 缺续字节 → '?' */
    CHECK(ink_in(16, 0, 31, 15) == want);
}

static void draw16_four_byte_sequence_skips_cell(void)
{
    fill(&expect, SENTINEL);
    pet_text_draw16(&expect, 0, 0, "ab", -1, INK, 1);

    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16(&surface, 0, 0, "a\xF0\x9F\x98\x80" "b", -1, INK, 1) == 2);
    CHECK(band_equals_expected(0, 15)); /* 与 "ab" 逐像素一致（emoji 未占格） */
}

static void draw16_scale2_quadruples_ink(void)
{
    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16(&surface, 0, 0, "中", -1, INK, 1) == 1);
    const int n1 = ink_in(0, 0, 15, 15);

    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16(&surface, 0, 0, "中", -1, INK, 2) == 1);
    CHECK(ink_in(0, 0, 31, 31) == 4 * n1);
    CHECK(ink_in(32, 0, SW - 1, 31) == 0);
    CHECK(ink_in(0, 32, SW - 1, SH - 1) == 0);
}

static void draw16_clips_out_of_bounds(void)
{
    arm_guard();
    fill(&surface, SENTINEL);
    pet_text_draw16(&surface, SW - 8, SH - 8, "中汉", -1, INK, 2);
    CHECK(guard_intact());

    pet_text_draw16(&surface, -10, -10, "中", -1, INK, 1);
    CHECK(guard_intact());
    CHECK(ink_in(0, 0, SW - 1, SH - 1) > 0); /* 部分像素仍在画布内 */

    CHECK(pet_text_draw16(NULL, 0, 0, "中", -1, INK, 1) == 0);
    CHECK(pet_text_draw16(&surface, 0, 0, NULL, -1, INK, 1) == 0);
    CHECK(pet_text_draw16(&surface, 0, 0, "中", -1, INK, 0) == 0);
}

static void draw16_max_cells_cuts(void)
{
    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16(&surface, 0, 0, "AB中D", 3, INK, 1) == 3);
    CHECK(ink_in(48, 0, SW - 1, 15) == 0); /* 第 4 格未画 */
}

/* ---- pet_text_draw16_wrap：按格折行 + "…" 截断 ---- */

/* 30 个 GB2312 一级汉字（每字 3 字节 UTF-8），切子串按 3 字节步进 */
#define CJK30 "一二三四五六七八九十百千万亿元角分大小上下左右中人口手日月水"
#define C3(s) (&(CJK30)[3 * (s)])

/* CJK30 的前 n 字（NUL 结尾）；n ≤ 30 */
static const char *cjk_n(char *buf, int n)
{
    memcpy(buf, CJK30, (size_t)n * 3);
    buf[n * 3] = '\0';
    return buf;
}

static void wrap_breaks_at_cell_limit(void)
{
    char ten[3 * 10 + 1];
    cjk_n(ten, 10);

    fill(&expect, SENTINEL);
    pet_text_draw16(&expect, 0, 0, C3(0), 7, INK, 1);
    pet_text_draw16(&expect, 0, 16, C3(7), 3, INK, 1);

    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16_wrap(&surface, 0, 0, ten, 7, 4, 16, INK, 1) == 2);
    CHECK(band_equals_expected(0, 15));
    CHECK(band_equals_expected(16, 31));
    CHECK(ink_in(0, 32, SW - 1, SH - 1) == 0);
}

static void wrap_truncates_with_ellipsis(void)
{
    char want[3 * 7 + 4];
    memcpy(want, C3(21), 3 * 6);
    memcpy(want + 18, "\xE2\x80\xA6", 4); /* 6 字 + "…" */

    fill(&expect, SENTINEL);
    pet_text_draw16(&expect, 0, 48, want, 7, INK, 1);

    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16_wrap(&surface, 0, 0, CJK30, 7, 4, 16, INK, 1) == 4);
    CHECK(band_equals_expected(48, 63)); /* 末行 = 21..26 字 + "…" */
    CHECK(ink_in(0, 64, SW - 1, SH - 1) == 0);
}

static void wrap_exact_fit_has_no_ellipsis(void)
{
    char buf[3 * 28 + 1];
    cjk_n(buf, 28);

    fill(&expect, SENTINEL);
    pet_text_draw16(&expect, 0, 48, C3(21), 7, INK, 1); /* 28 字：末行 7 字无 … */

    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16_wrap(&surface, 0, 0, buf, 7, 4, 16, INK, 1) == 4);
    CHECK(band_equals_expected(48, 63));
    CHECK(ink_in(112, 48, SW - 1, 63) == 0); /* 第 7 格（…的位置）无墨 */
}

static void wrap_empty_and_overload(void)
{
    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16_wrap(&surface, 0, 0, "", 7, 4, 16, INK, 1) == 0);
    CHECK(ink_in(0, 0, SW - 1, SH - 1) == 0);

    /* 容量 1×1、文本 2 格 → 只画 "…" */
    fill(&surface, SENTINEL);
    CHECK(pet_text_draw16_wrap(&surface, 0, 0, CJK30, 1, 1, 16, INK, 1) == 1);
    CHECK(ink_in(0, 0, 15, 15) == glyph_popcount(pet_font16_glyph(0x2026)));

    CHECK(pet_text_draw16_wrap(NULL, 0, 0, CJK30, 7, 4, 16, INK, 1) == 0);
    CHECK(pet_text_draw16_wrap(&surface, 0, 0, CJK30, 0, 4, 16, INK, 1) == 0);
}

static void wrap_mixed_ascii_cjk(void)
{
    fill(&expect, SENTINEL);
    pet_text_draw16(&expect, 0, 32, "的", 1, INK, 1);

    fill(&surface, SENTINEL);
    /* 4 格/行："REC " / "3s 好" / "的"，共 3 行 */
    CHECK(pet_text_draw16_wrap(&surface, 0, 0, "REC 3s 好的", 4, 4, 16, INK, 1) == 3);
    CHECK(band_equals_expected(32, 47));
}

int main(void)
{
    surface.pixels = fb;
    surface.width = SW;
    surface.height = SH;
    surface.stride = SW * 2;
    expect.pixels = exp_fb;
    expect.width = SW;
    expect.height = SH;
    expect.stride = SW * 2;

    RUN(font_covers_ascii);
    RUN(font_covers_cjk_and_punct);
    RUN(font_missing_returns_null);
    RUN(font_table_sorted_and_complete);
    RUN(draw16_counts_cells_and_pixels);
    RUN(draw16_unknown_and_invalid_draw_question);
    RUN(draw16_four_byte_sequence_skips_cell);
    RUN(draw16_scale2_quadruples_ink);
    RUN(draw16_clips_out_of_bounds);
    RUN(draw16_max_cells_cuts);
    RUN(wrap_breaks_at_cell_limit);
    RUN(wrap_truncates_with_ellipsis);
    RUN(wrap_exact_fit_has_no_ellipsis);
    RUN(wrap_empty_and_overload);
    RUN(wrap_mixed_ascii_cjk);

    REPORT();
}
