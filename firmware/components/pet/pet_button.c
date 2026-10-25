/* M6 增量 1：按键短/长按状态机（纯逻辑）。
 * 契约：contracts/M6-按键映射契约清单.md §7.1/§7.2；PTT 增量补 PRESS/RELEASE。
 * 边沿驱动：设备侧每 KEY_DEBOUNCE_MS 采样一次（首次翻转即生效）；
 * 按下沿发 PRESS；短按在松键沿判定（§6.4），长按在按住达到阈值时触发一次（§6.3）；
 * 松键沿同时判出 SHORT 与 RELEASE → 两事件入队，按物理时序先产先发。
 * 每次 update 先出队一个旧事件、再最多产生两个新事件入队；队列满时丢最旧
 * （仅病态持续抖动可触发，见 evq_push）。 */
#include "pet_button.h"

static pet_key_event_t evq_pop(pet_button_t *b)
{
    if (b->q_len == 0) {
        return PET_KEY_EV_NONE;
    }
    const pet_key_event_t ev = b->q[b->q_head];
    b->q_head = (uint8_t)((b->q_head + 1u) % PET_KEY_EVQ_LEN);
    b->q_len--;
    return ev;
}

static void evq_push(pet_button_t *b, pet_key_event_t ev)
{
    if (b->q_len >= PET_KEY_EVQ_LEN) {
        /* 持续病态抖动（每采样点都翻转）才会积满：丢最旧事件。
         * 新事件对应当前物理态，PTT 的启停沿必须存活；旧 SHORT/LONG 丢失可容忍。 */
        (void)evq_pop(b);
    }
    b->q[(b->q_head + b->q_len) % PET_KEY_EVQ_LEN] = ev;
    b->q_len++;
}

void pet_button_init(pet_button_t *b)
{
    b->pressed = false;
    b->long_fired = false;
    b->press_ms = 0;
    b->q_head = 0;
    b->q_len = 0;
}

pet_key_event_t pet_button_update(pet_button_t *b, bool raw_pressed, uint32_t now_ms)
{
    pet_key_event_t out = evq_pop(b);
    pet_key_event_t ev = PET_KEY_EV_NONE;
    pet_key_event_t ev2 = PET_KEY_EV_NONE; /* 松键沿的第二事件（RELEASE） */

    if (raw_pressed && !b->pressed) {
        /* 松→按：记起点，重新武装长按 */
        b->pressed = true;
        b->long_fired = false;
        b->press_ms = now_ms;
        ev = PET_KEY_EV_PRESS;
    } else if (!raw_pressed && b->pressed) {
        /* 按→松：此时才能确定不是长按 → 判短按（§6.4）；RELEASE 紧随 SHORT */
        b->pressed = false;
        if (b->long_fired) {
            ev = PET_KEY_EV_RELEASE;
        } else {
            ev = PET_KEY_EV_SHORT;
            ev2 = PET_KEY_EV_RELEASE;
        }
    } else if (b->pressed && !b->long_fired) {
        /* 持续按住：达到阈值触发一次；now < press_ms 视为未到（防回拨，无回绕减法） */
        if (now_ms >= b->press_ms && now_ms - b->press_ms >= KEY_LONGPRESS_MS) {
            b->long_fired = true;
            ev = PET_KEY_EV_LONG;
        }
    }

    /* 本拍至多 3 事件（积压 ≤2 + 新 ≤2，出队后 ≤3）：最旧者作为返回值，
     * 其余按物理时序入队，绝不乱序/丢弃。 */
    if (out == PET_KEY_EV_NONE) {
        out = ev;
        ev = ev2;
        ev2 = PET_KEY_EV_NONE;
    }
    if (ev != PET_KEY_EV_NONE) {
        evq_push(b, ev);
    }
    if (ev2 != PET_KEY_EV_NONE) {
        evq_push(b, ev2);
    }
    return out;
}
