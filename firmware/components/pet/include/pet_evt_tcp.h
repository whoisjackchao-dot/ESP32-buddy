#pragma once

/* M13：WiFi TCP 事件通道（STA 下 TCP :8766）。
 * STA 拿到 IP 后顺序 accept 单个客户端，按行切分（与 USB 同款 pet_linebuf 语义，
 * 超长整行丢弃），每行交 pet_rx_submit() —— 与 USB 共用同一条队列/消费/去重路径。
 * 不直接触碰 pet_state；不新增每连接线程。 */

/* 请求启动监听（幂等；任意任务上下文可调；监听任务只创建一次，
 * 之后仅在使能时监听。未连 WiFi 时调用无效）。 */
void pet_evt_tcp_start(void);

/* 请求停止监听（幂等；监听任务 ≤0.5 s 内自查到后自行关套接字并回落驻留）。 */
void pet_evt_tcp_stop(void);
