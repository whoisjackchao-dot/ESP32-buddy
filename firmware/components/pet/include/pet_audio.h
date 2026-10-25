#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#include "pet_voice.h"

/* M5 增量 2：设备侧语音播放（I2S TX + ES8311）。
 * 复用同作者既有 StickS3 项目（MIT）中已在实机验证的实现与参数：
 *   16 kHz / 16-bit / **STEREO 槽位**（MONO 会导致 WS 变 32kHz → 2 倍拉伸）、
 *   MCLK×256、I2S_NUM_0、MCLK=18 BCLK=17 WS=15 DOUT=14 DIN=16。
 * **异步**：入队即返回，由独立任务播放（不阻塞动画节拍）。
 * PTT 增量：同通道补 RX（全双工，同 std 配置），供 pet_ptt 采音。 */

/* 前置：I2C 已安装（pet_hal_sticks3 负责）、PM1 的 L3B 已上电。 */
esp_err_t pet_audio_init(void);

/* 入队一次播报；队列满返回 ESP_ERR_NO_MEM（丢弃而非阻塞）。 */
esp_err_t pet_audio_play_voice(pet_voice_id_t id);

/* 是否正在播（含排队）。 */
bool pet_audio_busy(void);

/* 读一帧麦克风采样：I2S 为 STEREO 槽位，此处读立体声、取左声道，
 * 向 mono 写入至多 samples 个单声道样本，返回实际样本数（0 = 超时/未就绪）。
 * samples 上限 960（60ms @16kHz）。阻塞至多 timeout_ms。 */
size_t pet_audio_mic_read(int16_t *mono, size_t samples, uint32_t timeout_ms);

/* 麦克风通路开关。首次 enable 做 ES8311 ADC（模拟麦）配置 + 24dB 增益；
 * 幂等：重复 enable/disable 均安全。false 关 RX 通道省 DMA。 */
esp_err_t pet_audio_mic_enable(bool on);
