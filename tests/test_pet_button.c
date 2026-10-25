/* M6 增量 1 T 类：按键短/长按状态机。
 * 契约：contracts/M6-按键映射契约清单.md §7.1/§7.2；PTT 增量补 PRESS/RELEASE。
 * 用例：BTN-02（边沿/去抖采样语义）、BTN-03（长按阈值）、BTN-04（短按松键判定）、
 *       PTT-01（按下/松开沿事件与顺延规则）。 */
#include <stdio.h>
#include <string.h>

#include "harness.h"
#include "pet_button.h"

/* BTN-02：init 清干净状态（含事件队列），空闲采样无事件 */
static void b01_init_idle_none(void)
{
    pet_button_t b;
    /* 预置"脏"残留态（上一次按住未清）：init 必须全部复位。
     * （不用 memset 0xFF —— 对 _Bool 是无效值，属 UB。） */
    b.pressed = true;
    b.long_fired = true;
    b.press_ms = 12345u;
    b.q_head = 1;
    b.q_len = 2;
    b.q[0] = PET_KEY_EV_RELEASE;
    b.q[1] = PET_KEY_EV_SHORT;
    pet_button_init(&b);
    CHECK(!b.pressed);
    CHECK(!b.long_fired);
    CHECK(b.press_ms == 0u);
    CHECK(b.q_head == 0);
    CHECK(b.q_len == 0);
    CHECK(pet_button_update(&b, false, 100u) == PET_KEY_EV_NONE);
}

/* BTN-02/PTT-01：按下沿发 PRESS，并记录起点 */
static void b02_press_edge_press(void)
{
    pet_button_t b;
    pet_button_init(&b);
    CHECK(pet_button_update(&b, true, 100u) == PET_KEY_EV_PRESS);
    CHECK(b.pressed);
    CHECK(!b.long_fired);
    CHECK(b.press_ms == 100u);
}

/* BTN-04/PTT-01：按住 < 600ms 采样全 NONE；松键沿判 SHORT，
 * RELEASE 顺延一拍发出；再按重新武装 */
static void b03_short_then_release(void)
{
    pet_button_t b;
    pet_button_init(&b);
    CHECK(pet_button_update(&b, true, 0u) == PET_KEY_EV_PRESS);
    int held_none = 1;
    for (uint32_t t = KEY_DEBOUNCE_MS; t < 580u; t += KEY_DEBOUNCE_MS) {
        if (pet_button_update(&b, true, t) != PET_KEY_EV_NONE) {
            held_none = 0;
        }
    }
    CHECK(held_none);
    CHECK(pet_button_update(&b, false, 580u) == PET_KEY_EV_SHORT);
    CHECK(!b.pressed);
    CHECK(pet_button_update(&b, false, 600u) == PET_KEY_EV_RELEASE); /* 顺延一拍 */
    CHECK(pet_button_update(&b, false, 620u) == PET_KEY_EV_NONE);    /* 吐完即静默 */
    CHECK(pet_button_update(&b, true, 1000u) == PET_KEY_EV_PRESS);
    CHECK(pet_button_update(&b, false, 1200u) == PET_KEY_EV_SHORT);
    CHECK(pet_button_update(&b, false, 1220u) == PET_KEY_EV_RELEASE);
}

/* BTN-03/PTT-01：600ms 触发 LONG 恰好一次；长按后松键只发 RELEASE（无 SHORT）；
 * 再按可再次长按 */
static void b04_long_then_release(void)
{
    pet_button_t b;
    pet_button_init(&b);
    CHECK(pet_button_update(&b, true, 0u) == PET_KEY_EV_PRESS);
    int pre_none = 1;
    for (uint32_t t = KEY_DEBOUNCE_MS; t < 600u; t += KEY_DEBOUNCE_MS) { /* 到 580 为止 */
        if (pet_button_update(&b, true, t) != PET_KEY_EV_NONE) {
            pre_none = 0;
        }
    }
    CHECK(pre_none);
    CHECK(pet_button_update(&b, true, 600u) == PET_KEY_EV_LONG);
    CHECK(b.long_fired);
    CHECK(pet_button_update(&b, true, 620u) == PET_KEY_EV_NONE); /* 继续按住不重复 */
    CHECK(pet_button_update(&b, true, 5000u) == PET_KEY_EV_NONE);
    CHECK(pet_button_update(&b, false, 5020u) == PET_KEY_EV_RELEASE); /* 无 SHORT */
    CHECK(!b.pressed);
    CHECK(pet_button_update(&b, false, 5040u) == PET_KEY_EV_NONE);
    CHECK(pet_button_update(&b, true, 6000u) == PET_KEY_EV_PRESS);
    CHECK(pet_button_update(&b, true, 6600u) == PET_KEY_EV_LONG); /* 重新武装 */
}

