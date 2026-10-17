/* M8 T 类：休闲状态机（阶段推进 + 小剧调度）。增量 1（纯逻辑，host 可测）。
 * 契约：contracts/M8-休闲与多卡栈契约清单.md §6（LEI-01..05）。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "pet_leisure.h"

#define DAY 12   /* 白天注入小时（不在夜间窗口 [23,7)） */
#define NIGHT 23 /* 夜间注入小时 */
#define NOT_FIRED UINT32_MAX

/* 从 from_ms 起每 1000 ms 注入一次 tick，返回 skit() 变为 want 的时刻；
 * 到 limit 仍未开演返回 NOT_FIRED。间隔提案值为整秒，故 1 s 步进不会跨过边界。 */
static uint32_t poll_skit(pet_leisure_t *l, uint32_t from_ms, uint32_t limit_ms,
                          int hour24, int want)
{
    for (uint32_t t = from_ms; t <= from_ms + limit_ms; t += 1000u) {
        pet_leisure_tick(l, t, false, hour24);
        if (pet_leisure_skit(l) == want) {
            return t;
        }
    }
    return NOT_FIRED;
}

/* LEI-01：init 默认值（DUTY、无剧目、未关灯） */
static void lei01_init_defaults(void)
{
    pet_leisure_t l;
    pet_leisure_init(&l, 1000, 42);
    CHECK(pet_leisure_phase(&l) == PET_LEI_DUTY);
    CHECK(pet_leisure_skit(&l) == -1);
    CHECK(pet_leisure_lights_out(&l) == false);
}

/* LEI-02：阶段边界（>= 即切换）+ 夜间门控 + 时间回绕保护 */
static void lei02_phase_boundaries(void)
{
    pet_leisure_t l;
    pet_leisure_init(&l, 0, 7);

    pet_leisure_tick(&l, 299999, false, DAY); /* 4:59.999 仍值班 */
    CHECK(pet_leisure_phase(&l) == PET_LEI_DUTY);
    pet_leisure_tick(&l, 300000, false, DAY); /* 5:00 → BORED */
    CHECK(pet_leisure_phase(&l) == PET_LEI_BORED);
    pet_leisure_tick(&l, 1799999, false, DAY); /* 29:59.999 仍 BORED */
    CHECK(pet_leisure_phase(&l) == PET_LEI_BORED);
    pet_leisure_tick(&l, 1800000, false, DAY); /* 30:00 → SLEEPY */
    CHECK(pet_leisure_phase(&l) == PET_LEI_SLEEPY);

    pet_leisure_tick(&l, 5399999, false, NIGHT); /* 89:59.999 夜间仍 SLEEPY */
    CHECK(pet_leisure_phase(&l) == PET_LEI_SLEEPY);
    CHECK(pet_leisure_lights_out(&l) == false);
    pet_leisure_tick(&l, 5400000, false, NIGHT); /* 90:00 夜间 → LIGHTS_OUT */
    CHECK(pet_leisure_phase(&l) == PET_LEI_LIGHTS_OUT);
    CHECK(pet_leisure_lights_out(&l) == true);

    pet_leisure_t d; /* 同样 90 min 空闲但白天 → 不关灯，停留 SLEEPY */
    pet_leisure_init(&d, 0, 7);
    pet_leisure_tick(&d, 5400000, false, DAY);
    CHECK(pet_leisure_phase(&d) == PET_LEI_SLEEPY);
    CHECK(pet_leisure_lights_out(&d) == false);

    pet_leisure_t w; /* now < last_active（回绕）：增量按 0，仍 DUTY */
    pet_leisure_init(&w, 1000000, 7);
    pet_leisure_tick(&w, 999999, false, DAY);
    CHECK(pet_leisure_phase(&w) == PET_LEI_DUTY);
}

/* LEI-03：BORED 小剧 —— 首段进入后 3 s，其后间隔 20–40 s，id 轮换；DUTY 恒 -1 */
static void lei03_bored_skit_schedule(void)
{
    pet_leisure_t l;
    pet_leisure_init(&l, 0, 9);
    pet_leisure_tick(&l, 300000, false, DAY); /* 进入 BORED */
    CHECK(pet_leisure_phase(&l) == PET_LEI_BORED);
    CHECK(pet_leisure_skit(&l) == -1); /* 尚无剧目 */

    pet_leisure_tick(&l, 302999, false, DAY);
    CHECK(pet_leisure_skit(&l) == -1); /* 首段（+3 s）之前保持 -1 */
    uint32_t at0 = poll_skit(&l, 303000, 10000, DAY, 0);
    CHECK(at0 == 303000); /* 首段恰在进入 BORED 后 3 s，id 0 */

    uint32_t at1 = poll_skit(&l, at0 + 1000u, 40000u, DAY, 1);
    CHECK(at1 != NOT_FIRED);
    if (at1 == NOT_FIRED) {
        return;
    }
    CHECK(at1 - at0 >= 20000u); /* 第二段间隔下限 20 s */
    CHECK(at1 - at0 <= 40000u); /* 第二段间隔上限 40 s */

    uint32_t at2 = poll_skit(&l, at1 + 1000u, 40000u, DAY, 2);
    CHECK(at2 != NOT_FIRED);
    if (at2 == NOT_FIRED) {
        return;
    }
    CHECK(at2 - at1 >= 20000u);
    CHECK(at2 - at1 <= 40000u);

    /* 唤醒：agent_active → 立即回 DUTY，DUTY 下剧目恒 -1 */
    pet_leisure_tick(&l, at2, true, DAY);
    CHECK(pet_leisure_phase(&l) == PET_LEI_DUTY);
    CHECK(pet_leisure_skit(&l) == -1);
}

