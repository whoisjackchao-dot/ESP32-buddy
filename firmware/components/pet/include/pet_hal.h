#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pet_types.h"
#include "pet_render.h"

/* 唯一的硬件层。M5StickS3 实现在 pet_hal_sticks3.c。 */
typedef struct {
    uint32_t (*now_ms)(void);
    void     (*present)(const pet_surface_t *surface);
    void     (*set_backlight)(bool on);
    void     (*play_sound)(pet_sound_t sound);
    bool     (*audio_busy)(void);
    int       panel_width;
    int       panel_height;
} pet_hal_t;

void              pet_hal_install(const pet_hal_t *hal);
uint32_t          pet_hal_now_ms(void);
const pet_hal_t  *pet_hal_get(void);

#ifdef PET_TEST_CLOCK
void pet_hal_test_set_offset_ms(int32_t ms); /* F8 测试构建专用 */
#endif
