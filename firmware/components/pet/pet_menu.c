/* pet_menu：菜单状态机（M6 增量 3，纯逻辑 host 可测）。
 * 契约：contracts/M6-按键映射契约清单.md §9.1/§9.2。
 * KEY1 短按切选中；KEY2 短按按选中行产出：静音翻转 / 状态关闭 / 配网模式；KEY2 长按关闭。 */
#include "pet_menu.h"

void pet_menu_init(pet_menu_t *m)
{
    m->selected = 0; /* 打开菜单默认选中静音 */
}

pet_menu_cmd_t pet_menu_key(pet_menu_t *m, pet_key_t key, pet_key_event_t ev)
{
    if (ev == PET_KEY_EV_SHORT) {
        if (key == PET_KEY1) {
            m->selected = (m->selected + 1) % PET_MENU_ITEMS;
            return PET_MENU_CMD_NONE;
        }
        if (key == PET_KEY2) {
            if (m->selected == 0) {
                return PET_MENU_CMD_TOGGLE_MUTE;
            }
            return (m->selected == 2) ? PET_MENU_CMD_PROVISION
                                      : PET_MENU_CMD_CLOSE;
        }
    } else if (ev == PET_KEY_EV_LONG && key == PET_KEY2) {
        return PET_MENU_CMD_CLOSE;
    }
    return PET_MENU_CMD_NONE;
}
