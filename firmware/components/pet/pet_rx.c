/* M4 增量 2b：设备侧收件（USB-Serial-JTAG → 事件行 → pet_state）。
 * 契约：contracts/M4-事件入口与OpenCode接入契约清单.md §9.1/§9.2。
 * 解析失败/超长：记 warn、丢弃、不改状态、不崩。 */
#include "pet_rx.h"

#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pet_hal.h"
#include "pet_linebuf.h"
#include "pet_engine.h"
#include "pet_link.h"
#include "pet_net.h"
#include "pet_state.h"
#include "pet_types.h"

static const char *TAG = "pet-rx";

static struct pet_state *s_state;

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
                apply_line(lb.buf, lb.len);
            }
        }
    }
}

void pet_rx_start(struct pet_state *s)
{
    s_state = s;

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
