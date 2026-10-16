/* M5 增量 1：语音逻辑（映射 / 静音）。
 * 契约：contracts/M5-语音契约清单.md §5。
 * 资产与设备播放属增量 2（需 es8311 组件 + I2S TX）。 */
#include "pet_voice.h"

/* 静音开关：**不持久化**（重启恢复有声，与上游一致） */
static bool s_muted;

pet_voice_id_t pet_voice_id_for(pet_sound_t sound)
{
    switch (sound) {
    case PET_SOUND_INPUT:
        return PET_VOICE_INPUT;
    case PET_SOUND_DONE:
        return PET_VOICE_DONE;
    case PET_SOUND_FAILED:
        return PET_VOICE_FAILED;
    case PET_SOUND_NONE:
    default:
        return PET_VOICE_NONE;
    }
}

bool pet_voice_should_play(pet_sound_t sound, bool muted)
{
    if (muted) {
        return false;
    }
    return pet_voice_id_for(sound) != PET_VOICE_NONE;
}

void pet_voice_set_muted(bool muted)
{
    s_muted = muted;
}

bool pet_voice_muted(void)
{
    return s_muted;
}
