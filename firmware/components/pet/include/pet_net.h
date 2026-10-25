#pragma once

#include <stddef.h>

#include <stdbool.h>

/* WiFi 状态机（idle/sta/ap）+ SNTP 授时 + AP 配网（热点/网页/DNS 在 pet_prov）。
 * 非阻塞：pet_net_start 只建任务；配网/连不上时显示、按键、宠物行为与无网时完全一致。
 * 契约：evidence/2026-10-24-WiFi与NTP-设备.md */

/* 启动网络任务：读 NVS 凭据，有则连 WiFi + SNTP；无则开配网热点（ESP32-Buddy-XXXX）。
 * STA 连不上（约 2 min）自动回落热点；绝不阻塞启动。 */
void pet_net_start(void);

/* 配网页 POST /save 落点：校验并保存凭据到 NVS（namespace petnet），通知状态机切 STA。
 * 只在 pet_prov 的 httpd 上下文调用；ssid 1..32 字节、pass 0..64 字节，非法仅记日志丢弃。 */
void pet_net_apply_credentials(const char *ssid, const char *pass);

/* 配网页 GET /status 数据源：state 为 "ap|sta|idle"，ssid/ip 无值为空串，hour 未同步为 -1。
 * 无口令。任意任务上下文可调用（无锁快照）。 */
void pet_net_read_status(char *state, size_t state_sz, char *ssid, size_t ssid_sz,
                         char *ip, size_t ip_sz, int *hour);

/* 设备侧产线 cmd 行（wifi.provision / wifi.status / wifi.clear / host.addr）；
 * 命中并处理返回 true。 */
bool pet_net_handle_line(const char *line);

/* 设备→主机控制线（引擎产物 voice.text / send 等经 WiFi TCP 镜像发往主机，
 * 主机地址由 host.addr 命令下发并持久化在 NVS）。只拷贝入队即返回（引擎
 * 循环零阻塞）；未配置主机 / 无 WiFi 时由发送任务静默丢弃，队列满时限频
 * 日志丢弃。USB 路径不受影响（双路并行，主机侧去重）。 */
void pet_net_send_control_line(const char *line);

/* 菜单“配网模式”：请求进入 AP 配网（断开 STA，开热点+网页）。 */
void pet_net_request_provision(void);

/* 当前 STA IPv4 字符串（未连接 → 空串）。 */
void pet_net_get_ip(char *out, size_t len);
