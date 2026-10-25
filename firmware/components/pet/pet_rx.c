/* M4 增量 2b：设备侧收件（USB-Serial-JTAG → 事件行 → pet_state）。
 * M13 增量：统一事件入口 —— USB 与 WiFi TCP 两条路径都只经 pet_rx_submit()
 * 入队（行副本、有界队列），由唯一消费任务 pet-rx-apply 做跨通道去重后
 * 才执行 apply_line；pet_state 从此只被一个任务改写。
 * 契约：contracts/M4-事件入口与OpenCode接入契约清单.md §9.1/§9.2；
 * evidence/2026-10-25-双通道事件入口-M13.md。
 * 解析失败/超长/队列满/重复行：记 warn、丢弃、不改状态、不崩。 */
#include "pet_rx.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "pet_hal.h"
#include "pet_linebuf.h"
#include "pet_engine.h"
#include "pet_link.h"
#include "pet_net.h"
#include "pet_state.h"
#include "pet_types.h"

static const char *TAG = "pet-rx";

#define PET_RX_QUEUE_DEPTH 16         /* 行副本队列深度（固定；满即丢 + 限频日志） */
#define PET_RX_DUP_WINDOW_US 500000LL /* 跨通道去重窗口：与上一条已应用行相同且 <500 ms */
#define PET_RX_WARN_PERIOD_US 2000000LL /* 限频告警周期 2 s */

/* 队列元素：整行副本（NUL 结尾）。pet_linebuf 保证 READY 行 len ≤ PET_LINE_MAX。 */
typedef struct {
    size_t len;
    char   line[PET_LINE_MAX + 1];
} pet_rx_msg_t;

static struct pet_state *s_state;
static QueueHandle_t s_line_q;

/* ---- 限频告警（只用于可刷屏的丢弃类日志；竞态最坏多打一条，无害） ---- */
static void warn_throttled(const char *msg, int64_t *last_us)
{
    const int64_t now = esp_timer_get_time();
    if (now - *last_us >= PET_RX_WARN_PERIOD_US) {
        ESP_LOGW(TAG, "%s", msg);
        *last_us = now;
    }
}

static void apply_line(const char *line, size_t len)
{
#ifdef PET_TEST_CLOCK
    /* F8：测试构建专用时钟注入（设备侧时间回拨验证，不进入发布固件） */
    if (strstr(line, "\"device.test.pomo\"") != NULL) {
        const char *p = strstr(line, "\"cmd\":");
        if (p != NULL) {
            pet_engine_test_pomo((int)strtol(p + 6, NULL, 10));
        }
        return;
    }
    if (strstr(line, "\"device.test.clock\"") != NULL) {
        const char *p = strstr(line, "\"offset_ms\":");
        if (p != NULL) {
            pet_hal_test_set_offset_ms((int32_t)strtol(p + 12, NULL, 10));
        }
        return;
    }
#endif
    /* M8 增量 2：产线 cmd 线（wifi.provision/status/clear）在事件解析前截获；
     * 未识别的 cmd 返回 false，继续走原事件解析（照旧拒绝）。 */
    if (strstr(line, "\"cmd\"") != NULL && pet_net_handle_line(line)) {
        return;
    }
    pet_link_msg_t msg;
    const pet_link_status_t st = pet_link_parse(line, len, &msg);
    if (st != PET_LINK_OK) {
        ESP_LOGW(TAG, "link parse err=%d（已丢弃）", (int)st);
        return;
    }

    pet_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.kind = msg.kind;
    ev.now_ms = pet_hal_now_ms();
    pet_state_apply(s_state, &ev);
    if (msg.card_count > 0) {
        pet_state_set_cards(s_state, msg.cards, msg.card_count); /* M8 §8.2：整栈替换 */
    } else if (msg.has_card) {
        pet_state_set_card(s_state, &msg.card); /* 顶层单卡（M4 兼容） */
    }
}

/* ---- M13：唯一消费任务：出队 → 去重 → apply_line（状态变更单线程化） ---- */
static void rx_apply_task(void *arg)
{
    (void)arg;
    static char last_line[PET_LINE_MAX + 1]; /* 上一条已应用行（仅本任务访问） */
    static size_t last_len;
    static int64_t last_us;
    static int64_t dup_log_us;

    pet_rx_msg_t m;
    for (;;) {
        if (xQueueReceive(s_line_q, &m, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const int64_t now = esp_timer_get_time();
        if (last_len > 0 && m.len == last_len &&
            memcmp(last_line, m.line, m.len) == 0 &&
            now - last_us < PET_RX_DUP_WINDOW_US) {
            /* USB+WiFi 双投递去重：与上一条**已应用**行全等且落在窗口内 → 丢 */
            warn_throttled("rx: dup dropped", &dup_log_us);
            continue;
        }
        apply_line(m.line, m.len);
        memcpy(last_line, m.line, m.len);
        last_line[m.len] = '\0';
        last_len = m.len;
        last_us = now;
    }
}

/* M13 公共入口：任意任务上下文提交一行事件（线程安全；拷贝入队）。
 * 队列满/未就绪/超长：丢弃 + 限频日志，绝不阻塞调用方。 */
void pet_rx_submit(const char *line, size_t len)
{
    static int64_t full_log_us; /* USB/TCP 两个生产者共用（最坏竞态=重复一条日志） */
    if (line == NULL || len == 0 || len > PET_LINE_MAX) {
        return;
    }
    if (s_line_q == NULL) {
        warn_throttled("rx: queue not ready (dropped)", &full_log_us);
        return;
    }
    pet_rx_msg_t m;
    memcpy(m.line, line, len);
    m.line[len] = '\0';
    m.len = len;
    if (xQueueSend(s_line_q, &m, 0) != pdTRUE) {
        warn_throttled("rx: queue full (dropped)", &full_log_us);
    }
}

/* USB 生产任务：USB-Serial-JTAG 字节 → 行缓冲 → 入队（不直接改状态）。
 * WiFi 在不在都走同一条路；无 WiFi 时行为与旧版逐行 apply 完全一致（仅多一跳队列）。 */
static void rx_task(void *arg)
{
    (void)arg;
    pet_linebuf_t lb;
    pet_linebuf_init(&lb);

    uint8_t chunk[128];
    for (;;) {
        const int n = usb_serial_jtag_read_bytes(chunk, sizeof chunk, pdMS_TO_TICKS(100));
        if (n <= 0) {
            continue;
        }
        for (int i = 0; i < n; i++) {
            const pet_line_feed_t r = pet_linebuf_feed(&lb, (char)chunk[i]);
            if (r == PET_LINE_DROPPED) {
                ESP_LOGW(TAG, "line too long（已丢弃）");
            } else if (r == PET_LINE_READY && lb.buf[0] != '\0') {
                pet_rx_submit(lb.buf, lb.len);
            }
        }
    }
}

void pet_rx_start(struct pet_state *s)
{
    s_state = s;

    if (s_line_q == NULL) {
        s_line_q = xQueueCreate(PET_RX_QUEUE_DEPTH, sizeof(pet_rx_msg_t));
        if (s_line_q == NULL) {
            ESP_LOGE(TAG, "rx: queue create failed");
            return; /* 降级：设备照常跑，只丢事件入口 */
        }
    }
    if (xTaskCreate(rx_apply_task, "pet-rx-apply", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "rx: apply task create failed");
        return;
    }

    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = 256,
        .rx_buffer_size = 1024,
    };
    const esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "usb_serial_jtag 驱动安装失败: %d", (int)err);
        return;
    }

    if (xTaskCreate(rx_task, "pet-rx", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "收件任务创建失败");
    }
}
