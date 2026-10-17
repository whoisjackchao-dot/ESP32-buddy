#pragma once

/* M6 增量 1：两个物理键的采样 + 短/长按分派（GPIO 在此，纯逻辑在 pet_button/pet_keymap）。
 * 契约：contracts/M6-按键映射契约清单.md §7。 */

void pet_keys_start(void);
