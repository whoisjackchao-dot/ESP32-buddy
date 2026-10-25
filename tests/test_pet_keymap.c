/* M6 增量 1 T 类：键 → 动作映射（Q11 已批准；PTT 增量改：值班 KEY1 长 → NONE）。
 * 契约：contracts/M6-按键映射契约清单.md §3/§7.3。用例：BTN-01、PTT-02。 */
#include <stdio.h>

#include "harness.h"
#include "pet_keymap.h"

/* BTN-01：值班模式（§3 第一列；长按不再静音 —— 静音收敛到菜单） */
static void k01_duty_key1(void)
{
    CHECK(pet_keymap_action(PET_MODE_DUTY, PET_KEY1, PET_KEY_EV_SHORT) ==
          PET_ACT_BACK_WINDOW);
    CHECK(pet_keymap_action(PET_MODE_DUTY, PET_KEY1, PET_KEY_EV_LONG) ==
          PET_ACT_NONE);
}

static void k02_duty_key2(void)
{
    CHECK(pet_keymap_action(PET_MODE_DUTY, PET_KEY2, PET_KEY_EV_SHORT) ==
          PET_ACT_MODE_POMO);
    CHECK(pet_keymap_action(PET_MODE_DUTY, PET_KEY2, PET_KEY_EV_LONG) == PET_ACT_MENU);
}

/* BTN-01：番茄钟模式（§3 第二列） */
static void k03_pomo_key1(void)
{
    CHECK(pet_keymap_action(PET_MODE_POMO, PET_KEY1, PET_KEY_EV_SHORT) ==
          PET_ACT_POMO_SHORT);
    CHECK(pet_keymap_action(PET_MODE_POMO, PET_KEY1, PET_KEY_EV_LONG) ==
          PET_ACT_POMO_LONG);
}

static void k04_pomo_key2(void)
{
    CHECK(pet_keymap_action(PET_MODE_POMO, PET_KEY2, PET_KEY_EV_SHORT) ==
          PET_ACT_MODE_DUTY);
    CHECK(pet_keymap_action(PET_MODE_POMO, PET_KEY2, PET_KEY_EV_LONG) == PET_ACT_MENU);
}

/* 无事件一律无动作（两个键 × 两个模式） */
static void k05_none_event_none_action(void)
{
    CHECK(pet_keymap_action(PET_MODE_DUTY, PET_KEY1, PET_KEY_EV_NONE) == PET_ACT_NONE);
    CHECK(pet_keymap_action(PET_MODE_DUTY, PET_KEY2, PET_KEY_EV_NONE) == PET_ACT_NONE);
    CHECK(pet_keymap_action(PET_MODE_POMO, PET_KEY1, PET_KEY_EV_NONE) == PET_ACT_NONE);
    CHECK(pet_keymap_action(PET_MODE_POMO, PET_KEY2, PET_KEY_EV_NONE) == PET_ACT_NONE);
}

/* PTT-02：PRESS/RELEASE 不进键表（引擎直通 PTT），两键 × 两模式均无动作 */
static void k06_press_release_bypass_keymap(void)
{
    CHECK(pet_keymap_action(PET_MODE_DUTY, PET_KEY1, PET_KEY_EV_PRESS) == PET_ACT_NONE);
    CHECK(pet_keymap_action(PET_MODE_DUTY, PET_KEY1, PET_KEY_EV_RELEASE) == PET_ACT_NONE);
    CHECK(pet_keymap_action(PET_MODE_DUTY, PET_KEY2, PET_KEY_EV_PRESS) == PET_ACT_NONE);
    CHECK(pet_keymap_action(PET_MODE_DUTY, PET_KEY2, PET_KEY_EV_RELEASE) == PET_ACT_NONE);
    CHECK(pet_keymap_action(PET_MODE_POMO, PET_KEY1, PET_KEY_EV_PRESS) == PET_ACT_NONE);
    CHECK(pet_keymap_action(PET_MODE_POMO, PET_KEY1, PET_KEY_EV_RELEASE) == PET_ACT_NONE);
    CHECK(pet_keymap_action(PET_MODE_POMO, PET_KEY2, PET_KEY_EV_PRESS) == PET_ACT_NONE);
    CHECK(pet_keymap_action(PET_MODE_POMO, PET_KEY2, PET_KEY_EV_RELEASE) == PET_ACT_NONE);
}

int main(void)
{
    RUN(k01_duty_key1);
    RUN(k02_duty_key2);
    RUN(k03_pomo_key1);
    RUN(k04_pomo_key2);
    RUN(k05_none_event_none_action);
    RUN(k06_press_release_bypass_keymap);
    REPORT();
}
