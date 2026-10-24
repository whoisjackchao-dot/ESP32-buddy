/* M6 增量 3 T 类：菜单纯逻辑（选中切换 + 命令产出）。
 * 契约：contracts/M6-按键映射契约清单.md §9.1/§9.2。
 * 用例：BTN-07（菜单内按键）。 */
#include <stdio.h>

#include "harness.h"
#include "pet_menu.h"

/* BTN-07：init 必须选中第 0 项（静音），清掉脏残留 */
static void m01_init_selects_zero(void)
{
    pet_menu_t m;
    m.selected = 1; /* 预置脏残留 */
    pet_menu_init(&m);
    CHECK(m.selected == 0);
}

/* BTN-07：KEY1 短按在三行间循环切换 0→1→2→0 */
static void m02_key1_short_toggles_selection(void)
{
    pet_menu_t m;
    pet_menu_init(&m);
    CHECK(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_SHORT) == PET_MENU_CMD_NONE);
    CHECK(m.selected == 1);
    CHECK(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_SHORT) == PET_MENU_CMD_NONE);
    CHECK(m.selected == 2);
    CHECK(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_SHORT) == PET_MENU_CMD_NONE);
    CHECK(m.selected == 0);
}

/* BTN-07：选中配网（2）时 KEY2 短按 → 产出配网命令（菜单由引擎关闭） */
static void m07_key2_short_on_provision_requests_ap(void)
{
    pet_menu_t m;
    pet_menu_init(&m);
    (void)pet_menu_key(&m, PET_KEY1, PET_KEY_EV_SHORT);
    (void)pet_menu_key(&m, PET_KEY1, PET_KEY_EV_SHORT);
    CHECK(m.selected == 2);
    CHECK(pet_menu_key(&m, PET_KEY2, PET_KEY_EV_SHORT) == PET_MENU_CMD_PROVISION);
    CHECK(m.selected == 2);
}

/* BTN-07：选中静音（0）时 KEY2 短按 → 翻转静音，选中不动（菜单保持开） */
static void m03_key2_short_on_mute_toggles_mute_cmd(void)
{
    pet_menu_t m;
    pet_menu_init(&m);
    CHECK(pet_menu_key(&m, PET_KEY2, PET_KEY_EV_SHORT) == PET_MENU_CMD_TOGGLE_MUTE);
    CHECK(m.selected == 0);
}

/* BTN-07：选中状态（1）时 KEY2 短按 → 关闭菜单（状态只读） */
static void m04_key2_short_on_status_closes(void)
{
    pet_menu_t m;
    pet_menu_init(&m);
    CHECK(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_SHORT) == PET_MENU_CMD_NONE);
    CHECK(pet_menu_key(&m, PET_KEY2, PET_KEY_EV_SHORT) == PET_MENU_CMD_CLOSE);
}

/* BTN-07：KEY2 长按一律关闭菜单（无论选中哪行） */
static void m05_key2_long_closes(void)
{
    pet_menu_t m;
    pet_menu_init(&m);
    CHECK(pet_menu_key(&m, PET_KEY2, PET_KEY_EV_LONG) == PET_MENU_CMD_CLOSE);
    CHECK(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_SHORT) == PET_MENU_CMD_NONE);
    CHECK(pet_menu_key(&m, PET_KEY2, PET_KEY_EV_LONG) == PET_MENU_CMD_CLOSE);
}

/* BTN-07：KEY1 长按在菜单内无动作 */
static void m06_key1_long_none(void)
{
    pet_menu_t m;
    pet_menu_init(&m);
    CHECK(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_LONG) == PET_MENU_CMD_NONE);
}

/* BTN-07：无事件（ev==NONE）一律无动作（2 键矩阵） */
static void m07_ev_none_none(void)
{
    pet_menu_t m;
    pet_menu_init(&m);
    CHECK(pet_menu_key(&m, PET_KEY1, PET_KEY_EV_NONE) == PET_MENU_CMD_NONE);
    CHECK(pet_menu_key(&m, PET_KEY2, PET_KEY_EV_NONE) == PET_MENU_CMD_NONE);
}

int main(void)
{
    RUN(m01_init_selects_zero);
    RUN(m02_key1_short_toggles_selection);
    RUN(m03_key2_short_on_mute_toggles_mute_cmd);
    RUN(m04_key2_short_on_status_closes);
    RUN(m05_key2_long_closes);
    RUN(m06_key1_long_none);
    RUN(m07_key2_short_on_provision_requests_ap);
    RUN(m07_ev_none_none);
    REPORT();
}
