/* pet_render：立绘解析 + 绘制 + 状态标记。
 * 契约：contracts/M1-渲染分层契约冻结.md（接口/格式）、
 *       contracts/M2-状态机与动画契约清单.md §8（gray / mirror / 头顶标记）、
 *       contracts/M7-番茄钟契约清单.md §7.1/§7.2（进度环 / 番茄钟整屏）。 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pet_render.h"
#include "pet_text.h"

/* 立绘包格式（与上游 look.rs 规格一致，净室实现） */
#define LOOK_MAGIC      "LOOK"
#define LOOK_W          48
#define LOOK_H          64
#define LOOK_FRAMES     4
#define LOOK_COLORS     16
#define LOOK_FRAME_BYTES (LOOK_W * LOOK_H / 2)
#define LOOK_HEADER     (8 + LOOK_COLORS * 2)
#define LOOK_BYTES      (LOOK_HEADER + LOOK_FRAMES * LOOK_FRAME_BYTES)

/* 立绘在屏幕上按 2× 绘制（对应上游 look::SCALE） */
#define PET_LOOK_SCALE  2

/* 头顶标记（契约 §8.2，提案值） */
#ifndef PET_MARK_OX
#define PET_MARK_OX 56
#endif
#ifndef PET_MARK_OY
#define PET_MARK_OY (-18)
#endif
#define MARK_COLOR_WORKING 0xFFFFu /* 白 */
#define MARK_COLOR_INPUT   0xFD20u /* 橙 */

struct pet_look {
    uint16_t palette[LOOK_COLORS];
    uint8_t  frames[LOOK_FRAMES][LOOK_FRAME_BYTES];
};

/* ---- 解析 ---- */

pet_look_t *pet_look_parse(const uint8_t *bytes, size_t len)
{
    if (bytes == NULL || len != LOOK_BYTES)
        return NULL;
    if (memcmp(bytes, LOOK_MAGIC, 4) != 0)
        return NULL;
    if (bytes[4] != LOOK_W || bytes[5] != LOOK_H || bytes[6] != LOOK_FRAMES)
        return NULL;

    pet_look_t *look = malloc(sizeof *look);
    if (look == NULL)
        return NULL;

    for (int i = 0; i < LOOK_COLORS; i++)
        look->palette[i] = (uint16_t)(bytes[8 + 2 * i] | (bytes[9 + 2 * i] << 8));

    memcpy(look->frames, bytes + LOOK_HEADER, LOOK_FRAMES * LOOK_FRAME_BYTES);
    return look;
}

void pet_look_free(pet_look_t *look)
{
    free(look);
}

/* ---- 绘制工具 ---- */

static void put_px(pet_surface_t *s, int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= s->width || y >= s->height)
        return;
    uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF); /* RGB565 little-endian */
    p[1] = (uint8_t)(c >> 8);
}

static void fill_block(pet_surface_t *s, int x, int y, uint16_t c)
{
    for (int dy = 0; dy < PET_LOOK_SCALE; dy++)
        for (int dx = 0; dx < PET_LOOK_SCALE; dx++)
            put_px(s, x + dx, y + dy, c);
}

