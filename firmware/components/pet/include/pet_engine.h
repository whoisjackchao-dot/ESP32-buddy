#pragma once

/* 粘合层（不是第 4 层）：把 pet_state / pet_render / pet_hal 接起来。 */

void pet_engine_init(void);
void pet_engine_redraw(void);

/* 启动动画节拍任务：按 PET_FRAME_MS_DUTY 周期 tick + 重绘 + 取走待播。 */
void pet_engine_start(void);
