#include "pet_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pet_hal.h"
#include "pet_keymap.h"
#include "pet_leisure.h"
#include "pet_menu.h"
#include "pm1.h"
#include "pet_net.h"
#include "pet_ptt.h"
#include "pet_pomo.h"
#include "pet_pomo_store.h"
#include "pet_render.h"
#include "pet_state.h"
#include "pet_text.h"
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

/* PTT 语音浮层（值班/番茄钟画面下方卡区；菜单画面不画）。
 * M15：16×16 中文点阵重排——转写画在宠物下方卡区，不再盖顶部状态。
 * 版面：清屏区 x 4..130、y 156..233（卡区下缘，宠物 y≤151 之下不重叠）；
 * 文本原点 (6,160)，7 格/行 × 4 行（行距 18px）→ 28 格，超长末格 "…"。
 * 字号取 scale=1（16px）：123px 可用宽下 scale=2（32px）仅 3 格/行 × 4 行
 * = 12 字 < 常见转写，且 REC/ASR 标签 7 格都放不下；16px 可读性由加重
 * 字重（wght 560）保证（决策记录：evidence/2026-10-25-中文点阵字库-T.md）。 */
#ifndef PET_VOICE_RED
#define PET_VOICE_RED 0xF800u
#endif
#ifndef PET_VOICE_WHITE
#define PET_VOICE_WHITE 0xFFFFu
#endif
#define PET_VOICE_X 6
#define PET_VOICE_Y 160
#define PET_VOICE_CELLS 7
#define PET_VOICE_LINES 4
#define PET_VOICE_LINE_H 18
#define PET_VOICE_CLEAR_X 4
#define PET_VOICE_CLEAR_Y 156
#define PET_VOICE_CLEAR_W 127
#define PET_VOICE_CLEAR_H 78

static void voice_put_px(pet_surface_t *s, int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= s->width || y >= s->height) {
        return;
    }
    uint8_t *p = s->pixels + (size_t)y * s->stride + (size_t)x * 2;
    p[0] = (uint8_t)(c & 0xFF);
    p[1] = (uint8_t)(c >> 8);
}

static void voice_clear(pet_surface_t *s)
{
    for (int y = PET_VOICE_CLEAR_Y; y < PET_VOICE_CLEAR_Y + PET_VOICE_CLEAR_H; y++) {
        for (int x = PET_VOICE_CLEAR_X; x < PET_VOICE_CLEAR_X + PET_VOICE_CLEAR_W; x++) {
            voice_put_px(s, x, y, PET_SCENE_BG);
        }
    }
}

/* 待机页右上角：WiFi 接入状态（绿点+IP / 红点+OFFLINE） */
static void draw_net_badge(pet_surface_t *s)
{
    char ip[16];
    pet_net_get_ip(ip, sizeof ip);
    const int w = s->width;
    const int y = 3;
    const uint16_t dot = (ip[0] != '\0') ? 0x07E0u : 0xF800u;
    for (int dy = 0; dy < 4; dy++) {
        for (int dx = 0; dx < 4; dx++) {
            voice_put_px(s, w - 6 + dx, y + 3 + dy, dot);
        }
    }
    (void)ip; /* 第一页只显示状态点；IP 在番茄钟页显示 */
}

/* 番茄钟页右上：电池图标 + 百分比（10s 缓存，避免每帧 I2C） */
static int      s_bat_pct = -1;
static bool     s_bat_chg;
static uint32_t s_bat_next_ms;

static void battery_update(uint32_t now_ms)
{
    if (s_bat_pct >= 0 && (int32_t)(now_ms - s_bat_next_ms) < 0) {
        return;
    }
    uint16_t mv = 0;
    int pct = 0;
    bool chg = false;
    if (pm1_battery(&mv, &pct, &chg) == ESP_OK) {
        s_bat_pct = pct;
        s_bat_chg = chg;
    }
    s_bat_next_ms = now_ms + 10000u;
}

