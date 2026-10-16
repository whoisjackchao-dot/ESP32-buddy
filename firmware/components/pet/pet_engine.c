#include "pet_engine.h"

#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pet_hal.h"
#include "pet_render.h"
#include "pet_state.h"

static const char *TAG = "pet-engine";

/* M1 提案值（待批准，见 contracts/参数冻结表.md） */
#define PET_ORIGIN_X 19
#define PET_ORIGIN_Y 56
#define PET_BACKGROUND 0x0000u /* 黑底，宠物外壳为白/灰/青，对比足够 */

/* 立绘由 ESP-IDF EMBED_FILES 嵌入（文件名 robot.look） */
extern const uint8_t robot_look_start[] asm("_binary_robot_look_start");
extern const uint8_t robot_look_end[] asm("_binary_robot_look_end");

static struct pet_state *s_state;
static pet_look_t  *s_look;
static uint8_t     *s_pixels;
static pet_surface_t s_surface;

static void fill_background(void)
{
    const uint16_t c = PET_BACKGROUND;
    for (size_t i = 0; i < (size_t)s_surface.width * s_surface.height; i++) {
        s_pixels[i * 2]     = (uint8_t)(c & 0xFF); /* RGB565 little-endian */
        s_pixels[i * 2 + 1] = (uint8_t)(c >> 8);
    }
}

void pet_engine_init(void)
{
    const pet_hal_t *hal = pet_hal_get();
    const int w = hal ? hal->panel_width : 135;
    const int h = hal ? hal->panel_height : 240;

    s_pixels = malloc((size_t)w * h * 2);
    if (s_pixels == NULL) {
        ESP_LOGE(TAG, "framebuffer 分配失败");
        return;
    }
    s_surface.pixels = s_pixels;
    s_surface.width  = w;
    s_surface.height = h;
    s_surface.stride = w * 2;

    s_state = pet_state_create();

    const size_t look_len = (size_t)(robot_look_end - robot_look_start);
    s_look = pet_look_parse(robot_look_start, look_len);
    if (s_look == NULL) {
        ESP_LOGE(TAG, "立绘解析失败（len=%u）", (unsigned)look_len);
    } else {
        ESP_LOGI(TAG, "立绘就绪（len=%u）", (unsigned)look_len);
    }
}

void pet_engine_redraw(void)
{
    const pet_hal_t *hal = pet_hal_get();
    if (s_pixels == NULL || hal == NULL || hal->present == NULL) {
        return;
    }
    fill_background();

    pet_view_t view;
    pet_state_view(s_state, &view);
    if (s_look != NULL) {
        pet_render_pet(&view, s_look, &s_surface, PET_ORIGIN_X, PET_ORIGIN_Y);
    }
    hal->present(&s_surface);
}
