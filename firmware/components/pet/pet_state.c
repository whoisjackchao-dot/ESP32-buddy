/* M2：pet_state 状态机 + done 边沿合成 + 失联。
 * 契约：contracts/M2-状态机与动画契约清单.md §7（2026-10-16 冻结）。
 * 常量按契约 §7.4 提案值，可用 -D 覆盖（双轨制：待批准、可配置）。 */
#include <stdlib.h>
#include <string.h>

#include "pet_state.h"

#ifndef PET_DONE_DWELL_MS
#define PET_DONE_DWELL_MS 5000u
#endif
#ifndef PET_DONE_EDGE_DEBOUNCE_MS
#define PET_DONE_EDGE_DEBOUNCE_MS 1500u
#endif
#ifndef PET_LINK_TIMEOUT_MS
#define PET_LINK_TIMEOUT_MS 15000u
#endif
/* idle 动画节拍（契约 §9.2，提案值、可配置） */
#ifndef PET_FRAME_MS_DUTY
#define PET_FRAME_MS_DUTY 125u
#endif
#ifndef PET_BLINK_PERIOD_MS
#define PET_BLINK_PERIOD_MS 1000u
#endif
#ifndef PET_BLINK_MS
#define PET_BLINK_MS 125u
#endif
#ifndef PET_IDLE_MOOD_PERIOD_MS
#define PET_IDLE_MOOD_PERIOD_MS 20000u
#endif
#ifndef PET_IDLE_MOOD_MS
#define PET_IDLE_MOOD_MS 2000u
#endif

struct pet_state {
    pet_state_t  state;
    pet_state_t  prev_state;      /* 进 OFFLINE 前的状态，供 LINK_UP 恢复 */
    uint32_t     since_ms;        /* 进入当前状态的时刻 */
    uint32_t     last_valid_ms;   /* 最近一次有效事件时刻 */
    uint32_t     now_ms;          /* 最近一次 apply/tick 的时刻（动画相位用） */
    uint32_t     pending_done_at; /* 0 = 无待定完成 */
    bool         started;
    pet_sound_t  pending_sound;
    pet_card_t   cards[3];        /* M8 §8.2：任务卡栈，cards[0] = 最新（顶部） */
    int          card_count;      /* 0..3 */
};

/* 环绕安全的时间差比较：now - then >= ms */
static bool elapsed(uint32_t now, uint32_t then, uint32_t ms)
{
    return (int32_t)(now - then) >= (int32_t)ms;
}

static void set_state(struct pet_state *s, pet_state_t st, uint32_t now)
{
    s->state = st;
    s->since_ms = now;
}

struct pet_state *pet_state_create(void)
{
    struct pet_state *s = calloc(1, sizeof *s);
    if (s != NULL) {
        s->state = PET_STATE_IDLE;
        s->prev_state = PET_STATE_IDLE;
    }
    return s;
}

void pet_state_destroy(struct pet_state *s)
{
    free(s);
}

void pet_state_apply(struct pet_state *s, const pet_event_t *ev)
{
    if (s == NULL || ev == NULL) {
        return;
    }
    const uint32_t now = ev->now_ms;
    s->last_valid_ms = now;
    s->now_ms = now;
    s->started = true;

    switch (ev->kind) {
    case PET_EV_TASK_START:
        s->pending_done_at = 0; /* 抖动：取消待定完成 */
        set_state(s, PET_STATE_WORKING, now);
        break;

    case PET_EV_AGENT_INPUT_REQUIRED:
        set_state(s, PET_STATE_INPUT_REQUIRED, now);
        s->pending_sound = PET_SOUND_INPUT;
        break;

    case PET_EV_TASK_DONE:
        s->pending_done_at = 0;
        set_state(s, PET_STATE_DONE, now);
        s->pending_sound = PET_SOUND_DONE;
        break;

    case PET_EV_TASK_ERROR:
    case PET_EV_AGENT_BLOCKED:
        s->pending_done_at = 0;
        set_state(s, PET_STATE_FAILED, now);
        s->pending_sound = PET_SOUND_FAILED;
        break;

    case PET_EV_AGENT_IDLE:
        if (s->state == PET_STATE_WORKING || s->state == PET_STATE_INPUT_REQUIRED) {
            /* 边沿：进入待定完成，等去抖窗口 */
            uint32_t at = now + PET_DONE_EDGE_DEBOUNCE_MS;
            s->pending_done_at = (at == 0) ? 1u : at; /* 0 是哨兵 */
        } else {
            s->pending_done_at = 0;
            set_state(s, PET_STATE_IDLE, now);
        }
        break;

    case PET_EV_LINK_DOWN:
        if (s->state != PET_STATE_OFFLINE) {
            s->prev_state = s->state;
        }
        set_state(s, PET_STATE_OFFLINE, now);
        break;

    case PET_EV_LINK_UP:
        set_state(s, (s->prev_state == PET_STATE_OFFLINE) ? PET_STATE_IDLE
                                                          : s->prev_state,
                  now);
        break;

    case PET_EV_CARD:
    case PET_EV_ANNOUNCEMENT:
    default:
        break; /* M4 */
    }
}

