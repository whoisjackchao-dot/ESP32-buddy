#pragma once

/* M16 B1：BLE 广播基线（NimBLE 外设，仅广播，无 GATT 服务）。
 * 目的：验证 BLE 与 WiFi STA 共存、flash/RAM 增量、WiFi 时延/吞吐基线。
 * 契约：ESP32-buddy_BLE与WiFi双连接方案.md §3 B1。
 * 注意：B1 只广播；事件/控制通道仍在 USB / WiFi。 */
void pet_ble_start(void);
