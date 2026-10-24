#pragma once

#include <stdbool.h>

/* M8 增量 2：WiFi STA + SNTP 授时（Q17=A 仅授时）。
 * 非阻塞：pet_net_start 只建任务；无凭据 / WiFi 失败时设备行为与此前完全一致。 */

/* 启动网络任务：读 NVS 凭据，有则连 WiFi + SNTP；无则打 "net: no credentials" 停车。 */
void pet_net_start(void);

/* 设备侧产线 cmd 行（wifi.set / wifi.status / wifi.clear）；命中并处理返回 true。 */
bool pet_net_handle_line(const char *line);
