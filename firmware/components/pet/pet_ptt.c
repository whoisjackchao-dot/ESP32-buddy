/* PTT 语音输入（KEY1 按住录音 / 松开上传智谱 ASR）。
 * 采音参数复用同作者 StickS3 实机验证结论（MIT）：
 *   60ms 帧 = 960 样本 @16kHz；I2S STEREO 槽位取左声道（见 pet_audio.c）；
 *   ES8311 模拟麦 + 24dB（pet_audio_mic_enable）。
 * 状态机：IDLE → RECORDING →（≥300ms）UPLOADING → DONE/ERROR →（~5s）IDLE。
 * 30s 硬顶到时视同松键；PSRAM 不足降 10s；缓冲用完即释放。
 * 上传：esp_http_client + 证书捆绑（esp_crt_bundle_attach）流式写
 * multipart/form-data（不复制大缓冲）；响应 JSON 取 "text"（\uXXXX 转义感知）。
 * key 供给见 pet_ptt.h；key 只入 NVS 与内存，日志永不回显。 */
#include "pet_ptt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "pet_audio.h"
#include "pet_engine.h"

#define PTT_REC_RATE       16000
#define PTT_FRAME_SAMPLES  960       /* 60ms @16kHz */
#define PTT_MAX_SEC        30        /* 硬顶（缓冲按此分配） */
#define PTT_FALLBACK_SEC   10        /* PSRAM 不足时的降级时长 */
#define PTT_MIN_MS         300       /* 短于此丢弃 */
#define PTT_BUF_BYTES(sec) ((size_t)(sec) * PTT_REC_RATE * 2)
#define PTT_WAV_HDR        44
#define PTT_DONE_SHOW_MS   5000u
#define PTT_MIC_TIMEOUT_MS 200
#define PTT_MIC_MISS_MAX   10        /* 连续空读 ≈2s 视为通路故障 */
#define PTT_TEXT_MAX       512
#define PTT_RESP_MAX       1024
#define PTT_KEY_MAX        129
#define PTT_TASK_STACK     12288     /* esp-tls 握手峰值所在任务，实机待核 */

#define ASR_URL "https://open.bigmodel.cn/api/paas/v4/audio/transcriptions"
#define ASR_BOUNDARY "petptt-b7f31c9a"

static const char *TAG = "pet-ptt";

static const char PART1[] =
    "--" ASR_BOUNDARY "\r\n"
    "Content-Disposition: form-data; name=\"model\"\r\n"
    "\r\n"
    "glm-asr-2512\r\n"
    "--" ASR_BOUNDARY "\r\n"
    "Content-Disposition: form-data; name=\"stream\"\r\n"
    "\r\n"
    "false\r\n"
    "--" ASR_BOUNDARY "\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
    "Content-Type: audio/wav\r\n"
    "\r\n";
static const char TAIL[] = "\r\n--" ASR_BOUNDARY "--\r\n";

static TaskHandle_t s_task;
static volatile pet_ptt_state_t s_state = PET_PTT_IDLE;
static uint8_t *s_buf;                 /* PSRAM：44B 头 + PCM */
static size_t   s_buf_pcm_cap;         /* 本次分配可容纳的 PCM 字节 */
static volatile bool  s_stop_req;
static size_t   s_pcm_bytes;
static uint32_t s_rec_start_ms;
static char     s_text[PTT_TEXT_MAX];
static uint32_t s_done_at_ms;
static char     s_asr_key[PTT_KEY_MAX];
static char     s_asr_url[192] = ASR_URL; /* 可经 asr.set 覆盖（mock/换供应商） */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---- WAV 头（16kHz / mono / 16bit PCM，44 字节） ---- */
static void w16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void w32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void wav_fill_header(uint8_t *h, uint32_t data_len)
{
    memcpy(h, "RIFF", 4);
    w32(h + 4, 36 + data_len);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    w32(h + 16, 16);
    w16(h + 20, 1); /* PCM */
    w16(h + 22, 1); /* mono */
    w32(h + 24, PTT_REC_RATE);
    w32(h + 28, PTT_REC_RATE * 2);
    w16(h + 32, 2);
    w16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    w32(h + 40, data_len);
}

