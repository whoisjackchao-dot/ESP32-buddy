/* M8 增量 1：休闲状态机（阶段推进 + 小剧调度）。
 * 契约：contracts/M8-休闲与多卡栈契约清单.md §6。
 * 纯逻辑：时间与夜间小时由 tick 注入（无 NTP，Q17 属增量 2）；不画小剧、
 * 不控背光（增量 1b）；不读 GPIO。 */
#include "pet_leisure.h"

/* §6.1：夜间窗口 23:00–07:00（hour24 >= 23 或 < 7，外部注入） */
static bool lei_is_night(int hour24)
{
    return hour24 >= PET_LEI_NIGHT_BEGIN || hour24 < PET_LEI_NIGHT_END;
}

/* 回绕保护：now < 锚点（monotonic 时钟回卷）时增量按 0，不做无符号回卷 */
static uint32_t lei_idle_ms(const pet_leisure_t *l, uint32_t now_ms)
{
    return (now_ms >= l->last_active_ms) ? (now_ms - l->last_active_ms) : 0u;
}

/* §6.1：阶段是 idle（与夜间门控）的纯函数；LIGHTS_OUT 仅在夜间覆盖 SLEEPY */
static pet_leisure_phase_t lei_target_phase(const pet_leisure_t *l, uint32_t now_ms,
                                            int hour24)
{
    uint32_t idle = lei_idle_ms(l, now_ms);
    if (idle < PET_LEI_BORED_AFTER_MS) {
        return PET_LEI_DUTY;
    }
    if (idle < PET_LEI_SLEEPY_AFTER_MS) {
        return PET_LEI_BORED;
    }
    if (idle >= PET_LEI_LIGHTS_AFTER_MS && lei_is_night(hour24)) {
        return PET_LEI_LIGHTS_OUT;
    }
    return PET_LEI_SLEEPY;
}

/* 进入相位：重排小剧时刻表（DUTY/LIGHTS_OUT 不排；BORED/SLEEPY 首段延迟 3 s） */
static void lei_enter_phase(pet_leisure_t *l, pet_leisure_phase_t phase, uint32_t now_ms)
{
    l->phase = phase;
    l->phase_enter_ms = now_ms;
    l->next_skit_ms = 0;
    if (phase == PET_LEI_BORED || phase == PET_LEI_SLEEPY) {
        l->next_skit_ms = now_ms + PET_LEI_SKIT_FIRST_MS;
    }
}

/* §6.2：确定性间隔 —— LCG（Numerical Recipes）：
 *   state' = state * 1664525 + 1013904223 (mod 2^32)
 * 间隔 = min + 1s * ((state' >> 16) % 步数)，步数 = (max-min)/1s + 1，
 * 恒为整秒且含两端 [min, max]；同 seed 必得同序列。 */
static uint32_t lei_next_interval(pet_leisure_t *l, pet_leisure_phase_t phase)
{
    uint32_t lo = PET_LEI_BORED_SKIT_MIN_MS;
    uint32_t hi = PET_LEI_BORED_SKIT_MAX_MS;
    if (phase == PET_LEI_SLEEPY) {
        lo = PET_LEI_SLEEPY_SKIT_MIN_MS;
        hi = PET_LEI_SLEEPY_SKIT_MAX_MS;
    }
    l->rng_state = l->rng_state * 1664525u + 1013904223u;
    uint32_t steps = (hi - lo) / 1000u + 1u;
    return lo + ((l->rng_state >> 16) % steps) * 1000u;
}

void pet_leisure_init(pet_leisure_t *l, uint32_t now_ms, uint32_t seed)
{
    l->last_active_ms = now_ms;
    l->rng_state = seed;
    l->skit_id = -1;
    lei_enter_phase(l, PET_LEI_DUTY, now_ms);
}

void pet_leisure_tick(pet_leisure_t *l, uint32_t now_ms, bool agent_active, int hour24)
{
    /* §6.1：唤醒（事件/按键/断链 → agent_active）立即回 DUTY 并重置空闲计时 */
    if (agent_active) {
        l->last_active_ms = now_ms;
        lei_enter_phase(l, PET_LEI_DUTY, now_ms);
        return;
    }

    pet_leisure_phase_t target = lei_target_phase(l, now_ms, hour24);
    if (target != l->phase) {
        lei_enter_phase(l, target, now_ms); /* 切相位优先于未开演的旧剧目 */
    }

    /* §6.2：到点开演一段（轮换 id），并按当前相位排下一间隔 */
    if (l->next_skit_ms != 0u && now_ms >= l->next_skit_ms) {
        l->skit_id = (l->skit_id + 1) % PET_LEI_SKIT_COUNT;
        l->next_skit_ms = now_ms + lei_next_interval(l, l->phase);
    }
}

pet_leisure_phase_t pet_leisure_phase(const pet_leisure_t *l)
{
    return l->phase;
}

int pet_leisure_skit(const pet_leisure_t *l)
{
    /* §6.3 口径（歧义取最简读法）：返回最近开演剧目（非 3 s 窗口内的"正在演"）；
     * DUTY/LIGHTS_OUT 恒 -1，BORED/SLEEPY 返回上一段 id（含切相位时保留） */
    if (l->phase != PET_LEI_BORED && l->phase != PET_LEI_SLEEPY) {
        return -1;
    }
    return l->skit_id;
}

bool pet_leisure_lights_out(const pet_leisure_t *l)
{
    return l->phase == PET_LEI_LIGHTS_OUT;
}
