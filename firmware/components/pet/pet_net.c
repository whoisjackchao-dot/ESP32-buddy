/* M8 增量 2：WiFi STA + SNTP 授时。M5StickS3（ESP32-S3）2.4 GHz，仅 STA，无配网 UI。
 * 凭据只存 NVS（namespace petnet，键 ssid/pass），代码与日志均不出现明文口令。
 * 失败降级：无凭据 / 连不上 / SNTP 不通 → 保持运行，休闲 hour24 维持最后一次同步值。
 * 契约：evidence/2026-10-24-WiFi与NTP-设备.md */
#include "pet_net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include "pet_engine.h"

static const char *TAG = "pet-net";

#define NET_NVS_NS "petnet"
#define NET_SSID_MAX 33u  /* 802.11 SSID ≤32 字节 + NUL */
#define NET_PASS_MAX 65u  /* WPA 口令 ≤64 字节 + NUL */
#define NET_RESYNC_US 3600000000LL /* 每小时重算本地小时 */

/* 网络任务唯一持有 WiFi 状态；rx 任务只经 notify 触发重载 */
static TaskHandle_t s_task;
static char s_ssid[NET_SSID_MAX];
static char s_pass[NET_PASS_MAX];
static char s_ip[16]; /* "255.255.255.255" */
static volatile bool s_net_inited;
static volatile bool s_wifi_started;
static volatile bool s_wifi_run;    /* false = 已 clear，断开事件不再重连 */
static volatile bool s_reconnect_req;
static volatile uint32_t s_fails;   /* 连续失败次数 → 重试退避 */
static volatile int s_hour_synced = -1;
static int s_last_push = -1;

static esp_event_handler_instance_t s_ev_wifi;
static esp_event_handler_instance_t s_ev_ip;

/* ---- 极简 JSON 字符串取值：找 "key" 后跳空白，取带引号字符串，支持 \" 转义 ---- */
static bool json_string(const char *line, const char *key, char *out, size_t out_sz)
{
    char pat[32];
    if (snprintf(pat, sizeof pat, "\"%s\"", key) >= (int)sizeof pat) {
        return false;
    }
    const char *p = strstr(line, pat);
    if (p == NULL) {
        return false;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != ':') {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;
    size_t n = 0;
    while (*p != '\0' && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p != '\0') {
            c = *p++; /* \" \\ 等转义取字面量 */
        }
        if (n + 1 >= out_sz) {
            return false; /* 超长即拒（含 SSID>32 / 口令>64） */
        }
        out[n++] = c;
    }
    if (*p != '"') {
        return false;
    }
    out[n] = '\0';
    return true;
}

/* ---- NVS 凭据（namespace petnet：ssid / pass） ---- */
static bool nvs_load_creds(char *ssid, size_t ssid_sz, char *pass, size_t pass_sz)
{
    nvs_handle_t h;
    if (nvs_open(NET_NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = ssid_sz;
    const esp_err_t e1 = nvs_get_str(h, "ssid", ssid, &len);
    len = pass_sz;
    const esp_err_t e2 = nvs_get_str(h, "pass", pass, &len);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK;
}

static void nvs_save_creds(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NET_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "net: nvs open failed");
        return;
    }
    (void)nvs_set_str(h, "ssid", ssid);
    (void)nvs_set_str(h, "pass", pass);
    (void)nvs_commit(h);
    nvs_close(h);
}

static void nvs_clear_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NET_NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    (void)nvs_erase_all(h);
    (void)nvs_commit(h);
    nvs_close(h);
}

/* ---- SNTP：时区 CST-8，同步回调推本地小时给引擎 ---- */
static bool local_hour(int *out)
{
    time_t now = 0;
    time(&now);
    struct tm lt;
    localtime_r(&now, &lt);
    if (lt.tm_year < 2020 - 1900) {
        return false; /* 系统时间尚未被 SNTP 设置 */
    }
    *out = lt.tm_hour;
    return true;
}