/* 灰化：按亮度权重换算后写回三通道相等（上游 look::gray 同法） */
static uint16_t gray565(uint16_t c)
{
    const uint32_t r8 = ((c >> 11) & 0x1F) * 255 / 31;
    const uint32_t g8 = ((c >> 5) & 0x3F) * 255 / 63;
    const uint32_t b8 = (c & 0x1F) * 255 / 31;
    const uint32_t level = (r8 * 77 + g8 * 150 + b8 * 29) >> 8; /* 0..255 */
    const uint32_t g6 = (level * 63 + 127) / 255;
    const uint32_t r5 = g6 >> 1;
    const uint32_t b5 = g6 >> 1;
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

/* 头顶标记字符：每行一个位掩码，bit0 = 最左列 */
static const uint8_t GLYPH_GT[7]    = {0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10};
static const uint8_t GLYPH_BANG[7]  = {0x02, 0x02, 0x02, 0x02, 0x02, 0x00, 0x02};
static const uint8_t GLYPH_QUEST[7] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
#define GLYPH_ROWS 7

static void draw_glyph(pet_surface_t *s, int x, int y, const uint8_t *rows,
                       int width, uint16_t color)
{
    for (int r = 0; r < GLYPH_ROWS; r++) {
        for (int c = 0; c < width; c++) {
            if ((rows[r] & (uint8_t)(1u << c)) != 0) {
                fill_block(s, x + c * PET_LOOK_SCALE, y + r * PET_LOOK_SCALE, color);
            }
        }
    }
}

/* ---- 主绘制 ---- */

void pet_render_pet(const pet_view_t *v, const pet_look_t *look,
                    pet_surface_t *surface, int x, int y)
{
    if (look == NULL || surface == NULL || surface->pixels == NULL)
        return;
    if (surface->width <= 0 || surface->height <= 0)
        return;

    int frame = (v != NULL) ? (int)v->frame : (int)PET_FRAME_NORMAL;
    if (frame < 0 || frame >= LOOK_FRAMES)
        frame = 0;
    const bool gray = (v != NULL) && v->gray;
    const bool mirror = (v != NULL) && v->mirror;
    const uint8_t *pixels = look->frames[frame];

    for (int ly = 0; ly < LOOK_H; ly++) {
        for (int lx = 0; lx < LOOK_W; lx++) {
            const int sx = mirror ? (LOOK_W - 1 - lx) : lx; /* 翻转：输出列取镜像输入列 */
            const int at = ly * LOOK_W + sx;
            const uint8_t packed = pixels[at / 2];
            const uint8_t index = (at % 2 == 0) ? (uint8_t)(packed & 0x0F)
                                                : (uint8_t)(packed >> 4);
            if (index == 0)
                continue; /* 调色板 0 = 透明 */

            uint16_t color = look->palette[index];
            if (gray)
                color = gray565(color);
            fill_block(surface, x + lx * PET_LOOK_SCALE, y + ly * PET_LOOK_SCALE,
                       color);
        }
    }

    /* 状态标记：画在头顶上方 */
    if (v != NULL) {
        const int mx = x + PET_MARK_OX;
        const int my = y + PET_MARK_OY;
        switch (v->state) {
        case PET_STATE_WORKING:
            draw_glyph(surface, mx, my, GLYPH_GT, 5, MARK_COLOR_WORKING);
            break;
        case PET_STATE_INPUT_REQUIRED:
            draw_glyph(surface, mx, my, GLYPH_BANG, 3, MARK_COLOR_INPUT);
            draw_glyph(surface, mx + 4 * PET_LOOK_SCALE, my, GLYPH_QUEST, 5,
                       MARK_COLOR_INPUT);
            break;
        default:
            break;
        }
    }
}

/* ---- M3：整屏场景 ---- */

#define CARD_BG      0x0841u
#define CARD_BORDER  0x4208u
#define CARD_TITLE   0xFFFFu
#define CARD_PROJECT 0x8410u
#define CARD_ELAPSED 0xFFFFu
#ifndef CARD_TEXT_MAX
#define CARD_TEXT_MAX 14
#endif
#define CARD_ACCENT_W 3

static void fill_rect(pet_surface_t *s, int x0, int y0, int x1, int y1, uint16_t c)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            put_px(s, x, y, c);
        }
    }
}

static uint16_t state_accent(pet_state_t st)
{
    switch (st) {
    case PET_STATE_WORKING:
    case PET_STATE_INPUT_REQUIRED:
        return 0xFD20u; /* 橙 */
    case PET_STATE_DONE:
        return 0x07E0u; /* 绿 */
    case PET_STATE_FAILED:
        return 0xF800u; /* 红 */
    default:
        return 0x8410u; /* 灰 */
    }
}

static void draw_card(pet_surface_t *s, const pet_card_t *card)
{
    const int x0 = PET_CARD_X;
    const int y0 = PET_CARD_Y;
    const int x1 = PET_CARD_X + PET_CARD_W - 1;
    const int y1 = PET_CARD_Y + PET_CARD_H - 1;

    fill_rect(s, x0, y0, x1, y1, CARD_BG);
    /* 边框 */
    fill_rect(s, x0, y0, x1, y0, CARD_BORDER);
    fill_rect(s, x0, y1, x1, y1, CARD_BORDER);
    fill_rect(s, x0, y0, x0, y1, CARD_BORDER);
    fill_rect(s, x1, y0, x1, y1, CARD_BORDER);
    /* 左侧状态色条 */
    fill_rect(s, x0 + 1, y0 + 1, x0 + CARD_ACCENT_W, y1 - 1, state_accent(card->state));

    const int tx = x0 + CARD_ACCENT_W + 4;
    const char *title = (card->title[0] != '\0') ? card->title : "[NO TASK]";
    (void)pet_text_draw(s, tx, y0 + 6, title, CARD_TEXT_MAX, CARD_TITLE, 1);
    if (card->project[0] != '\0') {
        (void)pet_text_draw(s, tx, y0 + 22, card->project, CARD_TEXT_MAX, CARD_PROJECT, 1);
    }
    char elapsed[16];
    pet_format_elapsed(elapsed, sizeof elapsed, card->elapsed_s);
    (void)pet_text_draw(s, tx, y0 + 38, elapsed, CARD_TEXT_MAX, CARD_ELAPSED, 1);
}

