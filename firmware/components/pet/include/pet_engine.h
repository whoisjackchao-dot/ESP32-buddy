#pragma once

/* 粘合层（不是第 4 层）：把 pet_state / pet_render / pet_hal 接起来。
 * M1 只做：初始化 + 画一帧。 */

void pet_engine_init(void);
void pet_engine_redraw(void);
