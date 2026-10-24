/* AP 配网承载：热点 + 手机网页配置页（扫描列表选网）+ captive-portal DNS 劫持。
 * 由 pet_net 状态机调用（同一任务上下文）；HTTP/DNS 各自任务承载，不阻塞 pet-net。
 * 配网期间 WiFi 为 APSTA：AP 服务手机，STA 接口仅供 /scan 扫描（无凭据不连接）。
 * 提交的凭据经 pet_net_apply_credentials() 交给 pet_net（存 NVS + 通知状态机切换 STA）。
 * 口令只在内存传递，绝不进日志。契约：evidence/2026-10-24-WiFi与NTP-设备.md */
#include "pet_prov.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"

#include "pet_net.h"

static const char *TAG = "pet-prov";

#define PROV_DNS_PORT 53
#define PROV_DNS_BUF 512u
#define PROV_DNS_STACK 3072u

static volatile bool s_active;
static esp_netif_t *s_ap_netif;
static httpd_handle_t s_server;
static TaskHandle_t s_dns_task;
static volatile bool s_dns_run;
static char s_ap_ssid[20]; /* "ESP32-Buddy-XXXX" + NUL */

/* ---- 配置页（中文标签，内联样式/脚本，无外部资源；默认流程=扫描列表选网，手动输入仅隐藏网络兜底） ---- */
static const char PAGE_HOME[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>ESP32-Buddy 配网</title><style>"
    "body{font-family:system-ui,sans-serif;margin:0;padding:16px;background:#f4f6f8;color:#222}"
    ".card{max-width:360px;margin:24px auto;background:#fff;border-radius:12px;padding:20px;"
    "box-shadow:0 2px 8px rgba(0,0,0,.08)}"
    "h1{font-size:20px;margin:0 0 12px}"
    "label{display:block;margin:12px 0 4px;font-size:14px;color:#555}"
    "input{width:100%;box-sizing:border-box;padding:10px;font-size:16px;border:1px solid #ccc;border-radius:8px}"
    "button{font-size:16px;border:0;border-radius:8px;background:#2b7de9;color:#fff}"
    "#rescan{width:100%;margin-top:4px;padding:10px;background:#eef3f9;color:#2b7de9}"
    ".sb{width:100%;margin-top:16px;padding:12px}"
    "#list{display:flex;flex-direction:column;gap:8px;margin-top:8px;max-height:260px;overflow-y:auto}"
    ".net{display:flex;justify-content:space-between;align-items:center;gap:8px;text-align:left;"
    "padding:12px;background:#f6f8fa;color:#222;border:1px solid #e2e6ea}"
    ".net.on{background:#e6f0fd;border-color:#2b7de9}"
    ".sg{color:#2b7de9;letter-spacing:2px}"
    ".empty{color:#888;font-size:14px;margin:8px 0}"
    "summary{cursor:pointer;margin-top:14px;font-size:14px;color:#555}"
    "</style></head><body><div class=\"card\">"
    "<h1>ESP32-Buddy 配网</h1>"
    "<p>当前状态：<b id=\"st\">读取中…</b></p>"
    "<button id=\"rescan\" type=\"button\">重新扫描</button>"
    "<div id=\"list\"><p class=\"empty\">扫描中…</p></div>"
    "<details><summary>手动输入（隐藏网络）</summary>"
    "<label for=\"mssid\">网络名称（SSID）</label>"
    "<input id=\"mssid\" maxlength=\"32\" autocomplete=\"off\">"
    "</details>"
    "<form method=\"POST\" action=\"/save\" onsubmit=\"return preSubmit()\">"
    "<input type=\"hidden\" name=\"ssid\" id=\"ssid\">"
    "<label for=\"password\">密码</label>"
    "<input id=\"password\" name=\"password\" type=\"password\" maxlength=\"64\">"
    "<button type=\"submit\" class=\"sb\">保存并连接</button>"
    "</form></div>"
    "<script>var sel='';"
    "function bars(r){return r>-55?'\\u2582\\u2584\\u2586\\u2588':"
    "r>-67?'\\u2582\\u2584\\u2586':r>-78?'\\u2582\\u2584':'\\u2582'}"
    "function showEmpty(t){var l=document.getElementById('list');l.innerHTML='';"
    "var e=document.createElement('p');e.className='empty';e.textContent=t;l.appendChild(e)}"
    "function clearSel(){sel='';"
    "var xs=document.querySelectorAll('.net');"
    "for(var k=0;k<xs.length;k++){xs[k].classList.remove('on')}}"
    "function scan(){var rs=document.getElementById('rescan');"
    "rs.disabled=true;clearSel();showEmpty('扫描中…');"
    "fetch('/scan').then(function(r){return r.json()}).then(function(j){"
    "rs.disabled=false;"
    "if(!j.length){showEmpty('未扫描到网络，可重试或手动输入');return}"
    "var l=document.getElementById('list');l.innerHTML='';"
    "j.forEach(function(n){"
    "var b=document.createElement('button');b.type='button';b.className='net';"
    "var nm=document.createElement('span');nm.textContent=n.ssid;"
    "var sg=document.createElement('span');sg.className='sg';sg.textContent=bars(n.rssi);"
    "b.appendChild(nm);b.appendChild(sg);"
    "b.onclick=function(){sel=n.ssid;document.getElementById('mssid').value='';"
    "var xs=document.querySelectorAll('.net');"
    "for(var k=0;k<xs.length;k++){xs[k].classList.remove('on')}"
    "b.classList.add('on')};"
    "l.appendChild(b)})})"
    ".catch(function(){rs.disabled=false;showEmpty('扫描失败，可重试或手动输入')})}"
    "document.getElementById('rescan').onclick=scan;"
    "document.getElementById('mssid').oninput=clearSel;"
    "function preSubmit(){var m=document.getElementById('mssid');"
    "if(m.value){document.getElementById('ssid').value=m.value}"
    "else{document.getElementById('ssid').value=sel}"
    "if(!document.getElementById('ssid').value){"
    "alert('请选择一个网络，或在手动输入中填写网络名称');return false}"
    "return true}"
    "scan();"
    "fetch('/status').then(function(r){return r.json()}).then(function(j){"
    "var s=document.getElementById('st');"
    "if(j.hour>=0){s.textContent='时间同步（'+j.hour+' 点）'}"
    "else if(j.state=='sta'&&j.ip){s.textContent='已连接 '+j.ip}"
    "else if(j.state=='ap'&&j.ssid){s.textContent='已保存，正在连接'}"
    "else{s.textContent='未配置'}})"
    ".catch(function(){document.getElementById('st').textContent='未配置'});"
    "</script></body></html>";

static const char PAGE_SAVED[] =
    "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>已保存</title></head><body style=\"font-family:system-ui,sans-serif;padding:24px\">"
    "<h2>已保存，正在连接家庭 WiFi，可断开热点</h2></body></html>";

/* ---- 发送 HTML（status 为 NULL 用默认 200） ---- */
static esp_err_t send_html(httpd_req_t *req, const char *status, const char *body)
{
    if (status != NULL) {
        httpd_resp_set_status(req, status);
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

/* ---- GET /：配置页 ---- */
static esp_err_t h_root(httpd_req_t *req)
{
    return send_html(req, NULL, PAGE_HOME);
}

/* ---- 状态字段（pet_net 持有；这里只读快照，最坏撕读只影响一次页面显示） ---- */
static void json_escape(const char *in, char *out, size_t out_sz)
{
    size_t n = 0;
    for (; *in != '\0'; in++) {
        const unsigned char c = (unsigned char)*in;
        const char *esc = NULL;
        char buf[8];
        if (c == '"' || c == '\\') {
            buf[0] = '\\';
            buf[1] = (char)c;
            buf[2] = '\0';
            esc = buf;
        } else if (c < 0x20u) {
            snprintf(buf, sizeof buf, "\\u%04X", (unsigned)c);
            esc = buf;
        }
        const char *piece = esc != NULL ? esc : (const char[]){ (char)c, '\0' };
        const size_t len = strlen(piece);
        if (n + len >= out_sz) {
            break;
        }
        memcpy(&out[n], piece, len);
        n += len;
    }
    out[n] = '\0';
}

/* ---- GET /status：{state, ssid, ip, hour}，绝不含口令 ---- */
static esp_err_t h_status(httpd_req_t *req)
{
    char state[8], ssid[33], ip[16], essid[80], out[192];
    int hour = -1;
    pet_net_read_status(state, sizeof state, ssid, sizeof ssid, ip, sizeof ip, &hour);
    json_escape(ssid, essid, sizeof essid);
    snprintf(out, sizeof out,
             "{\"state\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"hour\":%d}",
             state, essid, ip, hour);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

/* ---- GET /scan：APSTA 阻塞扫描周围网络 → JSON 数组，RSSI 降序，同名去重留最强，≤20 个 ---- */
#define PROV_SCAN_MAX 30u /* 原始抓取上限（超出部分丢弃，输出上限 20 用不满） */
#define PROV_SCAN_OUT 20u /* 返回给页面的条数上限 */

typedef struct {
    char ssid[33]; /* SSID ≤32 字节 + NUL */
    int8_t rssi;
    uint8_t auth; /* wifi_auth_mode_t 原值 */
} prov_net_t;

static wifi_ap_record_t s_scan_recs[PROV_SCAN_MAX];
static prov_net_t s_scan_nets[PROV_SCAN_MAX];

static size_t prov_scan_collect(uint16_t num)
{
    size_t n = 0;
    for (uint16_t i = 0; i < num; i++) {
        const wifi_ap_record_t *r = &s_scan_recs[i];
        if (r->ssid[0] == '\0') {
            continue; /* 空 / 隐藏 SSID 跳过（show_hidden=false 已滤一层） */
        }
        size_t j = 0;
        while (j < n && strncmp(s_scan_nets[j].ssid, (const char *)r->ssid,
                                sizeof s_scan_nets[j].ssid) != 0) {
            j++;
        }
        if (j < n) { /* 同名：留最强 RSSI */
            if (r->rssi > s_scan_nets[j].rssi) {
                s_scan_nets[j].rssi = r->rssi;
                s_scan_nets[j].auth = (uint8_t)r->authmode;
            }
            continue;
        }
        if (n >= PROV_SCAN_MAX) {
            continue;
        }
        strncpy(s_scan_nets[j].ssid, (const char *)r->ssid, sizeof s_scan_nets[j].ssid - 1);
        s_scan_nets[j].ssid[sizeof s_scan_nets[j].ssid - 1] = '\0';
        s_scan_nets[j].rssi = r->rssi;
        s_scan_nets[j].auth = (uint8_t)r->authmode;
        n++;
    }
    /* RSSI 降序（n ≤ 30，选择排序足够） */
    for (size_t a = 0; a + 1 < n; a++) {
        size_t best = a;
        for (size_t b = a + 1; b < n; b++) {
            if (s_scan_nets[b].rssi > s_scan_nets[best].rssi) {
                best = b;
            }
        }
        if (best != a) {
            const prov_net_t t = s_scan_nets[a];
            s_scan_nets[a] = s_scan_nets[best];
            s_scan_nets[best] = t;
        }
    }
    return n;
}

static esp_err_t h_scan(httpd_req_t *req)
{
    /* 阻塞式主动扫描：默认信道集 + 默认 dwell（≈2–3 s，<6 s 处理上限）。
     * APSTA 下 AP 照常广播，已关联手机不掉线（个别信道驻留期间略有延迟）。 */
    httpd_resp_set_type(req, "application/json");
    wifi_scan_config_t scfg = { 0 };
    scfg.show_hidden = false;
    if (esp_wifi_scan_start(&scfg, true) != ESP_OK) {
        return httpd_resp_send(req, "[]", 2); /* 失败/超时/正在扫描 → 空数组 + 200 */
    }
    uint16_t num = (uint16_t)PROV_SCAN_MAX;
    if (esp_wifi_scan_get_ap_records(&num, s_scan_recs) != ESP_OK || num == 0) {
        return httpd_resp_send(req, "[]", 2);
    }
    const size_t n = prov_scan_collect(num);
    esp_err_t err = httpd_resp_send_chunk(req, "[", 1);
    for (size_t i = 0; err == ESP_OK && i < n && i < PROV_SCAN_OUT; i++) {
        char essid[200]; /* 32 字节 SSID 全控制字符转义最坏 192 字节 */
        char item[256];
        json_escape(s_scan_nets[i].ssid, essid, sizeof essid);
        snprintf(item, sizeof item, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%u}",
                 i == 0 ? "" : ",", essid, (int)s_scan_nets[i].rssi,
                 (unsigned)s_scan_nets[i].auth);
        err = httpd_resp_send_chunk(req, item, HTTPD_RESP_USE_STRLEN);
    }
    if (err != ESP_OK) {
        return err; /* 手机端提前断开等：中止即可 */
    }
    err = httpd_resp_send_chunk(req, "]", 1);
    if (err != ESP_OK) {
        return err;
    }
    return httpd_resp_send_chunk(req, NULL, 0); /* 收尾 0 长度块 */
}

/* ---- 表单解析：键值定位 + URL 解码（'+'→空格，%XX） ---- */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool url_decode(const char *in, char *out, size_t out_sz)
{
    size_t n = 0;
    for (; *in != '\0'; in++) {
        char c = *in;
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            const int hi = hexval(in[1]);
            const int lo = hexval(in[2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            c = (char)((hi << 4) | lo);
            in += 2;
        }
        if (n + 1 >= out_sz) {
            return false;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return true;
}

static bool form_field(const char *body, const char *key, char *out, size_t out_sz)
{
    const size_t klen = strlen(key);
    const char *p = body;
    while (*p != '\0') {
        const char *amp = strchr(p, '&');
        const size_t seg = amp != NULL ? (size_t)(amp - p) : strlen(p);
        if (seg > klen && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            char raw[192];
            if (seg - klen - 1 >= sizeof raw) {
                return false;
            }
            memcpy(raw, p + klen + 1, seg - klen - 1);
            raw[seg - klen - 1] = '\0';
            return url_decode(raw, out, out_sz);
        }
        if (amp == NULL) {
            break;
        }
        p = amp + 1;
    }
    return false;
}

/* ---- 400 页 ---- */
static esp_err_t save_bad_request(httpd_req_t *req, const char *msg)
{
    char page[512];
    snprintf(page, sizeof page,
             "<!doctype html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
             "</head><body style=\"font-family:system-ui,sans-serif;padding:24px\">"
             "<h2>配网失败</h2><p>%s</p><p><a href=\"/\">返回重试</a></p></body></html>",
             msg);
    return send_html(req, "400 Bad Request", page);
}

/* ---- POST /save：解析 → 校验 → 先应答 → 再交 pet_net 切换 ---- */
static esp_err_t h_save(httpd_req_t *req)
{
    char body[320];
    if (req->content_len <= 0 || req->content_len >= (int)sizeof body) {
        return save_bad_request(req, "请求长度无效");
    }
    int got = 0;
    while (got < req->content_len) {
        const int n = httpd_req_recv(req, &body[got], (size_t)(req->content_len - got));
        if (n <= 0) {
            return save_bad_request(req, "读取请求失败");
        }
        got += n;
    }
    body[got] = '\0';

    char ssid[33], pass[65];
    if (!form_field(body, "ssid", ssid, sizeof ssid) ||
        !form_field(body, "password", pass, sizeof pass) ||
        ssid[0] == '\0' || strlen(ssid) > 32 || strlen(pass) > 64) {
        return save_bad_request(req, "参数无效：WiFi 名称需 1–32 字符，密码 0–64 字符");
    }

    ESP_LOGI(TAG, "prov: save ssid=%s", ssid); /* 口令绝不入日志 */
    /* 先应答再切换：HTTP 句柄内不阻塞；热点拆除由 pet-net 任务在宽限后执行 */
    (void)send_html(req, NULL, PAGE_SAVED);
    pet_net_apply_credentials(ssid, pass);
    return ESP_OK;
}

static const httpd_uri_t URI_ROOT = { .uri = "/", .method = HTTP_GET, .handler = h_root };
static const httpd_uri_t URI_SCAN = { .uri = "/scan", .method = HTTP_GET, .handler = h_scan };
static const httpd_uri_t URI_STATUS = { .uri = "/status", .method = HTTP_GET, .handler = h_status };
static const httpd_uri_t URI_SAVE = { .uri = "/save", .method = HTTP_POST, .handler = h_save };

/* ---- DNS 劫持：UDP :53，任意查询答一条 A 记录 192.168.4.1 ---- */
static void dns_task(void *arg)
{
    (void)arg;
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        ESP_LOGE(TAG, "prov: dns socket failed");
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    const struct timeval tv = { .tv_sec = 0, .tv_usec = 500 * 1000 };
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PROV_DNS_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        ESP_LOGE(TAG, "prov: dns bind failed");
        (void)close(fd);
        s_dns_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "prov: dns hijack on :53");

    static uint8_t buf[PROV_DNS_BUF];
    while (s_dns_run) {
        struct sockaddr_in from = { 0 };
        socklen_t flen = sizeof from;
        const int n = recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &flen);
        if (n < 12) {
            continue; /* 500 ms 超时 / 过短包 */
        }
        if ((buf[2] & 0x80u) != 0u) {
            continue; /* 已是应答，不回环 */
        }
        const int qd = (buf[4] << 8) | buf[5];
        if (qd == 0) {
            continue;
        }
        /* 定位问题区结尾（跳过全部 QNAME + qtype + qclass） */
        int pos = 12;
        bool ok = true;
        for (int q = 0; q < qd && ok; q++) {
            while (pos < n && buf[pos] != 0u) {
                if ((buf[pos] & 0xC0u) != 0u) {
                    pos++; /* 压缩指针首字节，循环外再补一跳 */
                    break;
                }
                pos += buf[pos] + 1;
            }
            if (pos >= n) {
                ok = false;
                break;
            }
            pos += 1 + 4; /* QNAME 结束 0 字节（或指针第二字节）+ qtype + qclass */
        }
        if (!ok || pos + 16 > (int)sizeof buf) {
            continue;
        }
        buf[2] = (uint8_t)(buf[2] | 0x80u); /* QR=1 */
        buf[3] = (uint8_t)(buf[3] | 0x80u); /* RA=1 */
        buf[6] = 0; buf[7] = 1;             /* ANCOUNT=1 */
        buf[8] = 0; buf[9] = 0; buf[10] = 0; buf[11] = 0;
        static const uint8_t ans[16] = {          /* 指向 QNAME 的 A 应答 */
            0xC0, 0x0C,                           /* NAME 指针 → 偏移 12 */
            0x00, 0x01,                           /* TYPE=A */
            0x00, 0x01,                           /* CLASS=IN */
            0x00, 0x00, 0x00, 60,                 /* TTL=60 s */
            0x00, 0x04,                           /* RDLENGTH=4 */
            192, 168, 4, 1                        /* RDATA */
        };
        memcpy(&buf[pos], ans, sizeof ans);
        (void)sendto(fd, buf, pos + 16, 0, (struct sockaddr *)&from, flen);
    }

    (void)close(fd);
    ESP_LOGI(TAG, "prov: dns stopped");
    s_dns_task = NULL;
    vTaskDelete(NULL);
}

/* ---- 启动：AP netif（一次）→ 显式 192.168.4.1 → 模式/配置/开台 → HTTP → DNS ---- */
esp_err_t pet_prov_start(void)
{
    if (s_active) {
        return ESP_OK;
    }
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (s_ap_netif == NULL) {
            ESP_LOGE(TAG, "prov: netif ap failed");
            return ESP_FAIL;
        }
    }
    /* 契约锁定 AP IP=192.168.4.1（默认值一致，仍显式设置防默认漂移） */
    if (esp_netif_dhcps_stop(s_ap_netif) != ESP_OK) {
        ESP_LOGW(TAG, "prov: dhcps stop failed");
    }
    esp_netif_ip_info_t info = { 0 };
    IP4_ADDR(&info.ip, 192, 168, 4, 1);
    IP4_ADDR(&info.gw, 192, 168, 4, 1);
    IP4_ADDR(&info.netmask, 255, 255, 255, 0);
    if (esp_netif_set_ip_info(s_ap_netif, &info) != ESP_OK) {
        ESP_LOGE(TAG, "prov: set ip failed");
        return ESP_FAIL;
    }
    (void)esp_netif_dhcps_start(s_ap_netif);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_ap_ssid, sizeof s_ap_ssid, "ESP32-Buddy-%02X%02X", mac[4], mac[5]);

    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
        /* APSTA：AP 热点服务手机的同时，STA 接口可扫描（/scan）；STA 无凭据不连接 */
        ESP_LOGE(TAG, "prov: set mode failed");
        return ESP_FAIL;
    }
    wifi_config_t wcfg;
    memset(&wcfg, 0, sizeof wcfg);
    strncpy((char *)wcfg.ap.ssid, s_ap_ssid, sizeof wcfg.ap.ssid - 1);
    wcfg.ap.ssid_len = (uint8_t)strlen(s_ap_ssid);
    wcfg.ap.channel = 1;
    wcfg.ap.max_connection = 4;
    wcfg.ap.authmode = WIFI_AUTH_OPEN; /* 开放热点，无口令 */
    if (esp_wifi_set_config(WIFI_IF_AP, &wcfg) != ESP_OK) {
        ESP_LOGE(TAG, "prov: set config failed");
        return ESP_FAIL;
    }
    if (esp_wifi_start() != ESP_OK) {
        ESP_LOGE(TAG, "prov: wifi start failed");
        return ESP_FAIL;
    }

    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.stack_size = 6144; /* /save 栈上表单缓冲需要余量 */
    if (httpd_start(&s_server, &hcfg) != ESP_OK ||
        httpd_register_uri_handler(s_server, &URI_ROOT) != ESP_OK ||
        httpd_register_uri_handler(s_server, &URI_SCAN) != ESP_OK ||
        httpd_register_uri_handler(s_server, &URI_STATUS) != ESP_OK ||
        httpd_register_uri_handler(s_server, &URI_SAVE) != ESP_OK) {
        ESP_LOGE(TAG, "prov: http start failed");
        if (s_server != NULL) {
            (void)httpd_stop(s_server);
            s_server = NULL;
        }
        (void)esp_wifi_stop();
        return ESP_FAIL;
    }

    s_dns_run = true;
    if (xTaskCreate(dns_task, "prov-dns", PROV_DNS_STACK, NULL, 3, &s_dns_task) != pdPASS) {
        s_dns_task = NULL;
        ESP_LOGE(TAG, "prov: dns task failed"); /* DNS 缺席只影响自动弹窗，手动开 192.168.4.1 仍可用 */
    }

    s_active = true;
    ESP_LOGI(TAG, "net: provisioning AP ssid=%s ip=192.168.4.1", s_ap_ssid);
    return ESP_OK;
}

void pet_prov_stop(void)
{
    if (!s_active) {
        return;
    }
    s_active = false;
    if (s_server != NULL) {
        (void)httpd_stop(s_server); /* 停收新请求并回收 httpd 任务 */
        s_server = NULL;
    }
    s_dns_run = false;
    int wait = 0;
    while (s_dns_task != NULL && wait < 10) { /* 等 DNS 任务自行退出（≤500 ms） */
        vTaskDelay(pdMS_TO_TICKS(50));
        wait++;
    }
    (void)esp_wifi_stop(); /* 关台：热点消失，radio 交还 STA 路径 */
    ESP_LOGI(TAG, "prov: stopped");
}
