/* M5 T 类：语音映射 / 静音 + 占位资产健全性。
 * 契约：contracts/M5-语音契约清单.md §5。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "harness.h"
#include "pet_voice.h"

#define RATE 16000

static const char *ASSET_DIR = "firmware/components/pet/assets";

static void mapping_is_one_to_one(void)
{
    CHECK(pet_voice_id_for(PET_SOUND_NONE) == PET_VOICE_NONE);
    CHECK(pet_voice_id_for(PET_SOUND_INPUT) == PET_VOICE_INPUT);
    CHECK(pet_voice_id_for(PET_SOUND_DONE) == PET_VOICE_DONE);
    CHECK(pet_voice_id_for(PET_SOUND_FAILED) == PET_VOICE_FAILED);
    /* 未知值（越界）不得崩，且归为 NONE */
    CHECK(pet_voice_id_for((pet_sound_t)99) == PET_VOICE_NONE);
}

static void should_play_rules(void)
{
    CHECK(pet_voice_should_play(PET_SOUND_NONE, false) == false);  /* 无事件不播 */
    CHECK(pet_voice_should_play(PET_SOUND_NONE, true) == false);
    CHECK(pet_voice_should_play(PET_SOUND_INPUT, false) == true);
    CHECK(pet_voice_should_play(PET_SOUND_DONE, false) == true);
    CHECK(pet_voice_should_play(PET_SOUND_FAILED, false) == true);
    CHECK(pet_voice_should_play(PET_SOUND_INPUT, true) == false); /* 静音 */
    CHECK(pet_voice_should_play(PET_SOUND_DONE, true) == false);
    CHECK(pet_voice_should_play(PET_SOUND_FAILED, true) == false);
}

static void mute_is_toggleable_and_default_off(void)
{
    CHECK(pet_voice_muted() == false); /* 默认有声 */
    pet_voice_set_muted(true);
    CHECK(pet_voice_muted() == true);
    pet_voice_set_muted(false);
    CHECK(pet_voice_muted() == false);
}

/* ---- 占位资产健全性（直接读文件，不经 pet_voice） ---- */

static long load_asset(const char *name, int16_t **out)
{
    char path[256];
    snprintf(path, sizeof path, "%s/%s", ASSET_DIR, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "cannot open asset: %s\n", path);
        *out = NULL;
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    int16_t *buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        *out = NULL;
        return -1;
    }
    fclose(f);
    *out = buf;
    return n;
}

static void assets_are_valid_pcm(void)
{
    const char *names[] = {"input.pcm", "done.pcm", "failed.pcm"};
    for (int i = 0; i < 3; i++) {
        int16_t *buf = NULL;
        const long bytes = load_asset(names[i], &buf);
        CHECK(bytes > 0);
        if (bytes <= 0) {
            continue;
        }
        CHECK(bytes % 2 == 0); /* 16-bit 对齐 */
        const double secs = (double)(bytes / 2) / RATE;
        CHECK(secs >= 0.1 && secs <= 3.0); /* 契约 §5.1 时长范围 */

        int peak = 0;
        for (long k = 0; k < bytes / 2; k++) {
            const int v = buf[k] < 0 ? -buf[k] : buf[k];
            if (v > peak) {
                peak = v;
            }
        }
        CHECK(peak > 1000); /* 不是静音 */
        free(buf);
    }
}

int main(void)
{
    RUN(mapping_is_one_to_one);
    RUN(should_play_rules);
    RUN(mute_is_toggleable_and_default_off);
    RUN(assets_are_valid_pcm);
    REPORT();
}