static void push_hour(bool announce)
{
    int hour;
    if (!local_hour(&hour)) {
        return;
    }
    pet_engine_set_hour24(hour);
    s_hour_synced = hour;
    if (announce || hour != s_last_push) {
        ESP_LOGI(TAG, "net: sntp synced hour=%d", hour);
    }
    s_last_push = hour;
}

static void sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    push_hour(true); /* lwip 上下文：只做静态写入 + 日志 */
}

static void sntp_setup(void)
{
    if (esp_sntp_enabled()) {
        return;
    }
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "cn.pool.ntp.org");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_cb);
    setenv("TZ", "CST-8", 1);
    tzset();
    esp_sntp_init();
}

/* ---- 事件：断开只置标志（重连由网络任务退避执行），拿到 IP 才算连上 ---- */
static void net_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_run) {
            s_ip[0] = '\0';
            s_reconnect_req = true;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof s_ip, IPSTR, IP2STR(&ev->ip_info.ip));
        s_reconnect_req = false;
        s_fails = 0;
        ESP_LOGI(TAG, "net: wifi connected ip=%s", s_ip);
    }
}

static void creds_set(const char *ssid, const char *pass)
{
    strncpy(s_ssid, ssid, sizeof s_ssid - 1);
    s_ssid[sizeof s_ssid - 1] = '\0';
    strncpy(s_pass, pass, sizeof s_pass - 1);
    s_pass[sizeof s_pass - 1] = '\0';
}

/* 初始化（一次性）+ 下发凭据 + 启动/重连。任一步失败只记日志，不崩。 */
static void net_bring_up(void)
{
    if (!s_net_inited) {
        if (esp_netif_init() != ESP_OK) {
            ESP_LOGE(TAG, "net: netif init failed");
            return;
        }
        if (esp_event_loop_create_default() != ESP_OK) {
            ESP_LOGE(TAG, "net: event loop failed");
            return;
        }
        if (esp_netif_create_default_wifi_sta() == NULL) {
            ESP_LOGE(TAG, "net: netif sta failed");
            return;
        }
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&cfg) != ESP_OK) {
            ESP_LOGE(TAG, "net: wifi init failed");
            return;
        }
        if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                net_event_handler, NULL, &s_ev_wifi) != ESP_OK ||
            esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                net_event_handler, NULL, &s_ev_ip) != ESP_OK) {
            ESP_LOGE(TAG, "net: event register failed");
            return;
        }
        (void)esp_wifi_set_mode(WIFI_MODE_STA);
        (void)esp_wifi_set_storage(WIFI_STORAGE_RAM); /* 凭据只在我们自己的 NVS 命名空间 */
        s_net_inited = true;
    }

    wifi_config_t wcfg;
    memset(&wcfg, 0, sizeof wcfg);
    strncpy((char *)wcfg.sta.ssid, s_ssid, sizeof wcfg.sta.ssid - 1);
    strncpy((char *)wcfg.sta.password, s_pass, sizeof wcfg.sta.password - 1);
    if (esp_wifi_set_config(WIFI_IF_STA, &wcfg) != ESP_OK) {
        ESP_LOGE(TAG, "net: wifi config failed");
        return;
    }

    s_wifi_run = true;
    s_reconnect_req = false;
    s_fails = 0;
    sntp_setup();
    if (!s_wifi_started) {
        if (esp_wifi_start() != ESP_OK) {
            ESP_LOGE(TAG, "net: wifi start failed");
            s_wifi_run = false;
            return;
        }
        s_wifi_started = true;
    } else {
        /* 已在网：换凭据 → 断开事件触发任务用新配置重连 */
        (void)esp_wifi_disconnect();
    }
}

