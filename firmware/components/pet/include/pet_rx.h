#pragma once

/* M4 增量 2b：设备侧从 USB-Serial-JTAG 收事件行（契约 §9.2）。
 * M13 增量：USB 与 WiFi TCP 双通道统一入队，单一消费任务应用（见 pet_rx.c）。 */
#include <stddef.h>

struct pet_state;

/* 启动收件：USB-Serial-JTAG 生产任务 + 唯一消费任务（pet-rx-apply）。
 * 消费任务做 跨通道去重 → pet_link_parse → pet_state。
 * s 为 pet_engine 持有的状态机。 */
void pet_rx_start(struct pet_state *s);

/* M13：任意任务上下文提交一行事件（线程安全；整行拷贝入队，不阻塞调用方）。
 * 队列深度 16、行 ≤ PET_LINE_MAX；满/未就绪/超长 → 丢弃 + 限频日志。
 * 与 USB 路径共用消费与去重；不直接改 pet_state。 */
void pet_rx_submit(const char *line, size_t len);
