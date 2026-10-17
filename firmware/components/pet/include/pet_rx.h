#pragma once

/* M4 增量 2b：设备侧从 USB-Serial-JTAG 收事件行（契约 §9.2）。 */
struct pet_state;

/* 启动收件任务：读 USB-Serial-JTAG 字节 → 行缓冲 → pet_link_parse → pet_state。
 * s 为 pet_engine 持有的状态机。 */
void pet_rx_start(struct pet_state *s);
