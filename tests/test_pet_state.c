/* M2 T 类：pet_state 状态机 + done 边沿 + 失联。
 * 断言体现业务规则（显式事件去的态、边沿合成、抖动取消、失联灰化），不是"调用了函数"。
 * 契约：contracts/M2-状态机与动画契约清单.md §7。 */
#include "harness.h"
#include "pet_state.h"

#define DEBOUNCE_MS 1500u /* 与契约 §7.4 提案值一致；实现可配置，测试固定 */
#define DWELL_MS    5000u
#define LINK_MS     15000u

static struct pet_state *s;

static pet_event_t ev(pet_event_kind_t kind, uint32_t now)
{
    pet_event_t e;
    e.kind = kind;
    e.now_ms = now;
    e.title = NULL;
    e.project = NULL;
    e.card_state = PET_STATE_IDLE;
    e.elapsed_s = 0;
    return e;
}

static void apply_kind(pet_event_kind_t kind, uint32_t now)
{
    pet_event_t e = ev(kind, now);
    pet_state_apply(s, &e);
}

static pet_state_t view_state(void)
{
    pet_view_t v;
    pet_state_view(s, &v);
    return v.state;
}

static pet_frame_t view_frame(void)
{
    pet_view_t v;
    pet_state_view(s, &v);
    return v.frame;
}

static bool view_gray(void)
{
    pet_view_t v;
    pet_state_view(s, &v);
    return v.gray;
}

/* ---- 显式事件 ---- */

static void fresh_is_idle_normal_no_sound(void)
{
    pet_view_t v;
    pet_state_view(s, &v);
    CHECK(v.state == PET_STATE_IDLE);
    CHECK(v.frame == PET_FRAME_NORMAL);
    CHECK(v.gray == false);
    CHECK(pet_state_take_sound(s) == PET_SOUND_NONE);
}

static void task_start_is_working_silent(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    CHECK(view_state() == PET_STATE_WORKING);
    CHECK(view_frame() == PET_FRAME_NORMAL);
    CHECK(pet_state_take_sound(s) == PET_SOUND_NONE);
}

static void input_required_announces_once(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    apply_kind(PET_EV_AGENT_INPUT_REQUIRED, 2000);
    CHECK(view_state() == PET_STATE_INPUT_REQUIRED);
    CHECK(pet_state_take_sound(s) == PET_SOUND_INPUT);
    CHECK(pet_state_take_sound(s) == PET_SOUND_NONE); /* 取走即清 */
}

static void explicit_done_announces_and_is_happy(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    apply_kind(PET_EV_TASK_DONE, 2000);
    CHECK(view_state() == PET_STATE_DONE);
    CHECK(view_frame() == PET_FRAME_HAPPY);
    CHECK(pet_state_take_sound(s) == PET_SOUND_DONE);
}

static void task_error_and_blocked_are_failed_sad(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    apply_kind(PET_EV_TASK_ERROR, 2000);
    CHECK(view_state() == PET_STATE_FAILED);
    CHECK(view_frame() == PET_FRAME_SAD);
    CHECK(pet_state_take_sound(s) == PET_SOUND_FAILED);

    apply_kind(PET_EV_TASK_START, 3000);
    apply_kind(PET_EV_AGENT_BLOCKED, 4000);
    CHECK(view_state() == PET_STATE_FAILED);
    CHECK(pet_state_take_sound(s) == PET_SOUND_FAILED);
}

/* ---- done 边沿合成 ---- */

static void idle_edge_becomes_done_after_debounce(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    apply_kind(PET_EV_AGENT_IDLE, 2000); /* 边沿：进入待定，仍是 WORKING */
    CHECK(view_state() == PET_STATE_WORKING);
    CHECK(pet_state_take_sound(s) == PET_SOUND_NONE);

    pet_state_tick(s, 2000 + DEBOUNCE_MS - 1);
    CHECK(view_state() == PET_STATE_WORKING); /* 未到，不能提前 done */

    pet_state_tick(s, 2000 + DEBOUNCE_MS);
    CHECK(view_state() == PET_STATE_DONE);
    CHECK(view_frame() == PET_FRAME_HAPPY);
    CHECK(pet_state_take_sound(s) == PET_SOUND_DONE);
}

static void retry_during_debounce_cancels_done(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    apply_kind(PET_EV_AGENT_IDLE, 2000);
    /* 抖动：去抖窗口内又开工 → 取消完成 */
    apply_kind(PET_EV_TASK_START, 2500);
    pet_state_tick(s, 2000 + DEBOUNCE_MS);
    CHECK(view_state() == PET_STATE_WORKING);
    CHECK(pet_state_take_sound(s) == PET_SOUND_NONE);
}

static void idle_from_idle_stays_idle_silent(void)
{
    apply_kind(PET_EV_AGENT_IDLE, 1000);
    pet_state_tick(s, 1000 + DEBOUNCE_MS + 1);
    CHECK(view_state() == PET_STATE_IDLE);
    CHECK(pet_state_take_sound(s) == PET_SOUND_NONE);
}

static void done_returns_to_idle_after_dwell(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    apply_kind(PET_EV_TASK_DONE, 2000);
    CHECK(view_state() == PET_STATE_DONE);
    pet_state_tick(s, 2000 + DWELL_MS - 1);
    CHECK(view_state() == PET_STATE_DONE);
    pet_state_tick(s, 2000 + DWELL_MS);
    CHECK(view_state() == PET_STATE_IDLE);
}

