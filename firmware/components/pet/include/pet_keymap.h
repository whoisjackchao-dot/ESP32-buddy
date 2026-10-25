#pragma once

#include "pet_button.h"
#include "pet_engine.h"

/* M6 增量 1：键 → 动作映射（Q11 已批准，契约 §3/§7.3）。
 * 纯查表：只翻译，不执行 —— 引擎/静音/日志的接线属设备侧。 */

typedef enum {
    PET_ACT_NONE = 0,
    PET_ACT_POMO_SHORT,  /* 番茄钟 KEY1 短：开始/暂停/继续 */
    PET_ACT_POMO_LONG,   /* 番茄钟 KEY1 长：放弃番茄钟 */
    PET_ACT_MODE_POMO,   /* 值班 KEY2 短：切到番茄钟 */
    PET_ACT_MODE_DUTY,   /* 番茄钟 KEY2 短：切回值班 */
    PET_ACT_MUTE_TOGGLE, /* （保留枚举位）静音已收敛到菜单，键表不再产生 */
    PET_ACT_BACK_WINDOW, /* （保留枚举位）回窗口已挪到菜单，键表不再产生 */
    PET_ACT_SEND,        /* 值班 KEY1 短：发送（回车） */
    PET_ACT_MENU         /* KEY2 长：打开菜单 */
} pet_action_t;

pet_action_t pet_keymap_action(pet_mode_t mode, pet_key_t key, pet_key_event_t ev);
