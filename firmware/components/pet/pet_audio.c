/* M5 增量 2：设备侧语音播放（I2S TX + ES8311）。
 * 参数与流程复用同作者既有 StickS3 项目（MIT）中已在实机验证的实现：
 *   - 16 kHz / 16-bit / STEREO 槽位（MONO 会让 WS=BCLK/16=32kHz → 2 倍拉伸）
 *   - MCLK×256；I2S_NUM_0 master；DMA 6×240
 *   - 播放前 pm1_es8311(true) 开功放、解除静音；播完加静音尾再静音并关功放
 *     （避免停在方波沿产生持续电平 / 残留长鸣）
 *   - I2C 由 pet_hal_sticks3 安装（本模块不重复安装）
 * PTT 增量：同 I2S_NUM_0 补 RX（全双工，同 std 配置，DIN=GPIO16），
 *   RX 在 init 即使能（DMA 常流，读时取左声道）；ES8311 麦克风 ADC
 *   （模拟麦 + 24dB）由 pet_audio_mic_enable(true) 首次使能时配置。
 */
#include "pet_audio.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "es8311.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "pm1.h"

static const char *TAG = "pet-audio";

#define AUDIO_I2S_MCLK  GPIO_NUM_18
#define AUDIO_I2S_BCLK  GPIO_NUM_17
#define AUDIO_I2S_WS    GPIO_NUM_15
#define AUDIO_I2S_DOUT  GPIO_NUM_14
#define AUDIO_I2S_DIN   GPIO_NUM_16
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_MCLK_MULTIPLE 256u

#define QUEUE_DEPTH 4
#define CHUNK_FRAMES 480 /* 30ms @16k */
#define MIC_CHUNK_MAX 960 /* 60ms @16k（一次 mic_read 上限） */

/* 内嵌语音（EMBED_FILES：assets/{input,done,failed}.pcm） */
extern const uint8_t input_pcm_start[]  asm("_binary_input_pcm_start");
extern const uint8_t input_pcm_end[]    asm("_binary_input_pcm_end");
extern const uint8_t done_pcm_start[]   asm("_binary_done_pcm_start");
extern const uint8_t done_pcm_end[]     asm("_binary_done_pcm_end");
extern const uint8_t failed_pcm_start[] asm("_binary_failed_pcm_start");
extern const uint8_t failed_pcm_end[]   asm("_binary_failed_pcm_end");

typedef struct {
    const uint8_t *data;
    size_t         bytes;
} sample_t;

static sample_t sample_of(pet_voice_id_t id)
{
    switch (id) {
    case PET_VOICE_INPUT:
        return (sample_t){input_pcm_start, (size_t)(input_pcm_end - input_pcm_start)};
    case PET_VOICE_DONE:
        return (sample_t){done_pcm_start, (size_t)(done_pcm_end - done_pcm_start)};
    case PET_VOICE_FAILED:
        return (sample_t){failed_pcm_start, (size_t)(failed_pcm_end - failed_pcm_start)};
    default:
        return (sample_t){NULL, 0};
    }
}

static i2s_chan_handle_t s_tx;
static i2s_chan_handle_t s_rx;
static es8311_handle_t   s_codec;
static QueueHandle_t     s_q;
static volatile bool     s_playing;
static volatile bool     s_rx_on;
static bool              s_mic_cfg_done; /* ES8311 ADC 只配一次 */

static esp_err_t write_mono(const int16_t *mono, size_t samples)
{
    if (s_tx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    static int16_t stereo[2 * CHUNK_FRAMES];
    size_t done = 0;
    while (done < samples) {
        size_t n = samples - done;
        if (n > CHUNK_FRAMES) {
            n = CHUNK_FRAMES;
        }
        for (size_t i = 0; i < n; i++) {
            stereo[2 * i] = mono[done + i];
            stereo[2 * i + 1] = mono[done + i]; /* 左右相同（单声道源） */
        }
        size_t written = 0;
        const esp_err_t err = i2s_channel_write(s_tx, stereo,
                                                n * 2 * sizeof(int16_t),
                                                &written, pdMS_TO_TICKS(2000));
        if (err != ESP_OK) {
            return err;
        }
        done += n;
    }
    return ESP_OK;
}

static void play_now(pet_voice_id_t id)
{
    const sample_t s = sample_of(id);
    if (s.data == NULL || s.bytes < sizeof(int16_t)) {
        return;
    }

    (void)pm1_es8311(true); /* 功放仅在播报期间使能 */
    vTaskDelay(pdMS_TO_TICKS(10));
    if (s_codec != NULL) {
        es8311_voice_volume_set(s_codec, 70, NULL);
        es8311_voice_mute(s_codec, false);
    }

    (void)write_mono((const int16_t *)s.data, s.bytes / sizeof(int16_t));

    /* 静音尾：防止 I2S 停在波形沿上产生持续电平 */
    static const int16_t silence[320] = {0};
    (void)write_mono(silence, sizeof(silence) / sizeof(silence[0]));

    if (s_codec != NULL) {
        es8311_voice_mute(s_codec, true);
    }
    (void)pm1_es8311(false);
}

static void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        pet_voice_id_t id;
        if (xQueueReceive(s_q, &id, portMAX_DELAY) == pdTRUE) {
            s_playing = true;
            play_now(id);
            s_playing = false;
        }
    }
}

