#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pet_render.h"

/* 画 8×8 点阵文本；最多 max_chars 个字符（<=0 表示不限，遇 NUL 停止）。
 * 返回实际画出的字符数。越界裁剪。 */
int pet_text_draw(pet_surface_t *surface, int x, int y, const char *text,
                  int max_chars, uint16_t color, int scale);

/* 秒 → 精简时长文本："45S" / "12M" / "3H"；<=0 → "0S" */
void pet_format_elapsed(char *out, size_t cap, int32_t seconds);
