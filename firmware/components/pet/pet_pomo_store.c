/* M7 增量 2：番茄钟统计落盘（NVS）。
 * 契约：contracts/M7-番茄钟契约清单.md §7.3/§7.4。
 * 只存当日统计（day/done_count/focus_seconds）；缺失或损坏则用默认值，不崩。 */
#include "pet_pomo_store.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "pet-pomo-nvs";
static const char *NVS_NS = "pomo";
static const char *NVS_KEY = "stats";

void pet_pomo_nvs_load(pet_pomo_t *p)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return; /* 尚无命名空间：保留默认（p 由 pet_pomo_init 初始化） */
    }

    uint8_t buf[PET_POMO_STATS_LEN];
    size_t len = sizeof buf;
    const esp_err_t err = nvs_get_blob(h, NVS_KEY, buf, &len);
    nvs_close(h);

    if (err != ESP_OK) {
        return; /* 没有键：保留默认 */
    }
    if (!pet_pomo_decode_stats(buf, len, p)) {
        ESP_LOGW(TAG, "统计 blob 非法，忽略");
    }
}

void pet_pomo_nvs_save(const pet_pomo_t *p)
{
    uint8_t buf[PET_POMO_STATS_LEN];
    const size_t n = pet_pomo_encode_stats(buf, sizeof buf, p);
    if (n == 0) {
        return;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open 失败，统计未落盘");
        return;
    }
    if (nvs_set_blob(h, NVS_KEY, buf, n) == ESP_OK) {
        (void)nvs_commit(h);
    }
    nvs_close(h);
}
