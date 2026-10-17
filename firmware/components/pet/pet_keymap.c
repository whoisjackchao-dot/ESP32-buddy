/* M6 增量 1：键 → 动作映射（Q11 已批准）。
 * 契约：contracts/M6-按键映射契约清单.md §3/§7.3。
 * 纯查表；动作执行（引擎/静音/回窗口/菜单日志）属设备侧接线。 */
#include "pet_keymap.h"

pet_action_t pet_keymap_action(pet_mode_t mode, pet_key_t key, pet_key_event_t ev)
{
    if (ev == PET_KEY_EV_NONE) {
        return PET_ACT_NONE;
    }
    if (mode == PET_MODE_DUTY) {
        if (key == PET_KEY1) {
            return (ev == PET_KEY_EV_SHORT) ? PET_ACT_BACK_WINDOW : PET_ACT_MUTE_TOGGLE;
        }
        return (ev == PET_KEY_EV_SHORT) ? PET_ACT_MODE_POMO : PET_ACT_MENU;
    }
    /* 番茄钟模式 */
    if (key == PET_KEY1) {
        return (ev == PET_KEY_EV_SHORT) ? PET_ACT_POMO_SHORT : PET_ACT_POMO_LONG;
    }
    return (ev == PET_KEY_EV_SHORT) ? PET_ACT_MODE_DUTY : PET_ACT_MENU;
}