/* BTN-03：边界 599 → SHORT，600 → LONG（按住达阈值判定用 >=） */
static void b05_boundary_599_600(void)
{
    pet_button_t b;
    pet_button_init(&b);
    CHECK(pet_button_update(&b, true, 0u) == PET_KEY_EV_PRESS);
    CHECK(pet_button_update(&b, false, 599u) == PET_KEY_EV_SHORT); /* 599 松开 → 短按 */
    CHECK(pet_button_update(&b, false, 619u) == PET_KEY_EV_RELEASE);
    CHECK(pet_button_update(&b, true, 1000u) == PET_KEY_EV_PRESS);
    CHECK(pet_button_update(&b, true, 1600u) == PET_KEY_EV_LONG); /* 1600-1000 = 600 */
    pet_button_t c;
    pet_button_init(&c);
    CHECK(pet_button_update(&c, true, 0u) == PET_KEY_EV_PRESS);
    CHECK(pet_button_update(&c, true, 599u) == PET_KEY_EV_NONE); /* 差 1ms 不到阈值 */
    CHECK(pet_button_update(&c, false, 599u) == PET_KEY_EV_SHORT);
    CHECK(pet_button_update(&c, false, 619u) == PET_KEY_EV_RELEASE);
}

/* BTN-03：时间回拨（now < press_ms）不得回绕误判长按，视为未到阈值 */
static void b06_time_backwards_no_long(void)
{
    pet_button_t b;
    pet_button_init(&b);
    CHECK(pet_button_update(&b, true, 1000u) == PET_KEY_EV_PRESS);
    CHECK(pet_button_update(&b, true, 999u) == PET_KEY_EV_NONE);
    CHECK(pet_button_update(&b, false, 999u) == PET_KEY_EV_SHORT);
    CHECK(pet_button_update(&b, false, 1019u) == PET_KEY_EV_RELEASE);
}

/* PTT-01：急速双击 —— SHORT 顺延的 RELEASE 与新 PRESS 相邻时，
 * RELEASE 先行、PRESS 顺延一拍，事件不丢且时序不倒置 */
static void b07_double_tap_events_ordered(void)
{
    pet_button_t b;
    pet_button_init(&b);
    CHECK(pet_button_update(&b, true, 0u) == PET_KEY_EV_PRESS);
    CHECK(pet_button_update(&b, false, 20u) == PET_KEY_EV_SHORT);
    CHECK(pet_button_update(&b, true, 40u) == PET_KEY_EV_RELEASE); /* 先吐 RELEASE */
    CHECK(pet_button_update(&b, false, 60u) == PET_KEY_EV_PRESS);  /* 再补 PRESS */
    CHECK(pet_button_update(&b, false, 80u) == PET_KEY_EV_SHORT);
    CHECK(pet_button_update(&b, false, 100u) == PET_KEY_EV_RELEASE);
    CHECK(pet_button_update(&b, false, 120u) == PET_KEY_EV_NONE);
}

/* PTT-01：长按后松键只发 RELEASE，且事件队列清空、无残留 */
static void b08_long_held_release_single(void)
{
    pet_button_t b;
    pet_button_init(&b);
    CHECK(pet_button_update(&b, true, 0u) == PET_KEY_EV_PRESS);
    CHECK(pet_button_update(&b, true, 700u) == PET_KEY_EV_LONG);
    CHECK(b.q_len == 0);
    CHECK(pet_button_update(&b, false, 720u) == PET_KEY_EV_RELEASE);
    CHECK(b.q_len == 0);
    CHECK(pet_button_update(&b, false, 740u) == PET_KEY_EV_NONE);
}

int main(void)
{
    RUN(b01_init_idle_none);
    RUN(b02_press_edge_press);
    RUN(b03_short_then_release);
    RUN(b04_long_then_release);
    RUN(b05_boundary_599_600);
    RUN(b06_time_backwards_no_long);
    RUN(b07_double_tap_events_ordered);
    RUN(b08_long_held_release_single);
    REPORT();
}