/* ---- 极小 JSON 字符串取值（移植自 StickS3 asr_adapter.c，实机验证过）：
 * 找 "text"，跳过 ws/:，取带引号字符串；\" \\ \/ \b\f\n\r\t 与 \uXXXX
 * （含代理对）均解为 UTF-8 字节。失败/截断返回 false，不越界。 ---- */
static bool json_text_field(const char *body, size_t len, char *out, size_t cap)
{
    const char pat[] = "\"text\"";
    size_t i = 0;
    bool found = false;
    while (i + sizeof(pat) - 1 <= len) {
        if (body[i] == '"' && memcmp(body + i, pat, sizeof(pat) - 1) == 0) {
            i += sizeof(pat) - 1;
            found = true;
            break;
        }
        i++;
    }
    if (!found) {
        return false;
    }
    while (i < len && (body[i] == ' ' || body[i] == '\t' ||
                       body[i] == '\r' || body[i] == '\n')) {
        i++;
    }
    if (i >= len || body[i] != ':') {
        return false;
    }
    i++;
    while (i < len && (body[i] == ' ' || body[i] == '\t' ||
                       body[i] == '\r' || body[i] == '\n')) {
        i++;
    }
    if (i >= len || body[i] != '"') {
        return false;
    }
    i++;
    size_t o = 0;
    while (i < len) {
        unsigned char c = (unsigned char)body[i];
        if (c == '"') {
            out[o] = '\0';
            return true;
        }
        if (c != '\\') {
            if (o + 1 >= cap) {
                return false;
            }
            out[o++] = (char)c;
            i++;
            continue;
        }
        if (i + 1 >= len) {
            return false;
        }
        i++;
        char e = body[i];
        if (e == 'u') {
            if (i + 4 >= len) {
                return false;
            }
            unsigned cp = 0;
            for (int k = 1; k <= 4; k++) {
                char h = body[i + k];
                unsigned v;
                if (h >= '0' && h <= '9') {
                    v = (unsigned)(h - '0');
                } else if (h >= 'a' && h <= 'f') {
                    v = (unsigned)(h - 'a' + 10);
                } else if (h >= 'A' && h <= 'F') {
                    v = (unsigned)(h - 'A' + 10);
                } else {
                    return false;
                }
                cp = (cp << 4) | v;
            }
            i += 5;
            /* 高代理 + \u低代理 → 合成；孤代理 → U+FFFD */
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len &&
                body[i] == '\\' && body[i + 1] == 'u') {
                unsigned lo = 0;
                bool ok = true;
                if (i + 5 >= len) {
                    return false;
                }
                for (int k = 2; k <= 5; k++) {
                    char h = body[i + k];
                    unsigned v;
                    if (h >= '0' && h <= '9') {
                        v = (unsigned)(h - '0');
                    } else if (h >= 'a' && h <= 'f') {
                        v = (unsigned)(h - 'a' + 10);
                    } else if (h >= 'A' && h <= 'F') {
                        v = (unsigned)(h - 'A' + 10);
                    } else {
                        ok = false;
                        break;
                    }
                    lo = (lo << 4) | v;
                }
                if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                } else {
                    cp = 0xFFFD;
                }
            } else if (cp >= 0xD800 && cp <= 0xDFFF) {
                cp = 0xFFFD;
            }
            uint8_t u[4];
            size_t un;
            if (cp < 0x80) {
                u[0] = (uint8_t)cp;
                un = 1;
            } else if (cp < 0x800) {
                u[0] = (uint8_t)(0xC0 | (cp >> 6));
                u[1] = (uint8_t)(0x80 | (cp & 0x3F));
                un = 2;
            } else if (cp < 0x10000) {
                u[0] = (uint8_t)(0xE0 | (cp >> 12));
                u[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                u[2] = (uint8_t)(0x80 | (cp & 0x3F));
                un = 3;
            } else {
                u[0] = (uint8_t)(0xF0 | (cp >> 18));
                u[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
                u[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
                u[3] = (uint8_t)(0x80 | (cp & 0x3F));
                un = 4;
            }
            if (o + un >= cap) {
                return false;
            }
            memcpy(out + o, u, un);
            o += un;
            continue;
        }
        char lit;
        if (e == '"' || e == '\\' || e == '/') {
            lit = e;
        } else if (e == 'b') {
            lit = '\b';
        } else if (e == 'f') {
            lit = '\f';
        } else if (e == 'n') {
            lit = '\n';
        } else if (e == 'r') {
            lit = '\r';
        } else if (e == 't') {
            lit = '\t';
        } else {
            return false;
        }
        if (o + 1 >= cap) {
            return false;
        }
        out[o++] = lit;
        i++;
    }
    return false; /* 无闭合引号 */
}

/* ---- 控制线（与 back_window 同一 USB 写入口） ---- */
static void send_voice_text(const char *text)
{
    static char esc[PTT_TEXT_MAX * 2 + 16];
    size_t o = 0;
    for (const char *p = text; *p != '\0' && o < sizeof(esc) - 7; p++) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            esc[o++] = '\\';
            esc[o++] = (char)c;
        } else if (c == '\n') {
            esc[o++] = '\\';
            esc[o++] = 'n';
        } else if (c == '\r') {
            esc[o++] = '\\';
            esc[o++] = 'r';
        } else if (c == '\t') {
            esc[o++] = '\\';
            esc[o++] = 't';
        } else if (c >= 0x20) {
            esc[o++] = (char)c; /* 其余控制字节丢弃；UTF-8 原样透传 */
        }
    }
    esc[o] = '\0';

    static char line[sizeof(esc) + 48];
    const int n = snprintf(line, sizeof(line),
                           "{\"v\":1,\"cmd\":\"voice.text\",\"text\":\"%s\"}\n", esc);
    if (n > 0) {
        pet_engine_send_line(line);
    }
}

