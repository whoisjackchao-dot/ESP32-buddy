#pragma once

/* M16 B1：BLE 广播基线（NimBLE 外设，仅广播，无 GATT 服务）。
 * 目的：验证 BLE 与 WiFi STA 共存、flash/RAM 增量、WiFi 时延/吞吐基线。
 * 契约：ESP32-buddy_BLE与WiFi双连接方案.md §3 B1。
 * 注意：B1 只广播；事件/控制通道仍在 USB / WiFi。 */
#include <stdbool.h>

void pet_ble_start(void);

/* M16：激活配对模式（清绑定 + 重新广播）。返回 false 表示 BLE 未启动。 */
bool pet_ble_pairing_mode(void);

/* B2：通过 BLE TX(notify) 下发控制行；未连接返回 false。 */
bool pet_ble_send_line(const char *line);
