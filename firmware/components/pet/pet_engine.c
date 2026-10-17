#include "pet_engine.h"

#include <stdlib.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pet_hal.h"
#include "pet_pomo.h"
#include "pet_pomo_store.h"
#include "pet_render.h"
#include "pet_state.h"
#include "pet_voice.h"

static const char *TAG = "pet-engine";

/* 提案值（待批准，见 contracts/参数冻结表.md）：背景由 pet_render_scene 统一处理 */

/* 立绘由 ESP-IDF EMBED_FILES 嵌入（文件名 robot.look） */
extern const uint8_t robot_look_start[] asm("_binary_robot_look_start");
extern const uint8_t robot_look_end[] asm("_binary_robot_look_end");

static struct pet_state *s_state;
static pet_look_t  *s_look;
static uint8_t     *s_pixels;
static pet_surface_t s_surface;

/* M7 增量 2：番茄钟（模式切换由 M6 调用 pet_engine_set_mode） */
static pet_pomo_t s_pomo;
static pet_mode_t s_mode = PET_MODE_DUTY;

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

    pet_pomo_init(&s_pomo, 0);
    pet_pomo_nvs_load(&s_pomo);

#ifndef PET_ENGINE_AUTOSTART_POMO
#define PET_ENGINE_AUTOSTART_POMO 0
#endif
#if PET_ENGINE_AUTOSTART_POMO
    s_mode = PET_MODE_POMO;
#endif

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
    if (s_mode == PET_MODE_POMO) {
        pet_pomo_view_t pv;
        pet_pomo_view_get(&s_pomo, &pv);
        pet_render_pomo(&s_surface, &pv);
    } else {
        pet_view_t view;
        pet_state_view(s_state, &view);
        pet_render_scene(&view, s_look, &s_surface); /* 整屏：宠物 + 任务卡 */
    }
    hal->present(&s_surface);

    /* 取走一次性播报（M5 之前 play_sound 是空实现） */
    const pet_sound_t sound = pet_state_take_sound(s_state);
    if (pet_voice_should_play(sound, pet_voice_muted()) && hal->play_sound != NULL) {
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
    uint32_t frames = 0;
    uint32_t last_log_ms = 0;
    for (;;) {
        const pet_hal_t *hal = pet_hal_get();
        if (hal != NULL && hal->now_ms != NULL) {
            const uint32_t now = hal->now_ms();
            frames++;
            pet_state_tick(s_state, now);
            if (s_mode == PET_MODE_POMO) {
                pet_pomo_tick(&s_pomo, now);
                if (pet_pomo_take_alarm(&s_pomo)) {
                    if (hal->play_sound != NULL) {
                        hal->play_sound(PET_SOUND_DONE); /* 到点响铃（复用 done 音） */
                    }
                    pet_pomo_nvs_save(&s_pomo); /* 统计变更即落盘 */
                }
            }
            if (now - last_log_ms >= 10000u) {
                const uint32_t dt = now - last_log_ms;
                const uint32_t fps = (dt > 0u) ? (frames * 1000u / dt) : 0u;
                pet_pomo_view_t pv;
                pet_pomo_view_get(&s_pomo, &pv);
                ESP_LOGI(TAG, "st: fps=%lu muted=%d phase=%d done=%lu focus_s=%lu",
                         (unsigned long)fps, (int)pet_voice_muted(), (int)pv.phase,
                         (unsigned long)pv.done_count, (unsigned long)pv.focus_seconds);
                frames = 0;
                last_log_ms = now;
            }
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

struct pet_state *pet_engine_state(void)
{
    return s_state;
}

void pet_engine_set_mode(pet_mode_t mode)
{
    if (mode == s_mode) {
        return;
    }
    if (mode == PET_MODE_POMO) {
        pet_pomo_nvs_load(&s_pomo);
    } else {
        pet_pomo_nvs_save(&s_pomo);
    }
    s_mode = mode;
}

pet_mode_t pet_engine_mode(void)
{
    return s_mode;
}

void pet_engine_pomo_command(pet_pomo_cmd_t cmd)
{
    pet_pomo_command(&s_pomo, cmd, pet_hal_now_ms());
}

void pet_engine_request_back_window(void)
{
    static const char line[] = "{\"v\":1,\"cmd\":\"back_window\"}\n";
    (void)usb_serial_jtag_write_bytes(line, sizeof line - 1, pdMS_TO_TICKS(50));
}