/* ---- 上传（PTT 任务上下文；返回 HTTP 码，<0 = 传输失败） ---- */
static int asr_post(const char *key)
{
    char auth[PTT_KEY_MAX + 24];
    snprintf(auth, sizeof(auth), "Bearer %s", key);

    const size_t wav_len = PTT_WAV_HDR + s_pcm_bytes;
    const size_t content_len = sizeof(PART1) - 1 + wav_len + sizeof(TAIL) - 1;

    esp_http_client_config_t cfg = {
        .url = s_asr_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    if (strncmp(s_asr_url, "https://", 8) == 0) {
        cfg.crt_bundle_attach = esp_crt_bundle_attach; /* 明文 http（本机 mock）不挂证书 */
    }
    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) {
        return -1;
    }
    esp_http_client_set_header(cli, "Authorization", auth);
    esp_http_client_set_header(cli, "Content-Type",
                               "multipart/form-data; boundary=" ASR_BOUNDARY);

    int status = -1;
    size_t off = 0; /* 流式写 body 的进度（在 open 块外也要用） */
    if (esp_http_client_open(cli, (int)content_len) == ESP_OK) {
        int w = esp_http_client_write(cli, PART1, (int)(sizeof(PART1) - 1));
        if (w >= 0) {
            while (off < wav_len) {
                size_t n = wav_len - off;
                if (n > 4096) {
                    n = 4096;
                }
                w = esp_http_client_write(cli, (const char *)s_buf + off, (int)n);
                if (w <= 0) { /* 0 = 无进展：防死循环 */
                    break;
                }
                off += (size_t)w;
            }
        }
        if (w >= 0 && off == wav_len) {
            w = esp_http_client_write(cli, TAIL, (int)(sizeof(TAIL) - 1));
        }
        if (w >= 0) {
            esp_http_client_fetch_headers(cli);
            status = esp_http_client_get_status_code(cli);
        }
    }

    if (status == 200) {
        char resp[PTT_RESP_MAX]; /* 文本超长本就截断（屏显 ≤48B / 控制线 ≤512B） */
        size_t total = 0;
        int r;
        while ((r = esp_http_client_read(cli, resp + total,
                                         (int)(sizeof(resp) - 1 - total))) > 0) {
            total += (size_t)r;
            if (total >= sizeof(resp) - 1) {
                break;
            }
        }
        resp[total] = '\0';
        status = json_text_field(resp, total, s_text, sizeof(s_text)) ? 200 : -2;
    }
    esp_http_client_close(cli);
    esp_http_client_cleanup(cli);
    return status;
}

