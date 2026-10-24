/* WiFi 状态机（idle/sta/ap）+ SNTP 授时。M5StickS3（ESP32-S3）2.4 GHz。
 * 配网：无凭据开机即开 AP 热点 + 手机网页配置（pet_prov 承载热点/HTTP/DNS）；
 * STA 反复连不上（连续 5 个连接周期 ≈2 min 无 IP）也回落热点供用户改密码。
 * 凭据只存 NVS（namespace petnet，键 ssid/pass），代码与日志均不出现明文口令。
 * 失败降级：任何一步失败只记日志，不崩，显示/按键/宠物不受影响。
 * 契约：evidence/2026-10-24-WiFi与NTP-设备.md */
#include "pet_net.h"

#include <stdio.h>
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
#include "pet_prov.h"

static const char *TAG = "pet-net";

#define NET_NVS_NS "petnet"
#define NET_SSID_MAX 33u  /* 802.11 SSID ≤32 字节 + NUL */
#define NET_PASS_MAX 65u  /* WPA 口令 ≤64 字节 + NUL */
#define NET_RESYNC_US 3600000000LL /* 每小时重算本地小时 */
#define NET_STA_FAIL_MAX 5  /* 连续 5 个连接周期无 IP → 回落配网热点 */
#define NET_APPLY_GRACE_MS 1500 /* POST /save 应答落地后再拆热点 */

/* 任务通知位：clear/provision/apply（apply 最后处理，"先清后存"以最新保存为准） */
#define NET_NOTIFY_CLEAR 0x01u
#define NET_NOTIFY_PROV  0x02u
#define NET_NOTIFY_APPLY 0x04u

typedef enum {
    NET_ST_IDLE = 0, /* WiFi 未运行（初始化失败 / 瞬态） */
    NET_ST_STA,      /* STA 连接中或已连接 */
    NET_ST_AP        /* 配网热点运行中 */
} net_state_t;

/* 网络任务唯一持有 WiFi 状态；rx/httpd 上下文只经 notify 触发 */
static TaskHandle_t s_task;
static volatile net_state_t s_state;
static char s_ssid[NET_SSID_MAX];
static char s_pass[NET_PASS_MAX];
static char s_ip[16]; /* "255.255.255.255" */
static volatile bool s_net_inited;
static volatile bool s_wifi_started;
static volatile bool s_wifi_run;    /* false = STA 已停，断开事件不再重连 */
static volatile bool s_reconnect_req;
static volatile uint32_t s_fails;   /* 连续失败次数 → 重试退避 / 回落热点 */
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

/* ---- 底座（一次性）：netif + 事件循环 + wifi 驱动 + 事件注册 ---- */
static bool net_base_init(void)
{
    if (s_net_inited) {
        return true;
    }
    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "net: netif init failed");
        return false;
    }
    const esp_err_t el = esp_event_loop_create_default();
    if (el != ESP_OK && el != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "net: event loop failed");
        return false;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        ESP_LOGE(TAG, "net: netif sta failed");
        return false;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "net: wifi init failed");
        return false;
    }
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            net_event_handler, NULL, &s_ev_wifi) != ESP_OK ||
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            net_event_handler, NULL, &s_ev_ip) != ESP_OK) {
        ESP_LOGE(TAG, "net: event register failed");
        return false;
    }
    (void)esp_wifi_set_storage(WIFI_STORAGE_RAM); /* 凭据只在我们自己的 NVS 命名空间 */
    s_net_inited = true;
    return true;
}

static void creds_set(const char *ssid, const char *pass)
{
    strncpy(s_ssid, ssid, sizeof s_ssid - 1);
    s_ssid[sizeof s_ssid - 1] = '\0';
    strncpy(s_pass, pass, sizeof s_pass - 1);
    s_pass[sizeof s_pass - 1] = '\0';
}

/* 下发凭据 + 启动 STA（模式/配置/开台）。任一步失败只记日志返回 false，不崩。 */
static bool net_bring_up(void)
{
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        ESP_LOGE(TAG, "net: set mode sta failed");
        return false;
    }
    wifi_config_t wcfg;
    memset(&wcfg, 0, sizeof wcfg);
    strncpy((char *)wcfg.sta.ssid, s_ssid, sizeof wcfg.sta.ssid - 1);
    strncpy((char *)wcfg.sta.password, s_pass, sizeof wcfg.sta.password - 1);
    if (esp_wifi_set_config(WIFI_IF_STA, &wcfg) != ESP_OK) {
        ESP_LOGE(TAG, "net: wifi config failed");
        return false;
    }

    s_wifi_run = true;
    s_reconnect_req = false;
    s_fails = 0;
    sntp_setup();
    if (!s_wifi_started) {
        if (esp_wifi_start() != ESP_OK) {
            ESP_LOGE(TAG, "net: wifi start failed");
            s_wifi_run = false;
            return false;
        }
        s_wifi_started = true;
    } else {
        /* 已在网：换凭据 → 断开事件触发任务用新配置重连 */
        (void)esp_wifi_disconnect();
    }
    return true;
}

