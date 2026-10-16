#include "pet_engine.h"

#include <stdlib.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pet_hal.h"
#include "pet_render.h"
#include "pet_state.h"

static const char *TAG = "pet-engine";

/* 提案值（待批准，见 contracts/参数冻结表.md）：背景由 pet_render_scene 统一处理 */

/* 立绘由 ESP-IDF EMBED_FILES 嵌入（文件名 robot.look） */
extern const uint8_t robot_look_start[] asm("_binary_robot_look_start");
extern const uint8_t robot_look_end[] asm("_binary_robot_look_end");

static struct pet_state *s_state;
static pet_look_t  *s_look;
static uint8_t     *s_pixels;
static pet_surface_t s_surface;

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
    pet_view_t view;
    pet_state_view(s_state, &view);
    pet_render_scene(&view, s_look, &s_surface); /* 整屏：宠物 + 任务卡 */
    hal->present(&s_surface);

    /* 取走一次性播报（M5 之前 play_sound 是空实现） */
    const pet_sound_t sound = pet_state_take_sound(s_state);
    if (sound != PET_SOUND_NONE && hal->play_sound != NULL) {
        hal->play_sound(sound);
    }
}

/* 动画节拍：周期 = 值班帧间隔（契约 M2 §9.2，提案值） */
#ifndef PET_FRAME_MS_DUTY
#define PET_FRAME_MS_DUTY 125u
#endif

static void pet_task(void *arg)
{
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(PET_FRAME_MS_DUTY);
    for (;;) {
        const pet_hal_t *hal = pet_hal_get();
        if (hal != NULL && hal->now_ms != NULL) {
            pet_state_tick(s_state, hal->now_ms());
        }
        pet_engine_redraw();
        vTaskDelay(period);
    }
}

void pet_engine_start(void)
{
    if (xTaskCreate(pet_task, "pet", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "动画任务创建失败");
    }
}