static void draw_battery_badge(pet_surface_t *s)
{
    if (s_bat_pct < 0) {
        return;
    }
    const int w = s->width;
    const int x0 = w - 22, y0 = 2, bw = 16, bh = 9;
    for (int x = x0; x < x0 + bw; x++) {
        voice_put_px(s, x, y0, 0xFFFFu);
        voice_put_px(s, x, y0 + bh - 1, 0xFFFFu);
    }
    for (int y = y0; y < y0 + bh; y++) {
        voice_put_px(s, x0, y, 0xFFFFu);
        voice_put_px(s, x0 + bw - 1, y, 0xFFFFu);
    }
    for (int y = y0 + 3; y < y0 + 6; y++) {
        voice_put_px(s, x0 + bw, y, 0xFFFFu);
    }
    const int fill = (bw - 4) * s_bat_pct / 100;
    const uint16_t col = (s_bat_chg || s_bat_pct >= 50) ? 0x07E0u
                       : (s_bat_pct >= 20) ? 0xFFE0u : 0xF800u;
    for (int x = x0 + 2; x < x0 + 2 + fill; x++) {
        for (int y = y0 + 2; y < y0 + bh - 2; y++) {
            voice_put_px(s, x, y, col);
        }
    }
    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", s_bat_pct);
    (void)pet_text_draw(s, x0 - (int)strlen(pct) * 8 - 3, y0, pct, 8, 0xFFFFu, 1);
}

static void draw_voice_overlay(pet_surface_t *s)
{
    const pet_ptt_state_t st = pet_ptt_state();
    if (st == PET_PTT_IDLE) {
        return;
    }

    voice_clear(s);

    if (st == PET_PTT_DONE) {
        if (pet_text_draw16_wrap(s, PET_VOICE_X, PET_VOICE_Y, pet_ptt_text(),
                                 PET_VOICE_CELLS, PET_VOICE_LINES,
                                 PET_VOICE_LINE_H, PET_VOICE_WHITE, 1) == 0) {
            pet_text_draw16(s, PET_VOICE_X, PET_VOICE_Y, "OK",
                            PET_VOICE_CELLS, PET_VOICE_WHITE, 1);
        }
        return;
    }

    char line[32];
    uint16_t color = PET_VOICE_RED;
    if (st == PET_PTT_RECORDING) {
        /* 聆听中 + 1..3 个点（每 500ms 递增，轻微动效） */
        const uint32_t dots = (pet_ptt_elapsed_ms() / 500u) % 3u + 1u;
        snprintf(line, sizeof line, "聆听中%.*s", (int)dots, "...");
        color = PET_VOICE_WHITE;
    } else if (st == PET_PTT_UPLOADING) {
        snprintf(line, sizeof line, "理解你的意思");
        color = PET_VOICE_WHITE;
    } else {
        snprintf(line, sizeof line, "出错了");
    }
    pet_text_draw16(s, PET_VOICE_X, PET_VOICE_Y, line,
                    PET_VOICE_CELLS, color, 1);
}


/* M7 增量 2：番茄钟（模式切换由 M6 调用 pet_engine_set_mode） */
static pet_pomo_t s_pomo;
static pet_mode_t s_mode = PET_MODE_DUTY;

/* M6 增量 3：菜单 */
static pet_menu_t s_menu;
static bool s_menu_open;

/* M8 增量 1b：休闲 + 背光 */
#ifndef PET_SKIT_SHOW_MS
#define PET_SKIT_SHOW_MS 3000u
#endif
/* M8 增量 2：运行时小时取代编译期 PET_ENGINE_HOUR24；SNTP 同步后由 pet_net 经
 * pet_engine_set_hour24 注入。默认 -1（未知）→ 与原默认行为一致：不熄屏。 */
static int s_hour24 = -1;
static pet_leisure_t s_leisure;
static int      s_last_skit = -1;
static uint32_t s_skit_ms;
static bool     s_skit_active;
static uint32_t s_skit_t_ms;
static bool     s_bl_on = true;

/* M9 §2：设备侧可观测性（rf/off/on，在 pet_engine_redraw 记录）。
 * 链路在线 ⇔ 渲染视图 state != PET_STATE_OFFLINE（失联判定在 pet_state，M2）。 */