/* 停 STA（保留最后一次同步的 hour）；热点开台前的 STA 路径清场 */
static void sta_stop_radio(void)
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
}

static void enter_prov(void)
{
    if (s_state == NET_ST_AP) {
        return;
    }
    sta_stop_radio();
    if (pet_prov_start() != ESP_OK) {
        ESP_LOGE(TAG, "net: provisioning start failed");
        s_state = NET_ST_IDLE;
        return;
    }
    s_state = NET_ST_AP;
}

static void net_task(void *arg)
{
    (void)arg;
    if (!net_base_init()) {
        ESP_LOGE(TAG, "net: base init failed, net parked"); /* 降级：设备照常跑，只是无网 */
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(60000));
        }
    }

    char ssid[NET_SSID_MAX], pass[NET_PASS_MAX];
    const bool have = nvs_load_creds(ssid, sizeof ssid, pass, sizeof pass);
    int64_t last_retry = 0;
    int64_t last_push = 0;

    s_state = NET_ST_IDLE;
    if (have) {
        creds_set(ssid, pass);
        if (net_bring_up()) {
            s_state = NET_ST_STA;
        }
    } else {
        ESP_LOGW(TAG, "net: no credentials");
        enter_prov(); /* 开机无凭据 → 配网热点 */
    }

    for (;;) {
        const uint32_t bits = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));

        if ((bits & NET_NOTIFY_CLEAR) != 0u) {
            nvs_clear_creds();
            ESP_LOGI(TAG, "net: credentials cleared");
            s_ssid[0] = '\0';
            s_pass[0] = '\0';
            enter_prov(); /* clear → 断 STA（如有）+ 开热点 */
        }
        if ((bits & NET_NOTIFY_PROV) != 0u) {
            if (s_state != NET_ST_AP) {
                ESP_LOGI(TAG, "net: provisioning requested");
                enter_prov(); /* prov → STA 在连也断开，转热点 */
            }
        }
        if ((bits & NET_NOTIFY_APPLY) != 0u && nvs_load_creds(ssid, sizeof ssid, pass, sizeof pass)) {
            if (s_state == NET_ST_AP) {
                vTaskDelay(pdMS_TO_TICKS(NET_APPLY_GRACE_MS)); /* 让手机先收到"已保存"页面 */
                pet_prov_stop(); /* 停 AP+HTTP+DNS 后再连 STA */
            }
            creds_set(ssid, pass);
            if (net_bring_up()) {
                s_state = NET_ST_STA;
                last_retry = 0;
                last_push = 0;
            } else {
                s_state = NET_ST_IDLE;
            }
        }

        if (s_state != NET_ST_STA) {
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
            if (s_fails >= NET_STA_FAIL_MAX && s_ip[0] == '\0') {
                /* 连续 5 个连接周期（≈2 min）拿不到 IP → 密码可能不对，回落热点 */
                ESP_LOGW(TAG, "net: sta failed, entering provisioning");
                enter_prov();
                continue;
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

static void net_notify(uint32_t bits)
{
    if (s_task != NULL) {
        xTaskNotify(s_task, bits, eSetBits);
    }
}

/* 配网页 POST /save 落点：校验 → 存 NVS → 通知状态机切换 STA。口令绝不入日志。 */
void pet_net_apply_credentials(const char *ssid, const char *pass)
{
    if (ssid == NULL || pass == NULL || ssid[0] == '\0' ||
        strlen(ssid) > 32 || strlen(pass) > 64) {
        ESP_LOGW(TAG, "net: apply invalid");
        return;
    }
    nvs_save_creds(ssid, pass);
    ESP_LOGI(TAG, "net: credentials saved"); /* 口令永不回显 */
    net_notify(NET_NOTIFY_APPLY);
}

/* 配网页 GET /status 数据源：state/ssid/ip/hour（无口令；撕读最坏只影响一次页面） */
void pet_net_read_status(char *state, size_t state_sz, char *ssid, size_t ssid_sz,
                         char *ip, size_t ip_sz, int *hour)
{
    snprintf(state, state_sz, "%s",
             s_state == NET_ST_STA ? "sta" : (s_state == NET_ST_AP ? "ap" : "idle"));
    snprintf(ssid, ssid_sz, "%s", s_ssid);
    snprintf(ip, ip_sz, "%s", s_ip);
    *hour = s_hour_synced;
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
    if (strcmp(cmd, "wifi.status") == 0) {
        ESP_LOGI(TAG, "net: status state=%s ssid=%s ip=%s hour=%d",
                 s_state == NET_ST_STA ? "sta" : (s_state == NET_ST_AP ? "ap" : "idle"),
                 s_ssid[0] != '\0' ? s_ssid : "-",
                 s_ip[0] != '\0' ? s_ip : "-",
                 s_hour_synced);
        return true;
    }
    if (strcmp(cmd, "wifi.provision") == 0) {
        net_notify(NET_NOTIFY_PROV);
        return true;
    }
    if (strcmp(cmd, "wifi.clear") == 0) {
        net_notify(NET_NOTIFY_CLEAR);
        return true;
    }
    return false; /* 其他 cmd：交给事件解析路径按原样拒绝 */
}
