/* M6 增量 1：按键短/长按状态机（纯逻辑）。
 * 契约：contracts/M6-按键映射契约清单.md §7.1/§7.2。
 * 边沿驱动：设备侧每 KEY_DEBOUNCE_MS 采样一次（首次翻转即生效）；
 * 短按在松键沿判定（§6.4），长按在按住达到阈值时触发一次（§6.3）。 */
#include "pet_button.h"

void pet_button_init(pet_button_t *b)
{
    b->pressed = false;
    b->long_fired = false;
    b->press_ms = 0;
}

pet_key_event_t pet_button_update(pet_button_t *b, bool raw_pressed, uint32_t now_ms)
{
    if (raw_pressed && !b->pressed) {
        /* 松→按：记起点，重新武装长按 */
        b->pressed = true;
        b->long_fired = false;
        b->press_ms = now_ms;
        return PET_KEY_EV_NONE;
    }

    if (!raw_pressed && b->pressed) {
        /* 按→松：此时才能确定不是长按 → 判短按（§6.4） */
        b->pressed = false;
        return b->long_fired ? PET_KEY_EV_NONE : PET_KEY_EV_SHORT;
    }

    if (b->pressed && !b->long_fired) {
        /* 持续按住：达到阈值触发一次；now < press_ms 视为未到（防回拨误判，无回绕减法） */
        if (now_ms >= b->press_ms && now_ms - b->press_ms >= KEY_LONGPRESS_MS) {
            b->long_fired = true;
            return PET_KEY_EV_LONG;
        }
    }

    return PET_KEY_EV_NONE;
}