static bool        s_rf_valid;   /* 尚无上一帧基线时不打 rf */
static pet_state_t s_rf_state;
static int         s_rf_count;
static char        s_rf_top[PET_TITLE_MAX];
static bool        s_link_seen;
static bool        s_link_online = true;

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
    pet_menu_init(&s_menu);
    pet_leisure_init(&s_leisure, 0, 20261017u);
    pet_ptt_init();

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
    if (s_menu_open) {
        pet_pomo_view_t pv;
        pet_pomo_view_get(&s_pomo, &pv);
        pet_menu_view_t mv;
        mv.muted = pet_voice_muted();
        mv.selected = s_menu.selected;
        mv.mode = (s_mode == PET_MODE_POMO) ? 1 : 0;
        mv.phase = (int)pv.phase;
        mv.done_count = pv.done_count;
        mv.focus_seconds = pv.focus_seconds;
        pet_render_menu(&s_surface, &mv);
    } else if (s_mode == PET_MODE_POMO) {
        pet_pomo_view_t pv;
        pet_pomo_view_get(&s_pomo, &pv);
        pet_net_get_ip(pv.ip, sizeof pv.ip);
        pet_render_pomo(&s_surface, &pv);
        battery_update((uint32_t)(esp_timer_get_time() / 1000));
        draw_battery_badge(&s_surface);
        draw_voice_overlay(&s_surface);
    } else {
        pet_view_t view;
        pet_state_view(s_state, &view);

        /* M9 §2：rf 仅在（状态, 卡数, 顶卡标题）较上一帧变化时、绘制前记录一次 */
        const char *rf_top = (view.card_count > 0) ? view.cards[0].title : "-";
        if (!s_rf_valid || view.state != s_rf_state ||
            view.card_count != s_rf_count ||
            strncmp(rf_top, s_rf_top, sizeof s_rf_top) != 0) {
            ESP_LOGI(TAG, "rf t=%lld st=%d n=%d top=%.8s",
                     (long long)esp_timer_get_time(), (int)view.state,
                     view.card_count, rf_top);
            s_rf_valid = true;
            s_rf_state = view.state;
            s_rf_count = view.card_count;
            strncpy(s_rf_top, rf_top, sizeof s_rf_top - 1);
            s_rf_top[sizeof s_rf_top - 1] = '\0';
        }

        /* M9 §2：链路只记边沿（off=在线→离线 / on=离线→在线）；首帧仅建基线 */
        const bool online = (view.state != PET_STATE_OFFLINE);
        if (!s_link_seen) {
            s_link_seen = true;
            s_link_online = online;
        } else if (online != s_link_online) {
            s_link_online = online;
            if (online) {
                ESP_LOGI(TAG, "on t=%lld", (long long)esp_timer_get_time());
            } else {
                ESP_LOGI(TAG, "off t=%lld", (long long)esp_timer_get_time());
            }
        }

        if (s_skit_active) {
            pet_render_skit(&s_surface, s_look, &view, s_last_skit, s_skit_t_ms);
        } else {
            pet_render_scene(&view, s_look, &s_surface); /* 整屏：宠物 + 任务卡 */
        }
        draw_net_badge(&s_surface); /* 右上角：WiFi 状态 */
        draw_voice_overlay(&s_surface);
    }
    hal->present(&s_surface);

    /* 取走一次性播报（M5 之前 play_sound 是空实现） */
    const pet_sound_t sound = pet_state_take_sound(s_state);
    if (pet_voice_should_play(sound, pet_voice_muted()) && hal->play_sound != NULL) {
        ESP_LOGI(TAG, "voice id=%d t=%lld", (int)sound, (long long)esp_timer_get_time()); /* M9 §2 G7 */
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
            } else {
                /* M8：值班模式跑休闲；hour24=-1（未知）→ 不熄屏 */
                pet_view_t lv;
                pet_state_view(s_state, &lv);
                const bool active = (lv.state != PET_STATE_IDLE); /* 含断链/完成/失败 */
                pet_leisure_tick(&s_leisure, now, active, s_hour24);

                const int skit = pet_leisure_skit(&s_leisure);
                if (skit != s_last_skit) {
                    s_last_skit = skit;
                    s_skit_ms = now;
                }
                s_skit_active = (skit >= 0) && (now - s_skit_ms < PET_SKIT_SHOW_MS);
                s_skit_t_ms = now - s_skit_ms;

                const bool want_bl = !pet_leisure_lights_out(&s_leisure);
                if (want_bl != s_bl_on) {
                    s_bl_on = want_bl;
                    if (hal->set_backlight != NULL) {
                        hal->set_backlight(want_bl);
                    }
                }
            }
            if (now - last_log_ms >= 10000u) {
                const uint32_t dt = now - last_log_ms;
                const uint32_t fps = (dt > 0u) ? (frames * 1000u / dt) : 0u;
                pet_pomo_view_t pv;
                pet_pomo_view_get(&s_pomo, &pv);
                ESP_LOGI(TAG, "st: fps=%lu muted=%d phase=%d done=%lu focus_s=%lu lei=%d skit=%d",
                         (unsigned long)fps, (int)pet_voice_muted(), (int)pv.phase,
                         (unsigned long)pv.done_count, (unsigned long)pv.focus_seconds,
                         (int)pet_leisure_phase(&s_leisure), pet_leisure_skit(&s_leisure));
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

void pet_engine_set_hour24(int hour)
{
    if (hour < -1) {
        hour = -1;
    } else if (hour > 23) {
        hour = 23;
    }
    s_hour24 = hour;
}

void pet_engine_pomo_command(pet_pomo_cmd_t cmd)
{
    pet_pomo_command(&s_pomo, cmd, pet_hal_now_ms());
}

void pet_engine_request_back_window(void)
{
    pet_engine_send_line("{\"v\":1,\"cmd\":\"back_window\"}\n");
}

void pet_engine_send_line(const char *line)
{
    if (line == NULL) {
        return;
    }
    (void)usb_serial_jtag_write_bytes(line, strlen(line), pdMS_TO_TICKS(50));
    pet_net_send_control_line(line); /* WiFi 镜像（不阻塞；主机侧 500ms 去重） */
}

void pet_engine_key(pet_key_t key, pet_key_event_t ev)
{
    if (ev == PET_KEY_EV_NONE) {
        return;
    }
    /* M8：任意按键唤醒休闲（复位空闲计时 + 回值班 → 下一帧恢复背光） */
    pet_leisure_tick(&s_leisure, pet_hal_now_ms(), true, s_hour24);

    /* PTT：KEY1 按下/松开沿直通录音模块（不进键表）；菜单打开时不录；
     * 其余键/菜单中的 PRESS/RELEASE 一律吞掉。 */
    if (ev == PET_KEY_EV_PRESS || ev == PET_KEY_EV_RELEASE) {
        if (key == PET_KEY1 && !s_menu_open) {
            pet_ptt_on_key(ev == PET_KEY_EV_PRESS);
        }
        return;
    }

    if (s_menu_open) {
        const pet_menu_cmd_t cmd = pet_menu_key(&s_menu, key, ev);
        if (cmd == PET_MENU_CMD_BACK_WINDOW) {
            s_menu_open = false;
            pet_engine_request_back_window();
            ESP_LOGI(TAG, "menu: back window");
        } else if (cmd == PET_MENU_CMD_PROVISION) {
            s_menu_open = false;
            pet_net_request_provision();
            ESP_LOGI(TAG, "menu: provisioning requested");
        } else if (cmd == PET_MENU_CMD_TOGGLE_MUTE) {
            pet_voice_set_muted(!pet_voice_muted());
        } else if (cmd == PET_MENU_CMD_CLOSE) {
            s_menu_open = false;
        }
        return;
    }

    switch (pet_keymap_action(s_mode, key, ev)) {
    case PET_ACT_POMO_SHORT:
        pet_engine_pomo_command(PET_POMO_CMD_SHORT);
        break;
    case PET_ACT_POMO_LONG:
        pet_engine_pomo_command(PET_POMO_CMD_LONG);
        break;
    case PET_ACT_MODE_POMO:
        pet_engine_set_mode(PET_MODE_POMO);
        break;
    case PET_ACT_MODE_DUTY:
        pet_engine_set_mode(PET_MODE_DUTY);
        break;
    case PET_ACT_MUTE_TOGGLE:
        pet_voice_set_muted(!pet_voice_muted());
        break;
    case PET_ACT_SEND:
        pet_engine_send_line("{\"v\":1,\"cmd\":\"send\"}\n");
        ESP_LOGI(TAG, "key: send");
        break;
    case PET_ACT_BACK_WINDOW:
        pet_engine_request_back_window();
        break;
    case PET_ACT_MENU:
        pet_menu_init(&s_menu);
        s_menu_open = true;
        break;
    case PET_ACT_NONE:
    default:
        break;
    }
}

#ifdef PET_TEST_CLOCK
void pet_engine_test_pomo(int cmd)
{
    s_mode = PET_MODE_POMO; /* 测试：切到番茄钟模式，tick 才会推进 */
    pet_pomo_command(&s_pomo, (pet_pomo_cmd_t)cmd, pet_hal_now_ms());
}
#endif
