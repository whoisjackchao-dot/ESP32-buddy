#pragma once

#include <stdbool.h>

#include "esp_err.h"

/* AP 配网：热点 ESP32-Buddy-XXXX（开放，无口令，信道 1，最多 4 连接，AP IP 192.168.4.1）
 * + HTTP 配置页 :80（GET / 、GET /scan 、GET /status 、POST /save）+ DNS 劫持 :53（任意 A 查询答 192.168.4.1）。
 * 配网期间 WiFi 模式 APSTA：AP 服务手机，STA 接口仅供 /scan 主动扫描（无凭据不连接）。
 * 前提：调用前 pet_net 已完成 esp_netif / 默认事件循环 / esp_wifi_init。
 * 约束：只在 pet-net 任务上下文调用 start/stop（与 STA 切换天然串行，无锁）。
 * 契约：evidence/2026-10-24-WiFi与NTP-设备.md */

/* 启动配网（非阻塞：httpd/DNS 各自任务承载）。已启动时幂等返回 ESP_OK。 */
esp_err_t pet_prov_start(void);

/* 停配网：停 HTTP → 停 DNS → esp_wifi_stop（热点关台）。未启动时直接返回。 */
void pet_prov_stop(void);
