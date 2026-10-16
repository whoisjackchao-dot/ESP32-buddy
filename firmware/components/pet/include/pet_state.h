#pragma once
#include "pet_types.h"

/* 事件：采用 vibebuddy 事件模型（Q04）；线格式仍走本项目的 S3P/BLE */
typedef enum {
    PET_EV_AGENT_IDLE = 0,
    PET_EV_TASK_START,
    PET_EV_AGENT_INPUT_REQUIRED,
    PET_EV_TASK_DONE,
    PET_EV_TASK_ERROR,
    PET_EV_AGENT_BLOCKED,
    PET_EV_LINK_DOWN,
    PET_EV_LINK_UP,
    PET_EV_CARD,
    PET_EV_ANNOUNCEMENT
} pet_event_kind_t;

typedef struct {
    pet_event_kind_t kind;
    uint32_t         now_ms;
    const char      *title;
    const char      *project;
    pet_state_t      card_state;
    int32_t          elapsed_s;
} pet_event_t;

/* 不透明状态机句柄。
 * 注意：这里**只用结构体标签**、不 typedef —— 因为 `pet_state_t` 在 pet_types.h
 * 中已被用作 6 态枚举。2026-10-16 编译期发现该命名冲突，见 M1 契约「缺陷修订」。 */
struct pet_state;

struct pet_state *pet_state_create(void);
void pet_state_destroy(struct pet_state *s);

/* 收事件；不碰硬件，不画图。 */
void pet_state_apply(struct pet_state *s, const pet_event_t *ev);

/* 每帧推进（动画/计时/失联判定）。 */
void pet_state_tick(struct pet_state *s, uint32_t now_ms);

/* 取当前视图快照。 */
void pet_state_view(const struct pet_state *s, pet_view_t *out);

/* 取一次性播报，取走即清。 */
pet_sound_t pet_state_take_sound(struct pet_state *s);

/* 存/清任务卡（M4）：card 为 NULL 时清除。 */
void pet_state_set_card(struct pet_state *s, const pet_card_t *card);
