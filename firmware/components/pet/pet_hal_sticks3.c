/* M5StickS3 硬件层（pet_hal 的唯一实现）。
 * 低层 ST7789/M5PM1 的引脚、偏移、反色、字节序等事实复用同作者既有
 * StickS3×OpenCode 项目（MIT）中已在实机验证的实现（其 M2 evidence）。
 *
 * 关键事实（不可凭相似板推测，见 M5StickS3_硬件知识文档 §3）：
 *   屏 ST7789P3 135×240，SPI3，MOSI39/SCK40/DC45/CS41/RST21/BL38，
 *   gap(52,40)，反色开，40MHz，mode0；
 *   L3B 供电轨（PM1 GPIO2）必须先开并等待，否则屏不亮；
 *   ST7789 以 MSB 先出，而 uint16 为小端 → present 时逐像素交换字节。
 */
#include "pet_hal.h"
#include "pet_hal_sticks3.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pm1.h"

static const char *TAG = "pet-hal";

#define DISP_MOSI 39
#define DISP_SCK  40
#define DISP_DC   45
#define DISP_CS   41
#define DISP_RST  21
#define DISP_BL   38
#define DISP_W    135
#define DISP_H    240
#define DISP_GAP_X 52
#define DISP_GAP_Y 40

#define PM1_I2C_SDA 47
#define PM1_I2C_SCL 48

static esp_lcd_panel_handle_t s_panel;
/* 面板原始缓冲：字节序为 MSB 先出（与 ST7789 一致） */
static uint8_t s_panel_buf[DISP_W * DISP_H * 2];
static pet_hal_t s_hal;

/* ---- pet_hal 实现 ---- */

static uint32_t sticks3_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void sticks3_present(const pet_surface_t *surface)
{
    if (s_panel == NULL || surface == NULL || surface->pixels == NULL) {
        return;
    }
    /* surface 为 RGB565 little-endian；面板要 MSB 先出 → 交换高低字节。
     * 不做交换会导致 G/B 位互换（黄显示成紫）。 */
    const uint8_t *src = surface->pixels;
    const int n = DISP_W * DISP_H;
    for (int i = 0; i < n; i++) {
        s_panel_buf[i * 2]     = src[i * 2 + 1];
        s_panel_buf[i * 2 + 1] = src[i * 2];
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISP_W, DISP_H, s_panel_buf);
}

static void sticks3_set_backlight(bool on)
{
    gpio_set_level(DISP_BL, on ? 1 : 0);
}

static void sticks3_play_sound(pet_sound_t sound)
{
    (void)sound; /* 语音属 M5，本节点不做 */
}

static bool sticks3_audio_busy(void)
{
    return false; /* 有音频后由 M5 实现 */
}

/* ---- 硬件安装 ---- */

static esp_err_t i2c_install(void)
{
    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PM1_I2C_SDA,
        .scl_io_num = PM1_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    esp_err_t err = i2c_param_config(I2C_NUM_0, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    return i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
}

static esp_err_t panel_bringup(void)
{
    /* 1. L3B 供电轨（LCD/MIC/SPK），开后等待 */
    esp_err_t err = pm1_l3b_lcd(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "L3B 上电失败：%s", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(20));

    /* 2. 背光 GPIO */
    const gpio_config_t bl = {
        .pin_bit_mask = 1ULL << DISP_BL,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&bl);
    gpio_set_level(DISP_BL, 1);

    /* 3. SPI 总线 + ST7789 */
    const spi_bus_config_t bus = {
        .mosi_io_num = DISP_MOSI,
        .sclk_io_num = DISP_SCK,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .max_transfer_sz = DISP_W * DISP_H * 2 + 8,
    };
    err = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI 总线初始化失败：%s", esp_err_to_name(err));
        return err;
    }

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = DISP_DC,
        .cs_gpio_num = DISP_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    esp_lcd_panel_io_handle_t io = NULL;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel io 失败：%s", esp_err_to_name(err));
        return err;
    }

    const esp_lcd_panel_dev_config_t dev = {
        .reset_gpio_num = DISP_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(io, &dev, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "st7789 创建失败：%s", esp_err_to_name(err));
        return err;
    }

    if ((err = esp_lcd_panel_reset(s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_init(s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_invert_color(s_panel, true)) != ESP_OK ||
        (err = esp_lcd_panel_set_gap(s_panel, DISP_GAP_X, DISP_GAP_Y)) != ESP_OK ||
        (err = esp_lcd_panel_disp_on_off(s_panel, true)) != ESP_OK) {
        ESP_LOGE(TAG, "面板初始化序列失败：%s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t pet_hal_sticks3_install(void)
{
    if (i2c_install() != ESP_OK) {
        ESP_LOGE(TAG, "I2C 安装失败");
        return ESP_FAIL;
    }
    if (pm1_init() != ESP_OK) {
        ESP_LOGE(TAG, "M5PM1 初始化失败");
        return ESP_FAIL;
    }
    if (panel_bringup() != ESP_OK) {
        return ESP_FAIL;
    }

    s_hal.now_ms        = sticks3_now_ms;
    s_hal.present       = sticks3_present;
    s_hal.set_backlight = sticks3_set_backlight;
    s_hal.play_sound    = sticks3_play_sound;
    s_hal.audio_busy    = sticks3_audio_busy;
    s_hal.panel_width   = DISP_W;
    s_hal.panel_height  = DISP_H;
    pet_hal_install(&s_hal);

    /* 清屏（面板原始字节序，黑色 = 0） */
    memset(s_panel_buf, 0, sizeof(s_panel_buf));
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, DISP_W, DISP_H, s_panel_buf);

    ESP_LOGI(TAG, "ST7789 %dx%d gap(%d,%d) invert 背光开", DISP_W, DISP_H,
             DISP_GAP_X, DISP_GAP_Y);
    return ESP_OK;
}