void pet_state_tick(struct pet_state *s, uint32_t now_ms)
{
    if (s == NULL) {
        return;
    }
    s->now_ms = now_ms;
    if (!s->started) {
        s->last_valid_ms = now_ms;
        s->started = true;
    }

    /* done 边沿到点 → DONE（播报一次） */
    if (s->pending_done_at != 0 && (int32_t)(now_ms - s->pending_done_at) >= 0) {
        s->pending_done_at = 0;
        set_state(s, PET_STATE_DONE, now_ms);
        s->pending_sound = PET_SOUND_DONE;
    }

    /* DONE 驻留结束 → IDLE */
    if (s->state == PET_STATE_DONE && elapsed(now_ms, s->since_ms, PET_DONE_DWELL_MS)) {
        set_state(s, PET_STATE_IDLE, now_ms);
    }

    /* 失联 → OFFLINE（灰） */
    if (s->state != PET_STATE_OFFLINE &&
        elapsed(now_ms, s->last_valid_ms, PET_LINK_TIMEOUT_MS)) {
        s->prev_state = s->state;
        set_state(s, PET_STATE_OFFLINE, now_ms);
    }
}

/* idle 动画帧（契约 §9）：小动作期优先，其次眨眼 */
static pet_frame_t idle_frame(uint32_t phase)
{
    const uint32_t mood_phase = phase % PET_IDLE_MOOD_PERIOD_MS;
    if (mood_phase >= PET_IDLE_MOOD_PERIOD_MS - PET_IDLE_MOOD_MS) {
        const uint32_t mood = (phase / PET_IDLE_MOOD_PERIOD_MS) % 3u;
        return (mood == 0u) ? PET_FRAME_EYES_CLOSED /* Nap */ : PET_FRAME_NORMAL; /* Look/Stretch */
    }
    const uint32_t blink_phase = phase % PET_BLINK_PERIOD_MS;
    if (blink_phase >= PET_BLINK_PERIOD_MS - PET_BLINK_MS) {
        return PET_FRAME_EYES_CLOSED;
    }
    return PET_FRAME_NORMAL;
}

void pet_state_view(const struct pet_state *s, pet_view_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    if (s == NULL) {
        return;
    }
    out->state = s->state;
    out->gray = (s->state == PET_STATE_OFFLINE);
    out->mirror = false;
    out->card_count = s->card_count;
    out->has_card = (s->card_count > 0);
    for (int i = 0; i < s->card_count; i++) {
        out->cards[i] = s->cards[i];
    }
    if (s->card_count > 0) {
        out->card = s->cards[0]; /* 顶部卡（兼容旧字段） */
    }
    switch (s->state) {
    case PET_STATE_DONE:
        out->frame = PET_FRAME_HAPPY;
        break;
    case PET_STATE_FAILED:
        out->frame = PET_FRAME_SAD;
        break;
    case PET_STATE_OFFLINE:
        out->frame = PET_FRAME_EYES_CLOSED;
        break;
    case PET_STATE_WORKING:
        out->frame = PET_FRAME_WORKING;
        break;
    case PET_STATE_INPUT_REQUIRED:
        out->frame = PET_FRAME_INPUT;
        break;
    case PET_STATE_IDLE:
        out->frame = idle_frame(s->now_ms - s->since_ms);
        break;
    default:
        out->frame = PET_FRAME_NORMAL;
        break;
    }

    const uint32_t phase = s->now_ms - s->since_ms;
    out->hair_right = ((phase / 500u) % 2u) != 0u;
}

pet_sound_t pet_state_take_sound(struct pet_state *s)
{
    if (s == NULL) {
        return PET_SOUND_NONE;
    }
    const pet_sound_t sound = s->pending_sound;
    s->pending_sound = PET_SOUND_NONE;
    return sound;
}

/* M8 §8.2：入栈/更新——同名卡更新并移到顶部；新卡入栈顶；>3 丢最旧（底部）；
 * 空 title 的卡忽略；card == NULL 清栈。 */
void pet_state_set_card(struct pet_state *s, const pet_card_t *card)
{
    if (s == NULL) {
        return;
    }
    if (card == NULL) {
        s->card_count = 0;
        return;
    }
    if (card->title[0] == '\0') {
        return;
    }
    int found = -1;
    for (int i = 0; i < s->card_count; i++) {
        if (strcmp(s->cards[i].title, card->title) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0 && s->card_count < 3) {
        s->card_count++;
    }
    for (int i = (found < 0) ? s->card_count - 1 : found; i > 0; i--) {
        s->cards[i] = s->cards[i - 1];
    }
    s->cards[0] = *card;
}

/* M8 §8.2：消息卡数组**整体替换**栈内容（top-first 原样入栈，≤3 截断，
 * 空 title 项跳过；count<=0 不动栈）。 */
void pet_state_set_cards(struct pet_state *s, const pet_card_t *cards, int count)
{
    if (s == NULL || cards == NULL || count <= 0) {
        return;
    }
    s->card_count = 0;
    for (int i = 0; i < count && s->card_count < 3; i++) {
        if (cards[i].title[0] == '\0') {
            continue;
        }
        s->cards[s->card_count++] = cards[i];
    }
}
