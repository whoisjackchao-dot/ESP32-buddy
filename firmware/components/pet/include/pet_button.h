#pragma once

#include <stdbool.h>
#include <stdint.h>

/* M6 增量 1：按键短/长按状态机（纯逻辑，host 可测）。
 * 契约：contracts/M6-按键映射契约清单.md §7.1/§7.2。
 * 不碰 GPIO —— 设备侧每 KEY_DEBOUNCE_MS 采样一次（首次翻转即生效），
 * 本层只认边沿：短按在松键时判定，长按到阈值触发一次。 */

#ifndef KEY_DEBOUNCE_MS
#define KEY_DEBOUNCE_MS 20
#endif

#ifndef KEY_LONGPRESS_MS
#define KEY_LONGPRESS_MS 600 /* 提案值，待批准（§7.1） */
#endif

typedef enum { PET_KEY1 = 0, PET_KEY2, PET_KEY_COUNT } pet_key_t;

typedef enum {
    PET_KEY_EV_NONE = 0, /* 无事件 */
    PET_KEY_EV_SHORT,    /* 松键沿判出：本次按住未达长按阈值 */
    PET_KEY_EV_LONG      /* 按住达到阈值时触发（一次） */
} pet_key_event_t;

typedef struct {
    bool     pressed;    /* 当前（采样后）按住 */
    bool     long_fired; /* 本次按住已触发过长按 → 松键不再计短按 */
    uint32_t press_ms;   /* 本次按下时刻 */
} pet_button_t;

void pet_button_init(pet_button_t *b);

/* 喂一次采样（raw_pressed = 本采样点是否按下）。返回本次产生的事件。 */
pet_key_event_t pet_button_update(pet_button_t *b, bool raw_pressed, uint32_t now_ms);