static void net_teardown(void)
{
    s_wifi_run = false; /* 先关重连开关，断开事件才不会置 retry 标志 */
    s_reconnect_req = false;
    if (s_wifi_started) {
        if (esp_sntp_enabled()) {
            esp_sntp_stop();
        }
        (void)esp_wifi_disconnect();
        (void)esp_wifi_stop();
        s_wifi_started = false;
    }
    s_ip[0] = '\0';
    ESP_LOGI(TAG, "net: wifi cleared"); /* 保留最后一次同步的 hour */
}

static void net_task(void *arg)
{
    (void)arg;
    char ssid[NET_SSID_MAX], pass[NET_PASS_MAX];
    bool have = nvs_load_creds(ssid, sizeof ssid, pass, sizeof pass);
    if (!have) {
        ESP_LOGW(TAG, "net: no credentials");
    }
    int64_t last_retry = 0;
    int64_t last_push = 0;

    for (;;) {
        if (!have) {
            /* 停车：无凭据绝不碰 WiFi，设备行为与此前一致；wifi.set 到来即唤醒 */
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            have = nvs_load_creds(ssid, sizeof ssid, pass, sizeof pass);
            if (have) {
                last_retry = 0;
                last_push = 0;
            }
            continue; /* clear 通知或仍无凭据：继续停车 */
        }
        creds_set(ssid, pass);
        net_bring_up();

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000))) {
            have = nvs_load_creds(ssid, sizeof ssid, pass, sizeof pass);
            if (have) {
                creds_set(ssid, pass);
                net_bring_up(); /* wifi.set 连发：以最新 NVS 为准 */
                last_retry = 0;
            } else {
                net_teardown(); /* wifi.clear */
            }
            continue;
        }

        const int64_t now = esp_timer_get_time();
        if (s_wifi_run && s_reconnect_req) {
            const int64_t backoff_s = 10 * (int64_t)(s_fails < 4u ? (int)s_fails : 4); /* 0/10/20/30/40 s */
            if (now - last_retry >= backoff_s * 1000000LL) {
                last_retry = now;
                s_fails++;
                ESP_LOGI(TAG, "net: wifi retry");
                (void)esp_wifi_connect();
            }
        }
        if (s_wifi_started && now - last_push >= NET_RESYNC_US) {
            int hour;
            if (local_hour(&hour)) { /* 时间已有效才占周期；无效则 2 s 后再试 */
                last_push = now;
                push_hour(false);
            }
        }
    }
}

void pet_net_start(void)
{
    if (xTaskCreate(net_task, "pet-net", 4096, NULL, 3, &s_task) != pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "net: task create failed");
    }
}

static void net_notify(void)
{
    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

bool pet_net_handle_line(const char *line)
{
    if (line == NULL || strstr(line, "\"cmd\"") == NULL) {
        return false;
    }
    char cmd[16];
    if (!json_string(line, "cmd", cmd, sizeof cmd)) {
        return false;
    }
    if (strcmp(cmd, "wifi.set") == 0) {
        char ssid[NET_SSID_MAX], pass[NET_PASS_MAX];
        if (!json_string(line, "ssid", ssid, sizeof ssid) || ssid[0] == '\0' ||
            !json_string(line, "password", pass, sizeof pass)) {
            ESP_LOGW(TAG, "net: wifi.set invalid");
            return true;
        }
        nvs_save_creds(ssid, pass);
        ESP_LOGI(TAG, "net: credentials saved"); /* 口令永不回显 */
        net_notify();
        return true;
    }
    if (strcmp(cmd, "wifi.status") == 0) {
        ESP_LOGI(TAG, "net: status ssid=%s ip=%s hour=%d",
                 s_ssid[0] != '\0' ? s_ssid : "-",
                 s_ip[0] != '\0' ? s_ip : "-",
                 s_hour_synced);
        return true;
    }
    if (strcmp(cmd, "wifi.clear") == 0) {
        nvs_clear_creds();
        ESP_LOGI(TAG, "net: credentials cleared");
        net_notify();
        return true;
    }
    return false; /* 其他 cmd：交给事件解析路径按原样拒绝 */
}