void pet_render_scene(const pet_view_t *v, const pet_look_t *look,
                      pet_surface_t *surface)
{
    if (surface == NULL || surface->pixels == NULL) {
        return;
    }
    /* 场景底色 */
    for (int y = 0; y < surface->height; y++) {
        for (int x = 0; x < surface->width; x++) {
            put_px(surface, x, y, PET_SCENE_BG);
        }
    }
    /* 宠物（固定在宠物区） */
    pet_render_pet(v, look, surface, PET_SCENE_PET_X, PET_SCENE_PET_Y);
    /* 任务卡（M3 只 1 张） */
    if (v != NULL && v->has_card) {
        draw_card(surface, &v->card);
    }
}

/* ---- M7 增量 2：番茄钟视图（契约 §7.1/§7.2）---- */

/* 0°=12 点、顺时针（屏幕 y 向下，atan2(dx, -dy) 即给出顺时针角）。
 * 自定义 PI：-std=c11 严格模式下 M_PI 不保证可见。 */
#define POMO_PI 3.14159265358979323846

#define POMO_RING_CX 67
#define POMO_RING_CY 72
#define POMO_RING_R  48
#define POMO_RING_T  6
#define POMO_C_FOCUS 0xFD20u /* 橙 */
#define POMO_C_BREAK 0x07E0u /* 绿 */
#define POMO_C_OTHER 0x8410u /* 灰 */
#define POMO_C_TRACK 0x4208u
#define POMO_C_TEXT  0xFFFFu /* 白 */

void pet_render_ring(pet_surface_t *s, int cx, int cy, int r, int t,
                     int progress_permille, uint16_t color, uint16_t track)
{
    if (s == NULL || s->pixels == NULL || r <= 0 || t < 0) {
        return;
    }
    if (progress_permille < 0) {
        progress_permille = 0;
    } else if (progress_permille > 1000) {
        progress_permille = 1000;
    }

    const int inner = (t < r) ? (r - t) : 0; /* 洞半径；t >= r 时退化为实心圆 */
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            const int d2 = dx * dx + dy * dy;
            if (d2 > r * r || d2 < inner * inner) {
                continue; /* 环外 / 洞内 */
            }
            double ang = atan2((double)dx, (double)(-dy)) * 180.0 / POMO_PI;
            if (ang < 0) {
                ang += 360.0;
            }
            const uint16_t c = (ang * 1000.0 <= (double)progress_permille * 360.0)
                ? color
                : track;
            put_px(s, cx + dx, cy + dy, c);
        }
    }
}

static uint16_t pomo_phase_color(pet_pomo_phase_t phase)
{
    switch (phase) {
    case PET_POMO_FOCUS:
    case PET_POMO_FOCUS_PAUSED:
        return POMO_C_FOCUS;
    case PET_POMO_BREAK:
    case PET_POMO_BREAK_PAUSED:
        return POMO_C_BREAK;
    default:
        return POMO_C_OTHER;
    }
}

void pet_render_pomo(pet_surface_t *s, const pet_pomo_view_t *v)
{
    if (s == NULL || s->pixels == NULL) {
        return;
    }
    for (int y = 0; y < s->height; y++) {
        for (int x = 0; x < s->width; x++) {
            put_px(s, x, y, PET_SCENE_BG);
        }
    }
    if (v == NULL) {
        return;
    }

    /* 进度 = elapsed/total 千分比；total==0 → 0 */
    const uint32_t elapsed = (v->total_ms > 0) ? v->total_ms - v->remaining_ms : 0;
    const int permille = (v->total_ms > 0)
        ? (int)((uint32_t)elapsed * 1000u / v->total_ms)
        : 0;
    pet_render_ring(s, POMO_RING_CX, POMO_RING_CY, POMO_RING_R, POMO_RING_T,
                    permille, pomo_phase_color(v->phase), POMO_C_TRACK);

    /* 中心剩余 M:SS，水平居中于环心 */
    const uint32_t rem_s = v->remaining_ms / 1000u;
    char txt[16];
    snprintf(txt, sizeof txt, "%u:%02u", (unsigned)(rem_s / 60u),
             (unsigned)(rem_s % 60u));
    const int tx = POMO_RING_CX - (int)strlen(txt) * 8 / 2;
    (void)pet_text_draw(s, tx, POMO_RING_CY - 4, txt, (int)sizeof txt,
                        POMO_C_TEXT, 1);
}
