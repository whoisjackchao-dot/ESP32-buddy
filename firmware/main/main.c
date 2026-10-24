/* ESP32-buddy M1 入口：把三层接起来，在 M5StickS3 上显示宠物一帧。
 * M1 范围：只显示 IDLE/NORMAL 一帧；状态机与动画属 M2。 */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "pet_engine.h"
#include "pet_hal_sticks3.h"
#include "pet_keys.h"
#include "pet_net.h"
#include "pet_rx.h"

static const char *TAG = "esp32-buddy";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-buddy M1 启动");

    esp_err_t nv = nvs_flash_init();
    if (nv == ESP_ERR_NVS_NO_FREE_PAGES || nv == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        (void)nvs_flash_erase();
        nv = nvs_flash_init();
    }
    if (nv != ESP_OK) {
        ESP_LOGW(TAG, "NVS 初始化失败：%d（番茄钟统计不落盘）", (int)nv);
    }

    /* M8 增量 2：WiFi STA + SNTP（非阻塞；无凭据时行为与此前完全一致） */
    pet_net_start();

    if (pet_hal_sticks3_install() != ESP_OK) {
        ESP_LOGE(TAG, "DISPLAY ERROR（硬件层安装失败）");
        /* 不挂死：继续跑心跳，便于从日志定位 */
    } else {
        ESP_LOGI(TAG, "DISPLAY READY M1");
    }

    pet_engine_init();
    pet_engine_redraw();
    pet_engine_start();
    pet_rx_start(pet_engine_state());
    pet_keys_start();
    ESP_LOGI(TAG, "PIXEL CARD READY（宠物已显示，动画节拍启动）");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "hb: uptime=%llds heap=%lu",
                 (long long)(esp_timer_get_time() / 1000000),
                 (unsigned long)esp_get_free_heap_size());
    }
}
