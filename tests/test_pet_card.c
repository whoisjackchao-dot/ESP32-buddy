/* M3 T 类：8×8 文本 + 时长格式 + 整屏场景（宠物 + 1 张卡）。
 * 契约：contracts/M3-竖屏版式契约清单.md §5。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "pet_render.h"
#include "pet_state.h"
#include "pet_text.h"

#define SW 135
#define SH 240
#define GUARD 16
#define SENTINEL 0xF81Fu

#ifndef PET_FIXTURE
#define PET_FIXTURE "firmware/components/pet/assets/robot.look"
#endif

static uint8_t fb[SW * SH * 2 + GUARD];
static pet_surface_t surface;
static pet_look_t *g_look;

static void put_px(const pet_surface_t *s, int x, int y, uint16_t c)
{
    uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF);
    p[1] = (uint8_t)(c >> 8);
}

static uint16_t get_px(const pet_surface_t *s, int x, int y)
{
    const uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    return (uint16_t)(p[0] | (p[1] << 8));
}

static void fill(uint16_t c)
{
    for (int y = 0; y < SH; y++) {
        for (int x = 0; x < SW; x++) {
            put_px(&surface, x, y, c);
        }
    }
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

static int count_non(int x0, int y0, int x1, int y1, uint16_t skip)
{
    int n = 0;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (x < 0 || y < 0 || x >= SW || y >= SH) {
                continue;
            }
            if (get_px(&surface, x, y) != skip) {
                n++;
            }
        }
    }
    return n;
}

/* ---- 文本 ---- */

static void text_draw_paints_glyph(void)
{
    fill(SENTINEL);
    const int n = pet_text_draw(&surface, 0, 0, "A", 0, 0xFFFF, 1);
    CHECK(n == 1);
    CHECK(count_non(0, 0, 7, 7, SENTINEL) > 0);   /* 该字格内有像素 */
    CHECK(count_non(8, 0, SW - 1, 7, SENTINEL) == 0); /* 不越到下一格 */
}

static void text_draw_respects_max_chars(void)
{
    fill(SENTINEL);
    const int n = pet_text_draw(&surface, 0, 0, "AAAAAA", 2, 0xFFFF, 1);
    CHECK(n == 2);
    CHECK(count_non(0, 0, 15, 7, SENTINEL) > 0);  /* 前 2 格有内容 */
    CHECK(count_non(16, 0, SW - 1, 7, SENTINEL) == 0); /* 第 3 格起空白 */
}

static void elapsed_formats(void)
{
    char b[16];
    pet_format_elapsed(b, sizeof b, 0);
    CHECK(strcmp(b, "0S") == 0);
    pet_format_elapsed(b, sizeof b, -5);
    CHECK(strcmp(b, "0S") == 0);
    pet_format_elapsed(b, sizeof b, 45);
    CHECK(strcmp(b, "45S") == 0);
    pet_format_elapsed(b, sizeof b, 59);
    CHECK(strcmp(b, "59S") == 0);
    pet_format_elapsed(b, sizeof b, 60);
    CHECK(strcmp(b, "1M") == 0);
    pet_format_elapsed(b, sizeof b, 90);
    CHECK(strcmp(b, "1M") == 0);
    pet_format_elapsed(b, sizeof b, 3600);
    CHECK(strcmp(b, "1H") == 0);
    pet_format_elapsed(b, sizeof b, 7500);
    CHECK(strcmp(b, "2H") == 0);
}

/* ---- 场景 / 卡片 ---- */

static pet_view_t view_no_card(void)
{
    pet_view_t v;
    memset(&v, 0, sizeof v);
    v.state = PET_STATE_WORKING;
    v.frame = PET_FRAME_NORMAL;
    v.has_card = false;
    return v;
}

static pet_view_t view_with_card(const char *title, const char *project, int32_t secs)
{
    pet_view_t v = view_no_card();
    v.has_card = true;
    v.card.state = PET_STATE_WORKING;
    v.card.elapsed_s = secs;
    snprintf(v.card.title, sizeof v.card.title, "%s", title);
    snprintf(v.card.project, sizeof v.card.project, "%s", project);
    return v;
}

static void card_zone_is_empty_without_card(void)
{
    fill(SENTINEL);
    arm_guard();
    pet_view_t v = view_no_card();
    pet_render_scene(&v, g_look, &surface);

    /* 卡片区只有场景底色 */
    CHECK(count_non(PET_CARD_X, PET_CARD_Y, PET_CARD_X + PET_CARD_W - 1,
                    PET_CARD_Y + PET_CARD_H - 1, PET_SCENE_BG) == 0);
    CHECK(guard_intact());
}

static void card_zone_has_content_with_card(void)
{
    fill(SENTINEL);
    arm_guard();
    pet_view_t v = view_with_card("BUILD FIRMWARE", "ESP32-buddy", 125);
    pet_render_scene(&v, g_look, &surface);

    CHECK(count_non(PET_CARD_X, PET_CARD_Y, PET_CARD_X + PET_CARD_W - 1,
                    PET_CARD_Y + PET_CARD_H - 1, PET_SCENE_BG) > 0);
    CHECK(guard_intact());
}

static void pet_zone_has_content_either_way(void)
{
    fill(SENTINEL);
    pet_view_t v = view_no_card();
    pet_render_scene(&v, g_look, &surface);
    CHECK(count_non(PET_SCENE_PET_X, PET_SCENE_PET_Y,
                    PET_SCENE_PET_X + 96 - 1, PET_SCENE_PET_Y + 128 - 1,
                    PET_SCENE_BG) > 0);
}

static void card_long_title_does_not_overflow(void)
{
    fill(SENTINEL);
    arm_guard();
    char big[200];
    memset(big, 'W', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    pet_view_t v = view_with_card(big, "P", 99999);
    pet_render_scene(&v, g_look, &surface);
    CHECK(guard_intact());
    /* 卡片右侧之外仍是场景底色 */
    CHECK(count_non(PET_CARD_X + PET_CARD_W, 0, SW - 1, SH - 1, PET_SCENE_BG) == 0);
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : PET_FIXTURE;
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open fixture: %s\n", path);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytes = malloc((size_t)n);
    if (fread(bytes, 1, (size_t)n, f) != (size_t)n) {
        return 2;
    }
    fclose(f);
    g_look = pet_look_parse(bytes, (size_t)n);
    if (g_look == NULL) {
        fprintf(stderr, "look parse failed\n");
        return 2;
    }

    surface.pixels = fb;
    surface.width = SW;
    surface.height = SH;
    surface.stride = SW * 2;

    RUN(text_draw_paints_glyph);
    RUN(text_draw_respects_max_chars);
    RUN(elapsed_formats);
    RUN(card_zone_is_empty_without_card);
    RUN(card_zone_has_content_with_card);
    RUN(pet_zone_has_content_either_way);
    RUN(card_long_title_does_not_overflow);

    pet_look_free(g_look);
    free(bytes);
    REPORT();
}