/* ---- 录音（PTT 任务上下文；返回是否成功收满/正常停） ---- */
static bool ptt_record(void)
{
    if (pet_audio_mic_enable(true) != ESP_OK) {
        ESP_LOGE(TAG, "voice: mic enable failed");
        return false;
    }
    s_pcm_bytes = 0;
    s_rec_start_ms = now_ms();
    static int16_t chunk[PTT_FRAME_SAMPLES];
    int misses = 0;
    bool ok = true;
    for (;;) {
        if (s_stop_req) {
            break;
        }
        const uint32_t elapsed = now_ms() - s_rec_start_ms;
        const uint32_t cap_ms =
            (uint32_t)(s_buf_pcm_cap / (PTT_REC_RATE * 2)) * 1000u;
        if (elapsed >= cap_ms || s_pcm_bytes >= s_buf_pcm_cap) {
            ESP_LOGI(TAG, "voice: cap reached (%ums)", (unsigned)elapsed);
            break; /* 硬顶视同松键 */
        }
        const size_t got = pet_audio_mic_read(chunk, PTT_FRAME_SAMPLES,
                                              PTT_MIC_TIMEOUT_MS);
        if (got == 0) {
            if (++misses >= PTT_MIC_MISS_MAX) {
                ESP_LOGE(TAG, "voice: mic timeout");
                ok = false;
                break;
            }
            continue;
        }
        misses = 0;
        size_t bytes = got * sizeof(int16_t);
        if (s_pcm_bytes + bytes > s_buf_pcm_cap) {
            bytes = s_buf_pcm_cap - s_pcm_bytes; /* 尾帧截齐到缓冲 */
        }
        memcpy(s_buf + PTT_WAV_HDR + s_pcm_bytes, chunk, bytes);
        s_pcm_bytes += bytes;
        if (s_pcm_bytes >= s_buf_pcm_cap) {
            break;
        }
    }
    (void)pet_audio_mic_enable(false);
    return ok;
}

static void run_session(void)
{
    s_stop_req = false;
    s_text[0] = '\0';

    /* PSRAM 分配：30s 顶格，不足降 10s，再不足直接 ERROR */
    s_buf = heap_caps_malloc(PTT_WAV_HDR + PTT_BUF_BYTES(PTT_MAX_SEC),
                             MALLOC_CAP_SPIRAM);
    if (s_buf != NULL) {
        s_buf_pcm_cap = PTT_BUF_BYTES(PTT_MAX_SEC);
    } else {
        s_buf = heap_caps_malloc(PTT_WAV_HDR + PTT_BUF_BYTES(PTT_FALLBACK_SEC),
                                 MALLOC_CAP_SPIRAM);
        if (s_buf == NULL) {
            ESP_LOGE(TAG, "voice: no spiram");
            s_state = PET_PTT_ERROR;
            s_done_at_ms = now_ms();
            return;
        }
        ESP_LOGW(TAG, "voice: spiram short, cap %ds", PTT_FALLBACK_SEC);
        s_buf_pcm_cap = PTT_BUF_BYTES(PTT_FALLBACK_SEC);
    }

    if (!ptt_record()) {
        s_state = PET_PTT_ERROR;
        s_done_at_ms = now_ms();
        free(s_buf);
        s_buf = NULL;
        return;
    }

    const uint32_t rec_ms = (uint32_t)(s_pcm_bytes / 2) * 1000u / PTT_REC_RATE;
    if (rec_ms < PTT_MIN_MS) {
        ESP_LOGI(TAG, "voice: too short");
        s_state = PET_PTT_IDLE;
        free(s_buf);
        s_buf = NULL;
        return;
    }

    s_state = PET_PTT_UPLOADING;
    wav_fill_header(s_buf, (uint32_t)s_pcm_bytes);

    /* key 快照（rx 任务可能并发改写；竞态最坏一次失败上传） */
    char key[PTT_KEY_MAX];
    strncpy(key, s_asr_key, sizeof(key) - 1);
    key[sizeof(key) - 1] = '\0';
    int status = (key[0] == '\0') ? 401 : asr_post(key);

    if (status == 200) {
        s_state = PET_PTT_DONE;
        s_done_at_ms = now_ms();
        ESP_LOGI(TAG, "voice: text=%s", s_text);
        send_voice_text(s_text);
    } else {
        s_state = PET_PTT_ERROR;
        s_done_at_ms = now_ms();
        ESP_LOGE(TAG, "voice: asr failed %d", status);
    }
    free(s_buf);
    s_buf = NULL;
}

