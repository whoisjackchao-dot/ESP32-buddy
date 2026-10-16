/* M3：8×8 点阵文本绘制 + 时长格式化。
 * 字体 font8x8_basic.h：公有领域（Daniel Hepper / Marcel Sondaar / IBM VGA），
 * 来源已登记于 NOTICE.md。bit0 = 最左列。 */
#include "pet_text.h"

#include <stdio.h>

#include "font8x8_basic.h"

#define GLYPH_W 8
#define GLYPH_H 8

static void put_px(pet_surface_t *s, int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= s->width || y >= s->height) {
        return;
    }
    uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF); /* RGB565 little-endian */
    p[1] = (uint8_t)(c >> 8);
}

int pet_text_draw(pet_surface_t *surface, int x, int y, const char *text,
                  int max_chars, uint16_t color, int scale)
{
    if (surface == NULL || surface->pixels == NULL || text == NULL || scale <= 0) {
        return 0;
    }
    int drawn = 0;
    for (const char *p = text; *p != '\0'; p++) {
        if (max_chars > 0 && drawn >= max_chars) {
            break;
        }
        const unsigned char ch = (unsigned char)*p;
        if (ch < 128u) {
            const uint8_t *rows = font8x8_basic[ch];
            for (int r = 0; r < GLYPH_H; r++) {
                for (int c = 0; c < GLYPH_W; c++) {
                    if ((rows[r] & (uint8_t)(1u << c)) == 0) {
                        continue;
                    }
                    const int px = x + (drawn * GLYPH_W + c) * scale;
                    const int py = y + r * scale;
                    for (int dy = 0; dy < scale; dy++) {
                        for (int dx = 0; dx < scale; dx++) {
                            put_px(surface, px + dx, py + dy, color);
                        }
                    }
                }
            }
        }
        drawn++;
    }
    return drawn;
}

void pet_format_elapsed(char *out, size_t cap, int32_t seconds)
{
    if (out == NULL || cap == 0) {
        return;
    }
    if (seconds <= 0) {
        snprintf(out, cap, "0S");
    } else if (seconds >= 3600) {
        snprintf(out, cap, "%ldH", (long)(seconds / 3600));
    } else if (seconds >= 60) {
        snprintf(out, cap, "%ldM", (long)(seconds / 60));
    } else {
        snprintf(out, cap, "%ldS", (long)seconds);
    }
}