esp_err_t pet_audio_init(void)
{
    /* I2S TX+RX 同通道全双工：STEREO 槽位（见文件头注释） */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 240;
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx, &s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = AUDIO_I2S_MCLK,
            .bclk = AUDIO_I2S_BCLK,
            .ws = AUDIO_I2S_WS,
            .dout = AUDIO_I2S_DOUT,
            .din = AUDIO_I2S_DIN,
            .invert_flags = {false, false, false},
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    err = i2s_channel_init_std_mode(s_tx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx init: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_enable(s_tx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s tx enable: %s", esp_err_to_name(err));
        return err;
    }

    /* RX：同 std 配置（同 BCLK/WS/DIN16），init 即使能（参考实机同款做法） */
    err = i2s_channel_init_std_mode(s_rx, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s rx init: %s", esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_enable(s_rx);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s rx enable: %s", esp_err_to_name(err));
        return err;
    }
    s_rx_on = true;

    /* ES8311（地址 0x18；L3B 已由上电流程使能） */
    s_codec = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
    if (s_codec == NULL) {
        ESP_LOGE(TAG, "es8311_create 失败");
        return ESP_FAIL;
    }
    const es8311_clock_config_t clk_cfg = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = AUDIO_MCLK_MULTIPLE * AUDIO_SAMPLE_RATE,
        .sample_frequency = AUDIO_SAMPLE_RATE,
    };
    err = es8311_init(s_codec, &clk_cfg, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "es8311_init: %s", esp_err_to_name(err));
        es8311_delete(s_codec);
        s_codec = NULL;
        return err;
    }
    es8311_voice_volume_set(s_codec, 70, NULL);
    es8311_voice_mute(s_codec, true); /* 空闲保持静音 */

    s_q = xQueueCreate(QUEUE_DEPTH, sizeof(pet_voice_id_t));
    if (s_q == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(audio_task, "pet-audio", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "audio 任务创建失败");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "AUDIO READY (ES8311, 16kHz/STEREO, MCLKx%u)", AUDIO_MCLK_MULTIPLE);
    return ESP_OK;
}

esp_err_t pet_audio_play_voice(pet_voice_id_t id)
{
    if (s_q == NULL || id == PET_VOICE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    return (xQueueSend(s_q, &id, 0) == pdTRUE) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool pet_audio_busy(void)
{
    return s_playing || (s_q != NULL && uxQueueMessagesWaiting(s_q) > 0);
}

size_t pet_audio_mic_read(int16_t *mono, size_t samples, uint32_t timeout_ms)
{
    if (s_rx == NULL || mono == NULL || samples == 0 || !s_rx_on) {
        return 0;
    }
    if (samples > MIC_CHUNK_MAX) {
        samples = MIC_CHUNK_MAX;
    }
    static int16_t stereo[2 * MIC_CHUNK_MAX]; /* STEREO 槽位：读立体声取左 */
    size_t bytes_read = 0;
    const esp_err_t err = i2s_channel_read(s_rx, stereo,
                                           samples * 2 * sizeof(int16_t),
                                           &bytes_read,
                                           pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK && bytes_read == 0) {
        return 0;
    }
    const size_t frames = bytes_read / (2 * sizeof(int16_t));
    for (size_t i = 0; i < frames; i++) {
        mono[i] = stereo[2 * i];
    }
    return frames;
}

esp_err_t pet_audio_mic_enable(bool on)
{
    if (s_rx == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (on) {
        if (s_codec != NULL && !s_mic_cfg_done) {
            /* 参考实机：模拟麦（digital=false）+ 24dB；数字麦配置会采不到有效信号 */
            esp_err_t err = es8311_microphone_config(s_codec, false);
            if (err == ESP_OK) {
                err = es8311_microphone_gain_set(s_codec, ES8311_MIC_GAIN_24DB);
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "es8311 mic cfg: %s", esp_err_to_name(err));
                return err;
            }
            s_mic_cfg_done = true;
        }
        if (!s_rx_on) {
            const esp_err_t err = i2s_channel_enable(s_rx);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "i2s rx enable: %s", esp_err_to_name(err));
                return err;
            }
            s_rx_on = true;
        }
        return ESP_OK;
    }
    if (s_rx_on) {
        const esp_err_t err = i2s_channel_disable(s_rx);
        if (err != ESP_OK) {
            return err;
        }
        s_rx_on = false;
    }
    return ESP_OK;
}
