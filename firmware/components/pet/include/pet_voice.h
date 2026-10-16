#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pet_types.h"

/* M5 增量 1：语音逻辑（映射 / 静音）。
 * 契约：contracts/M5-语音契约清单.md §5。资产与播放属增量 2。 */

typedef enum {
    PET_VOICE_NONE = 0,
    PET_VOICE_INPUT,
    PET_VOICE_DONE,
    PET_VOICE_FAILED,
    PET_VOICE_COUNT
} pet_voice_id_t;

/* 播报 → 资产 id；PET_SOUND_NONE 或未知 → PET_VOICE_NONE */
pet_voice_id_t pet_voice_id_for(pet_sound_t sound);

/* 是否应当播放：NONE → false；静音 → false；其余 → true */
bool pet_voice_should_play(pet_sound_t sound, bool muted);

/* 静音开关（**不持久化**，重启恢复有声） */
void pet_voice_set_muted(bool muted);
bool pet_voice_muted(void);
