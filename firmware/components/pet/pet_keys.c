/* M6：按键采样任务（KEY1=GPIO11 / KEY2=GPIO12，低电平=按下）。
 * 契约：contracts/M6-按键映射契约清单.md §7.1（20 ms 采样、首次翻转）。
 * 短/长按判定在 pet_button；分派在 pet_engine_key（菜单优先）。 */
#include "pet_keys.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pet_button.h"
#include "pet_engine.h"
#include "pet_hal.h"

#define KEY1_GPIO 11
#define KEY2_GPIO 12

static const char *TAG = "pet-keys";

static void feed_one(pet_button_t *b, pet_key_t key, int gpio)
{
    const bool pressed = (gpio_get_level(gpio) == 0); /* 低电平=按下 */
    const pet_key_event_t ev = pet_button_update(b, pressed, pet_hal_now_ms());
    if (ev != PET_KEY_EV_NONE) {
        pet_engine_key(key, ev);
    }
}

static void keys_task(void *arg)
{
    (void)arg;
    pet_button_t b1;
    pet_button_t b2;
    pet_button_init(&b1);
    pet_button_init(&b2);

    const TickType_t period = pdMS_TO_TICKS(KEY_DEBOUNCE_MS);
    for (;;) {
        feed_one(&b1, PET_KEY1, KEY1_GPIO);
        feed_one(&b2, PET_KEY2, KEY2_GPIO);
        vTaskDelay(period);
    }
}

void pet_keys_start(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << KEY1_GPIO) | (1ULL << KEY2_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ONLY,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&io) != ESP_OK) {
        ESP_LOGE(TAG, "按键 GPIO 配置失败");
        return;
    }
    if (xTaskCreate(keys_task, "keys", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "按键任务创建失败");
    }
}