/* LEI-04：30 min 切 SLEEPY，小剧时刻表重排（首段再等 3 s，间隔换 120–300 s） */
static void lei04_sleepy_skit_schedule(void)
{
    pet_leisure_t l;
    pet_leisure_init(&l, 0, 11);
    pet_leisure_tick(&l, 300000, false, DAY); /* BORED */
    uint32_t at0 = poll_skit(&l, 303000, 10000, DAY, 0);
    CHECK(at0 == 303000);

    pet_leisure_tick(&l, 1800000, false, DAY); /* 30:00 → SLEEPY */
    CHECK(pet_leisure_phase(&l) == PET_LEI_SLEEPY);
    CHECK(pet_leisure_skit(&l) == 0); /* 口径=最近开演剧目：切相位不擦除 */

    uint32_t at1 = poll_skit(&l, 1800000, 10000, DAY, 1);
    CHECK(at1 == 1803000); /* SLEEPY 首段：进入后 3 s，id 轮换 (0+1)%7 */

    uint32_t at2 = poll_skit(&l, at1 + 1000u, 300000u, DAY, 2);
    CHECK(at2 != NOT_FIRED);
    if (at2 == NOT_FIRED) {
        return;
    }
    CHECK(at2 - at1 >= 120000u);  /* SLEEPY 间隔下限 120 s */
    CHECK(at2 - at1 <= 300000u);  /* SLEEPY 间隔上限 300 s */
}

/* LEI-05：深夜闲置后 agent_active → 立即 DUTY 且空闲计时重置 */
static void lei05_wake_resets_timer(void)
{
    pet_leisure_t l;
    pet_leisure_init(&l, 0, 3);
    pet_leisure_tick(&l, 7200000, false, DAY); /* 空闲 2 h → SLEEPY */
    CHECK(pet_leisure_phase(&l) == PET_LEI_SLEEPY);

    pet_leisure_tick(&l, 7200000, true, DAY); /* agent_active → 立即 DUTY */
    CHECK(pet_leisure_phase(&l) == PET_LEI_DUTY);
    CHECK(pet_leisure_skit(&l) == -1);

    pet_leisure_tick(&l, 7499999, false, DAY); /* 唤醒后 4:59.999 仍值班 */
    CHECK(pet_leisure_phase(&l) == PET_LEI_DUTY);
    pet_leisure_tick(&l, 7500000, false, DAY); /* 再满 5 min → BORED */
    CHECK(pet_leisure_phase(&l) == PET_LEI_BORED);

    pet_leisure_tick(&l, 12600000, false, NIGHT); /* 又 90 min 夜间 → LIGHTS_OUT */
    CHECK(pet_leisure_lights_out(&l) == true);
    pet_leisure_tick(&l, 12600000, true, NIGHT); /* 唤醒同样立即出关灯 */
    CHECK(pet_leisure_phase(&l) == PET_LEI_DUTY);
    CHECK(pet_leisure_lights_out(&l) == false);
}

/* LEI-05：确定性 —— 同 seed 两次运行的开演时刻逐段一致，且都在 20–40 s 内 */
static void lei05_same_seed_same_intervals(void)
{
    pet_leisure_t a, b;
    pet_leisure_init(&a, 0, 20260517u);
    pet_leisure_init(&b, 0, 20260517u);
    pet_leisure_tick(&a, 300000, false, DAY);
    pet_leisure_tick(&b, 300000, false, DAY);

    uint32_t aa[3], bb[3];
    aa[0] = poll_skit(&a, 303000, 10000u, DAY, 0);
    bb[0] = poll_skit(&b, 303000, 10000u, DAY, 0);
    CHECK(aa[0] == 303000 && bb[0] == 303000);
    if (aa[0] == NOT_FIRED || bb[0] == NOT_FIRED) {
        return;
    }
    for (int i = 1; i < 3; i++) {
        aa[i] = poll_skit(&a, aa[i - 1] + 1000u, 40000u, DAY, i);
        bb[i] = poll_skit(&b, bb[i - 1] + 1000u, 40000u, DAY, i);
        CHECK(aa[i] != NOT_FIRED);
        CHECK(aa[i] == bb[i]); /* 同 seed → 同间隔（整秒精度下逐段相同） */
        if (aa[i] == NOT_FIRED || bb[i] == NOT_FIRED) {
            return;
        }
        CHECK(aa[i] - aa[i - 1] >= 20000u);
        CHECK(aa[i] - aa[i - 1] <= 40000u);
    }
}

int main(void)
{
    RUN(lei01_init_defaults);
    RUN(lei02_phase_boundaries);
    RUN(lei03_bored_skit_schedule);
    RUN(lei04_sleepy_skit_schedule);
    RUN(lei05_wake_resets_timer);
    RUN(lei05_same_seed_same_intervals);
    REPORT();
}
