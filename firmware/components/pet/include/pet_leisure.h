#pragma once

#include <stdbool.h>
#include <stdint.h>

/* M8 增量 1：休闲状态机（阶段推进 + 小剧调度），host 可测。
 * 契约：contracts/M8-休闲与多卡栈契约清单.md §6。
 * 时间与"是否夜间"由外部注入（tick 的 now_ms / hour24）；NTP/夜间时间源属增量 2
 * （Q17=A 仅授时），小剧美术 / 背光硬件属增量 1b——本模块是纯逻辑，不读 GPIO、
 * 不碰显示。 */

/* §6.1 参数（提案值，待批准；编译期可覆盖） */
#ifndef PET_LEI_BORED_AFTER_MS
#define PET_LEI_BORED_AFTER_MS 300000u /* 空闲 5 min → BORED */
#endif
#ifndef PET_LEI_SLEEPY_AFTER_MS
#define PET_LEI_SLEEPY_AFTER_MS 1800000u /* 空闲 30 min → SLEEPY */
#endif
#ifndef PET_LEI_LIGHTS_AFTER_MS
#define PET_LEI_LIGHTS_AFTER_MS 5400000u /* 空闲 90 min 且夜间 → LIGHTS_OUT */
#endif
#ifndef PET_LEI_NIGHT_BEGIN
#define PET_LEI_NIGHT_BEGIN 23 /* 夜间窗口 23:00–07:00：hour24>=23 或 <7 */
#endif
#ifndef PET_LEI_NIGHT_END
#define PET_LEI_NIGHT_END 7
#endif

/* §6.2 小剧参数（提案值，待批准；编译期可覆盖） */
#ifndef PET_LEI_SKIT_FIRST_MS
#define PET_LEI_SKIT_FIRST_MS 3000u /* 进入 BORED/SLEEPY 后首段延迟 3 s */
#endif
#ifndef PET_LEI_BORED_SKIT_MIN_MS
#define PET_LEI_BORED_SKIT_MIN_MS 20000u /* BORED 段间隔 20–40 s */
#endif
#ifndef PET_LEI_BORED_SKIT_MAX_MS
#define PET_LEI_BORED_SKIT_MAX_MS 40000u
#endif
#ifndef PET_LEI_SLEEPY_SKIT_MIN_MS
#define PET_LEI_SLEEPY_SKIT_MIN_MS 120000u /* SLEEPY 段间隔 120–300 s */
#endif
#ifndef PET_LEI_SLEEPY_SKIT_MAX_MS
#define PET_LEI_SLEEPY_SKIT_MAX_MS 300000u
#endif
#ifndef PET_LEI_SKIT_COUNT
#define PET_LEI_SKIT_COUNT 7 /* 剧目 id 0..6，轮换 (上一段+1)%7 */
#endif

/* §6.1 阶段：DUTY →(5min) BORED →(30min) SLEEPY →(90min 且夜间) LIGHTS_OUT */
typedef enum { PET_LEI_DUTY = 0, PET_LEI_BORED, PET_LEI_SLEEPY, PET_LEI_LIGHTS_OUT } pet_leisure_phase_t;

typedef struct {
    pet_leisure_phase_t phase;
    uint32_t last_active_ms; /* 空闲锚点：agent_active（唤醒）即重置为 now */
    uint32_t phase_enter_ms; /* 进入当前相位的时刻 */
    uint32_t next_skit_ms;   /* 下一段小剧开演时刻；0 = 未排（DUTY/LIGHTS_OUT） */
    uint32_t rng_state;      /* 小剧间隔 LCG 状态（init 注入 seed） */
    int skit_id;             /* 最近开演剧目 0..6；-1 = 尚未开演过 */
} pet_leisure_t;

void pet_leisure_init(pet_leisure_t *l, uint32_t now_ms, uint32_t seed);
void pet_leisure_tick(pet_leisure_t *l, uint32_t now_ms, bool agent_active, int hour24);

pet_leisure_phase_t pet_leisure_phase(const pet_leisure_t *l);
int pet_leisure_skit(const pet_leisure_t *l); /* 最近开演剧目 0..6；-1=无；DUTY/LIGHTS_OUT 恒 -1 */
bool pet_leisure_lights_out(const pet_leisure_t *l); /* §6.3：phase == LIGHTS_OUT */
