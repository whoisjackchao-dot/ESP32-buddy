#pragma once

#include "pet_button.h"
#include "pet_pomo.h"

/* 粘合层（不是第 4 层）：把 pet_state / pet_render / pet_hal 接起来。 */

void pet_engine_init(void);
void pet_engine_redraw(void);

/* 启动动画节拍任务：按 PET_FRAME_MS_DUTY 周期 tick + 重绘 + 取走待播。 */
void pet_engine_start(void);

/* M7 增量 2：值班 / 番茄钟 两个模式（切换由 M6 按键调用；默认 DUTY）。 */
typedef enum {
    PET_MODE_DUTY = 0,
    PET_MODE_POMO
} pet_mode_t;

void pet_engine_set_mode(pet_mode_t mode);
pet_mode_t pet_engine_mode(void);

/* M6：按键把番茄钟命令转给引擎持有的 pet_pomo。 */
void pet_engine_pomo_command(pet_pomo_cmd_t cmd);

/* M6 增量 2：向主机发"回窗口"控制线（契约 M6 §8.1）。 */
void pet_engine_request_back_window(void);

/* M6 增量 3：按键事件入口（菜单优先；否则按 Q11 映射分派）。 */
void pet_engine_key(pet_key_t key, pet_key_event_t ev);

/* 取引擎持有的状态机句柄（供 pet_rx 把事件喂进来）。 */
struct pet_state *pet_engine_state(void);
