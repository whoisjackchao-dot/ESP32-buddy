#pragma once

#include "esp_err.h"

/* M5StickS3 硬件层安装：I2C → M5PM1 → LCD(L3B+背光+SPI+ST7789)，
 * 并把 pet_hal_t 装进 pet_hal。返回 ESP_OK 表示硬件通路就绪。 */
esp_err_t pet_hal_sticks3_install(void);
