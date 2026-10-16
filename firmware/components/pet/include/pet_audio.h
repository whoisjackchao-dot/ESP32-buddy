#pragma once

#include <stdbool.h>
#include "esp_err.h"

#include "pet_voice.h"

/* M5 增量 2：设备侧语音播放（I2S TX + ES8311）。
 * 复用同作者既有 StickS3 项目（MIT）中已在实机验证的实现与参数：
 *   16 kHz / 16-bit / **STEREO 槽位**（MONO 会导致 WS 变 32kHz → 2 倍拉伸）、
 *   MCLK×256、I2S_NUM_0、MCLK=18 BCLK=17 WS=15 DOUT=14。
 * **异步**：入队即返回，由独立任务播放（不阻塞动画节拍）。 */

/* 前置：I2C 已安装（pet_hal_sticks3 负责）、PM1 的 L3B 已上电。 */
esp_err_t pet_audio_init(void);

/* 入队一次播报；队列满返回 ESP_ERR_NO_MEM（丢弃而非阻塞）。 */
esp_err_t pet_audio_play_voice(pet_voice_id_t id);

/* 是否正在播（含排队）。 */
bool pet_audio_busy(void);
