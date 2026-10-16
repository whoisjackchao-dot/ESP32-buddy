/* ESP32-buddy M1 入口：把三层接起来，在 M5StickS3 上显示宠物一帧。
 * M1 范围：只显示 IDLE/NORMAL 一帧；状态机与动画属 M2。 */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pet_engine.h"
#include "pet_hal_sticks3.h"

static const char *TAG = "esp32-buddy";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-buddy M1 启动");

    if (pet_hal_sticks3_install() != ESP_OK) {
        ESP_LOGE(TAG, "DISPLAY ERROR（硬件层安装失败）");
        /* 不挂死：继续跑心跳，便于从日志定位 */
    } else {
        ESP_LOGI(TAG, "DISPLAY READY M1");
    }

    pet_engine_init();
    pet_engine_redraw();
    ESP_LOGI(TAG, "PIXEL CARD READY（宠物一帧已显示）");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "hb: uptime=%llds heap=%lu",
                 (long long)(esp_timer_get_time() / 1000000),
                 (unsigned long)esp_get_free_heap_size());
    }
}