static void ptt_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        run_session();
    }
}

void pet_ptt_init(void)
{
    nvs_handle_t h;
    if (nvs_open("petasr", NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(s_asr_key);
        if (nvs_get_str(h, "key", s_asr_key, &len) != ESP_OK) {
            s_asr_key[0] = '\0';
        }
        nvs_close(h);
    }
    if (xTaskCreate(ptt_task, "pet-ptt", PTT_TASK_STACK, NULL, 4, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "voice: task create failed");
        return;
    }
    {
        nvs_handle_t h;
        size_t len = sizeof(s_asr_url);
        if (nvs_open("petasr", NVS_READONLY, &h) == ESP_OK) {
            if (nvs_get_str(h, "url", s_asr_url, &len) != ESP_OK) {
                snprintf(s_asr_url, sizeof s_asr_url, "%s", ASR_URL);
            }
            nvs_close(h);
        }
    }
    ESP_LOGI(TAG, "voice: ptt ready key=%s url=%s", s_asr_key[0] != '\0' ? "set" : "missing", s_asr_url);
}

/* DONE/ERROR 展示期满 → 回 IDLE（懒回收；调用方任意任务） */
static pet_ptt_state_t refresh_state(void)
{
    pet_ptt_state_t st = s_state;
    if ((st == PET_PTT_DONE || st == PET_PTT_ERROR) &&
        now_ms() - s_done_at_ms >= PTT_DONE_SHOW_MS) {
        s_state = PET_PTT_IDLE;
        s_text[0] = '\0';
        st = PET_PTT_IDLE;
    }
    return st;
}

void pet_ptt_on_key(bool pressed)
{
    if (s_task == NULL) {
        return;
    }
    if (pressed) {
        if (refresh_state() != PET_PTT_IDLE) {
            return; /* 录/传中或展示未到期：忽略 */
        }
        s_state = PET_PTT_RECORDING;
        xTaskNotifyGive(s_task);
    } else if (s_state == PET_PTT_RECORDING) {
        s_stop_req = true;
    }
}

pet_ptt_state_t pet_ptt_state(void)
{
    return refresh_state();
}

const char *pet_ptt_text(void)
{
    return (s_state == PET_PTT_DONE) ? s_text : "";
}

uint32_t pet_ptt_elapsed_ms(void)
{
    return (s_state == PET_PTT_RECORDING) ? (now_ms() - s_rec_start_ms) : 0;
}

void pet_ptt_key_set(const char *key)
{
    if (key == NULL || key[0] == '\0' || strlen(key) >= sizeof(s_asr_key)) {
        ESP_LOGW(TAG, "asr: set invalid");
        return;
    }
    nvs_handle_t h;
    if (nvs_open("petasr", NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "asr: nvs open failed");
        return;
    }
    if (nvs_set_str(h, "key", key) != ESP_OK || nvs_commit(h) != ESP_OK) {
        ESP_LOGE(TAG, "asr: nvs write failed");
        nvs_close(h);
        return;
    }
    nvs_close(h);
    strcpy(s_asr_key, key); /* 不回显 */
    ESP_LOGI(TAG, "asr: key set");
}

void pet_ptt_key_clear(void)
{
    nvs_handle_t h;
    if (nvs_open("petasr", NVS_READWRITE, &h) == ESP_OK) {
        (void)nvs_erase_key(h, "key");
        (void)nvs_commit(h);
        nvs_close(h);
    }
    s_asr_key[0] = '\0';
    ESP_LOGI(TAG, "asr: key cleared");
}

bool pet_ptt_key_present(void)
{
    return s_asr_key[0] != '\0';
}

void pet_ptt_url_set(const char *url)
{
    if (url == NULL || strlen(url) < 8 || strlen(url) >= sizeof(s_asr_url)) {
        ESP_LOGW(TAG, "asr: url invalid");
        return;
    }
    nvs_handle_t h;
    if (nvs_open("petasr", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_set_str(h, "url", url);
    (void)nvs_commit(h);
    nvs_close(h);
    snprintf(s_asr_url, sizeof s_asr_url, "%s", url);
    ESP_LOGI(TAG, "asr: url=%s", s_asr_url);
}