/* ---- 失联 ---- */

static void link_timeout_goes_offline_gray(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    pet_state_tick(s, 1000 + LINK_MS - 1);
    CHECK(view_state() == PET_STATE_WORKING); /* 还没超时 */
    pet_state_tick(s, 1000 + LINK_MS + 1);
    CHECK(view_state() == PET_STATE_OFFLINE);
    CHECK(view_gray() == true);
    CHECK(view_frame() == PET_FRAME_EYES_CLOSED);
}

static void link_up_restores_previous_state(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    apply_kind(PET_EV_LINK_DOWN, 2000);
    CHECK(view_state() == PET_STATE_OFFLINE);
    apply_kind(PET_EV_LINK_UP, 3000);
    CHECK(view_state() == PET_STATE_WORKING);
    CHECK(view_gray() == false);
}

static void activity_refreshes_link_timer(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    apply_kind(PET_EV_TASK_START, 1000 + LINK_MS);        /* 有活动 → 刷新 */
    pet_state_tick(s, 1000 + LINK_MS + 100);
    CHECK(view_state() == PET_STATE_WORKING);             /* 不应误判失联 */
}

/* ---- idle 动画（契约 §9）---- */

/* 保活：ANNOUNCEMENT 不改状态、只刷新失联计时（否则测试时间一长会先判失联） */
static void keepalive(uint32_t now)
{
    apply_kind(PET_EV_ANNOUNCEMENT, now);
}

static void idle_blinks_periodically(void)
{
    apply_kind(PET_EV_AGENT_IDLE, 1000);
    pet_state_tick(s, 1000);
    CHECK(view_frame() == PET_FRAME_NORMAL); /* 相位 0：睁眼 */

    keepalive(1900);
    pet_state_tick(s, 1900); /* 相位 900 ∈ 末段 → 眨眼 */
    CHECK(view_frame() == PET_FRAME_EYES_CLOSED);

    keepalive(2000);
    pet_state_tick(s, 2000); /* 新周期起点 → 睁眼 */
    CHECK(view_frame() == PET_FRAME_NORMAL);
}

static void idle_naps_in_mood_window(void)
{
    apply_kind(PET_EV_AGENT_IDLE, 1000);

    keepalive(10000);
    pet_state_tick(s, 10000);

    keepalive(20000);
    pet_state_tick(s, 20000); /* 相位 19000 ∈ 小动作期，第 0 次 = Nap → 闭眼 */
    CHECK(view_frame() == PET_FRAME_EYES_CLOSED);

    keepalive(30000);
    pet_state_tick(s, 30000); /* 相位 29000：不在动作期 → 睁眼 */
    CHECK(view_frame() == PET_FRAME_NORMAL);

    keepalive(40000);
    pet_state_tick(s, 40000); /* 相位 39000：第 1 次 = Look → 睁眼 */
    CHECK(view_frame() == PET_FRAME_NORMAL);
}

static void working_never_blinks(void)
{
    apply_kind(PET_EV_TASK_START, 1000);

    keepalive(1900);
    pet_state_tick(s, 1900); /* 相位 900：WORKING 不眨眼 */
    CHECK(view_frame() == PET_FRAME_NORMAL);

    keepalive(10000);
    pet_state_tick(s, 10000);
    keepalive(20000);
    pet_state_tick(s, 20000);
    CHECK(view_frame() == PET_FRAME_NORMAL);
}

static void done_is_happy_during_dwell(void)
{
    apply_kind(PET_EV_TASK_START, 1000);
    apply_kind(PET_EV_TASK_DONE, 2000);

    pet_state_tick(s, 2000);
    CHECK(view_frame() == PET_FRAME_HAPPY);

    pet_state_tick(s, 2000 + 4999); /* 驻留期内仍为 HAPPY */
    CHECK(view_frame() == PET_FRAME_HAPPY);
}

int main(void)
{
    s = pet_state_create();
    fresh_is_idle_normal_no_sound();
    pet_state_destroy(s);

    s = pet_state_create();
    task_start_is_working_silent();
    pet_state_destroy(s);

    s = pet_state_create();
    input_required_announces_once();
    pet_state_destroy(s);

    s = pet_state_create();
    explicit_done_announces_and_is_happy();
    pet_state_destroy(s);

    s = pet_state_create();
    task_error_and_blocked_are_failed_sad();
    pet_state_destroy(s);

    s = pet_state_create();
    idle_edge_becomes_done_after_debounce();
    pet_state_destroy(s);

    s = pet_state_create();
    retry_during_debounce_cancels_done();
    pet_state_destroy(s);

    s = pet_state_create();
    idle_from_idle_stays_idle_silent();
    pet_state_destroy(s);

    s = pet_state_create();
    done_returns_to_idle_after_dwell();
    pet_state_destroy(s);

    s = pet_state_create();
    link_timeout_goes_offline_gray();
    pet_state_destroy(s);

    s = pet_state_create();
    link_up_restores_previous_state();
    pet_state_destroy(s);

    s = pet_state_create();
    activity_refreshes_link_timer();
    pet_state_destroy(s);

    s = pet_state_create();
    idle_blinks_periodically();
    pet_state_destroy(s);

    s = pet_state_create();
    idle_naps_in_mood_window();
    pet_state_destroy(s);

    s = pet_state_create();
    working_never_blinks();
    pet_state_destroy(s);

    s = pet_state_create();
    done_is_happy_during_dwell();
    pet_state_destroy(s);

    REPORT();
}
