/* M6 增量 1：键 → 动作映射（Q11 已批准）。
 * 契约：contracts/M6-按键映射契约清单.md §3/§7.3；PTT 增量改：
 * 值班 KEY1 长按不再静音（静音收敛到菜单，菜单项 MUTE 不变）；
 * PRESS/RELEASE 不进键表 —— 引擎把 KEY1 的按下/松开沿直通录音模块。 */
#include "pet_keymap.h"

pet_action_t pet_keymap_action(pet_mode_t mode, pet_key_t key, pet_key_event_t ev)
{
    if (ev != PET_KEY_EV_SHORT && ev != PET_KEY_EV_LONG) {
        return PET_ACT_NONE;
    }
    if (mode == PET_MODE_DUTY) {
        if (key == PET_KEY1) {
            return (ev == PET_KEY_EV_SHORT) ? PET_ACT_SEND : PET_ACT_NONE;
        }
        return (ev == PET_KEY_EV_SHORT) ? PET_ACT_MODE_POMO : PET_ACT_MENU;
    }
    /* 番茄钟模式 */
    if (key == PET_KEY1) {
        return (ev == PET_KEY_EV_SHORT) ? PET_ACT_POMO_SHORT : PET_ACT_POMO_LONG;
    }
    return (ev == PET_KEY_EV_SHORT) ? PET_ACT_MODE_DUTY : PET_ACT_MENU;
}
