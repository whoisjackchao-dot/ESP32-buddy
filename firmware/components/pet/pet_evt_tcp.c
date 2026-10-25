/* M13 增量：WiFi TCP 事件通道（STA :8766，单客户端、顺序 accept）。
 * 每行 → pet_rx_submit()（与 USB 共用队列；消费/去重/状态变更在 pet-rx-apply）。
 * 阻塞与停机策略：不跨任务 close 正阻塞中的套接字 —— 监听套接字与客户端套接字
 * 都设 SO_RCVTIMEO，任务周期性（≤0.5 s / ≤2 s）自查 s_enabled 后自行收尾退出。
 * 任何 socket 错误只记日志（限频不在此处，错误路径本身低频），关套接字重来或
 * 回落驻留，绝不崩；无 WiFi（未使能）时任务驻留轮询，不占端口不占连接。
 * 契约：evidence/2026-10-25-双通道事件入口-M13.md */
#include "pet_evt_tcp.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "pet_linebuf.h"
#include "pet_net.h"
#include "pet_rx.h"

/* 日志与 pet_net 同 TAG，行文保持 "net: ..." 风格（ASCII）。 */
static const char *TAG = "pet-net";

#define EVT_PORT 8766
#define EVT_ACCEPT_TIMEOUT_MS 500 /* 监听套接字 SO_RCVTIMEO：accept 轮询周期 */
#define EVT_RECV_TIMEOUT_MS 2000  /* 客户端套接字 SO_RCVTIMEO：静默自查周期 */
#define EVT_CHUNK 128

static volatile bool s_enabled; /* true=请求监听（STA 有 IP 期间） */
static volatile bool s_task_up; /* 监听任务只创建一次 */

/* 超时类错误（accept/recv 的 SO_RCVTIMEO 到期在 lwip 映射为 EAGAIN/EWOULDBLOCK/
 * ETIMEDOUT；EINTR 一并视为瞬态）：回查 s_enabled 后继续。 */
static bool sock_wait_err(int e)
{
    return e == EAGAIN || e == EWOULDBLOCK || e == ETIMEDOUT || e == EINTR;
}

/* 单个客户端：recv → pet_linebuf 切行 → pet_rx_submit。返回即关闭连接。 */
static void evt_serve_client(int cfd)
{
    const int rcv_ms = EVT_RECV_TIMEOUT_MS;
    (void)lwip_setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &rcv_ms, sizeof rcv_ms);

    pet_linebuf_t lb;
    pet_linebuf_init(&lb);

    uint8_t chunk[EVT_CHUNK];
    for (;;) {
        const int n = lwip_recv(cfd, chunk, sizeof chunk, 0);
        if (n == 0) {
            return; /* 客户端正常断开 */
        }
        if (n < 0) {
            if (sock_wait_err(errno)) {
                if (!s_enabled) {
                    return; /* 静默自查：已请求停机 */
                }
                continue;
            }
            ESP_LOGW(TAG, "net: event client recv err errno=%d", errno);
            return;
        }
        for (int i = 0; i < n; i++) {
            const pet_line_feed_t r = pet_linebuf_feed(&lb, (char)chunk[i]);
            if (r == PET_LINE_DROPPED) {
                ESP_LOGW(TAG, "net: event line too long (dropped)");
            } else if (r == PET_LINE_READY && lb.buf[0] != '\0') {
                pet_rx_submit(lb.buf, lb.len);
            }
        }
    }
}

static void evt_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!s_enabled) {
            vTaskDelay(pdMS_TO_TICKS(EVT_ACCEPT_TIMEOUT_MS)); /* 驻留等待下次 start */
            continue;
        }

        const int lfd = lwip_socket(AF_INET, SOCK_STREAM, 0);
        if (lfd < 0) {
            ESP_LOGE(TAG, "net: event server socket failed errno=%d", errno);
            vTaskDelay(pdMS_TO_TICKS(EVT_ACCEPT_TIMEOUT_MS));
            continue;
        }
        const int on = 1;
        (void)lwip_setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
        const int acc_ms = EVT_ACCEPT_TIMEOUT_MS;
        (void)lwip_setsockopt(lfd, SOL_SOCKET, SO_RCVTIMEO, &acc_ms, sizeof acc_ms);

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof addr);
        addr.sin_family = AF_INET;
        addr.sin_port = lwip_htons(EVT_PORT);
        addr.sin_addr.s_addr = INADDR_ANY; /* 0.0.0.0：STA netif 到达即可 */

        if (lwip_bind(lfd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
            lwip_listen(lfd, 1) != 0) {
            ESP_LOGE(TAG, "net: event server bind/listen failed errno=%d", errno);
            (void)lwip_close(lfd);
            vTaskDelay(pdMS_TO_TICKS(EVT_ACCEPT_TIMEOUT_MS));
            continue;
        }

        char ip[16];
        pet_net_get_ip(ip, sizeof ip);
        ESP_LOGI(TAG, "net: event server tcp://%s:%d", ip, EVT_PORT);

        while (s_enabled) {
            /* lwip：SO_RCVTIMEO 同样作用于 accept 的信箱等待 → 周期性回到这里
             * 自查 s_enabled，无需跨任务 close。 */
            const int cfd = lwip_accept(lfd, NULL, NULL);
            if (cfd < 0) {
                if (!sock_wait_err(errno)) {
                    ESP_LOGW(TAG, "net: event accept err errno=%d", errno);
                }
                continue;
            }
            evt_serve_client(cfd); /* 单客户端：服务完（断开/停机/错误）再 accept */
            (void)lwip_close(cfd);
            ESP_LOGI(TAG, "net: event client closed");
        }

        (void)lwip_close(lfd);
        ESP_LOGI(TAG, "net: event server stopped"); /* 每次监听会话恰好一条 */
    }
}

void pet_evt_tcp_start(void)
{
    if (!s_task_up) {
        if (xTaskCreate(evt_task, "pet-evt", 4096, NULL, 4, NULL) != pdPASS) {
            ESP_LOGE(TAG, "net: event server task create failed");
            return;
        }
        s_task_up = true;
    }
    s_enabled = true; /* 已在监听时重复 start（IP 变更）为空操作 */
}

void pet_evt_tcp_stop(void)
{
    s_enabled = false; /* 任务自查后自行 close + 打 "net: event server stopped" */
}
