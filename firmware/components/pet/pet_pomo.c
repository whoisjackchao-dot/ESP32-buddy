/* M7 增量 1：番茄钟逻辑（计时状态机 + 当日统计）。
 * 契约：contracts/M7-番茄钟契约清单.md §6。
 * 不读 GPIO（按键属 M6）；不画圆环（7.5）/ 不落 NVS（7.7）/ 不取"今天"（7.9，
 * 外部输入 set_day），均属增量 2；统计仅在 RAM，不持久化。 */
#include "pet_pomo.h"

/* §6.2：相位启动（FOCUS/BREAK 新开一段）：重置累计、记录起点 */
static void start_segment(pet_pomo_t *p, pet_pomo_phase_t phase,
                          uint32_t total_ms, uint32_t now_ms)
{
    p->phase = phase;
    p->total_ms = total_ms;
    p->elapsed_ms = 0;
    p->since_ms = now_ms;
}

/* §6.1：回到 IDLE（放弃或 BREAK_DONE 推进）：清本段累计，统计保持 */
static void enter_idle(pet_pomo_t *p)
{
    p->phase = PET_POMO_IDLE;
    p->elapsed_ms = 0;
}

void pet_pomo_init(pet_pomo_t *p, uint32_t now_ms)
{
    p->phase = PET_POMO_IDLE;
    p->since_ms = now_ms;
    p->elapsed_ms = 0;
    p->total_ms = POMO_FOCUS_MS;
    p->done_count = 0;
    p->focus_seconds = 0;
    p->alarm = false;
    p->has_day = false;
    p->day = 0;
}

void pet_pomo_command(pet_pomo_t *p, pet_pomo_cmd_t cmd, uint32_t now_ms)
{
    /* §6.1：长按一律放弃（IDLE 下无变化），不计完成 */
    if (cmd == PET_POMO_CMD_LONG) {
        if (p->phase != PET_POMO_IDLE) {
            enter_idle(p);
        }
        return;
    }

    switch (p->phase) {
    case PET_POMO_IDLE:
        start_segment(p, PET_POMO_FOCUS, POMO_FOCUS_MS, now_ms);
        break;
    case PET_POMO_FOCUS:
        p->phase = PET_POMO_FOCUS_PAUSED;
        break;
    case PET_POMO_FOCUS_PAUSED: /* 继续：累计保留，只换计时起点 */
        p->phase = PET_POMO_FOCUS;
        p->since_ms = now_ms;
        break;
    case PET_POMO_FOCUS_DONE: /* §6.1/6.6：下一阶段不自动开始，等按键 */
        start_segment(p, PET_POMO_BREAK, POMO_BREAK_MS, now_ms);
        break;
    case PET_POMO_BREAK:
        p->phase = PET_POMO_BREAK_PAUSED;
        break;
    case PET_POMO_BREAK_PAUSED:
        p->phase = PET_POMO_BREAK;
        p->since_ms = now_ms;
        break;
    case PET_POMO_BREAK_DONE:
        enter_idle(p);
        break;
    }
}

void pet_pomo_tick(pet_pomo_t *p, uint32_t now_ms)
{
    /* §6.2：仅 FOCUS/BREAK 推进；暂停/完成/空闲不推进 */
    if (p->phase != PET_POMO_FOCUS && p->phase != PET_POMO_BREAK) {
        return;
    }
    p->elapsed_ms += now_ms - p->since_ms;
    p->since_ms = now_ms;

    /* §6.2：elapsed_ms == total 即视为到点（>=） */
    if (p->phase == PET_POMO_FOCUS && p->elapsed_ms >= POMO_FOCUS_MS) {
        p->phase = PET_POMO_FOCUS_DONE;
        p->alarm = true;
        p->done_count++; /* 专注完成才计入统计 */
        p->focus_seconds += POMO_FOCUS_MS / 1000u;
    } else if (p->phase == PET_POMO_BREAK && p->elapsed_ms >= POMO_BREAK_MS) {
        p->phase = PET_POMO_BREAK_DONE;
        p->alarm = true; /* 休息完成只响铃，不计统计 */
    }
}

void pet_pomo_set_day(pet_pomo_t *p, int32_t day)
{
    /* §6.3：天数变化（或首次）→ 统计清零并记下；相同 → 不动 */
    if (p->has_day && p->day == day) {
        return;
    }
    p->has_day = true;
    p->day = day;
    p->done_count = 0;
    p->focus_seconds = 0;
}

void pet_pomo_view_get(const pet_pomo_t *p, pet_pomo_view_t *out)
{
    uint32_t remaining = 0; /* §6.4：下限 0 */
    if (p->elapsed_ms < p->total_ms) {
        remaining = p->total_ms - p->elapsed_ms;
    }
    out->phase = p->phase;
    out->remaining_ms = remaining;
    out->total_ms = p->total_ms;
    out->done_count = p->done_count;
    out->focus_seconds = p->focus_seconds;
    out->alarm = p->alarm;
}

bool pet_pomo_take_alarm(pet_pomo_t *p)
{
    if (!p->alarm) {
        return false;
    }
    p->alarm = false;
    return true;
}

/* ---- M7 增量 2：当日统计持久化编解码（契约 §7.3）----
 * 16 字节小端 blob：magic "PMC1" | day i32 | done_count u32 | focus_seconds u32。
 * 显式按字节移位写读，不做多字节字段的 memcpy（跨端一致）。 */

static void put_le32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v & 0xFFu);
    b[1] = (uint8_t)((v >> 8) & 0xFFu);
    b[2] = (uint8_t)((v >> 16) & 0xFFu);
    b[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t get_le32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

size_t pet_pomo_encode_stats(uint8_t *buf, size_t cap, const pet_pomo_t *p)
{
    if (cap < PET_POMO_STATS_LEN) {
        return 0;
    }
    buf[0] = (uint8_t)'P';
    buf[1] = (uint8_t)'M';
    buf[2] = (uint8_t)'C';
    buf[3] = (uint8_t)'1';
    put_le32(&buf[4], (uint32_t)p->day); /* int32 → uint32 按 2^32 取模，即补码字节序 */
    put_le32(&buf[8], p->done_count);
    put_le32(&buf[12], p->focus_seconds);
    return PET_POMO_STATS_LEN;
}

bool pet_pomo_decode_stats(const uint8_t *buf, size_t len, pet_pomo_t *p)
{
    if (len != PET_POMO_STATS_LEN) {
        return false;
    }
    if (buf[0] != (uint8_t)'P' || buf[1] != (uint8_t)'M' ||
        buf[2] != (uint8_t)'C' || buf[3] != (uint8_t)'1') {
        return false;
    }
    p->has_day = true;
    p->day = (int32_t)get_le32(&buf[4]); /* 两端补码平台上即符号回绕 */
    p->done_count = get_le32(&buf[8]);
    p->focus_seconds = get_le32(&buf[12]);
    return true;
}
