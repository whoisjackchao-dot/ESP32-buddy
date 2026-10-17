#pragma once

#include "pet_pomo.h"

/* M7 增量 2：番茄钟当日统计的 NVS 落盘（设备侧）。
 * 契约：contracts/M7-番茄钟契约清单.md §7.4。格式（§7.3）由 pet_pomo 的编解码提供。 */

void pet_pomo_nvs_load(pet_pomo_t *p);
void pet_pomo_nvs_save(const pet_pomo_t *p);
