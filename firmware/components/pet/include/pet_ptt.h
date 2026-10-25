#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* PTT 语音输入：KEY1 按住录音（ES8311 麦/I2S RX，16kHz/16bit），
 * 松开后打包 WAV 上传智谱 ASR（HTTPS multipart），文本回屏并作为
 * 控制线 {"v":1,"cmd":"voice.text",...} 发给主机。
 * 独立任务承载录音+上传，绝不阻塞按键/动画/引擎任务。
 * 失败只置 ERROR 态并记 ASCII 日志，不崩；ASR key 存 NVS（petasr/key）。 */

typedef enum {
    PET_PTT_IDLE = 0,    /* 空闲（缓冲已释放） */
    PET_PTT_RECORDING,   /* 按住录音中 */
    PET_PTT_UPLOADING,   /* 上传/等待 ASR */
    PET_PTT_DONE,        /* 成功，text 有效（约 5s 后自动回 IDLE） */
    PET_PTT_ERROR        /* 失败（约 5s 后自动回 IDLE） */
} pet_ptt_state_t;

/* 启动 PTT 任务 + 从 NVS 载入 ASR key。 */
void pet_ptt_init(void);

/* KEY1 按下/松开沿（引擎直通，不进键表）。菜单打开时引擎不转发。 */
void pet_ptt_on_key(bool pressed);

pet_ptt_state_t pet_ptt_state(void);

/* PET_PTT_DONE 期间的转写文本（UTF-8，NUL 结尾）；其余状态返回空串。 */
const char *pet_ptt_text(void);

/* PET_PTT_RECORDING 期间已录时长（ms）；其余状态 0。供 "REC <s>s" 显示。 */
uint32_t pet_ptt_elapsed_ms(void);

/* ---- ASR key 供给（serial cmd 路由 pet_net_handle_line 调用）----
 * NVS namespace "petasr" key "key"；key 只存 NVS/内存，绝不进日志。 */
void pet_ptt_key_set(const char *key);
void pet_ptt_key_clear(void);
bool pet_ptt_key_present(void);

/* ASR 端点覆盖（NVS petasr/url；默认智谱；用于本机 mock/换供应商）。 */
void pet_ptt_url_set(const char *url);
