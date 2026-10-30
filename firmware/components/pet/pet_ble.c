/* M16 B1：NimBLE 外设广播基线。
 * 只做 init + 广播（名称 ESP32-Buddy-XXXX，MAC 后缀），不注册服务、不连接。
 * 目的：与 WiFi STA 共存基线（官方矩阵：STA+BLE=稳定；WiFi+BLE 同时连接时射频 50/50 分时）。
 * 日志一律 ASCII。 */
#include "pet_ble.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/util/util.h"
#include "host/ble_store.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_mbuf.h"
#include "pet_linebuf.h"
#include "pet_rx.h"
#include "pet_net.h"
#include "pm1.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "pet-ble";

static char    s_name[24];
static int     s_started;
static uint16_t s_conn_handle;
static uint16_t s_tx_handle;
static uint16_t s_rx_handle;
static uint16_t s_status_handle;
static bool     s_status_subscribed;
static esp_timer_handle_t s_hb_timer;
#define BLE_HB_INTERVAL_US (10 * 1000 * 1000)
static pet_linebuf_t s_rx_lb;

/* B2：GATT 服务 0xFFE0；RX 0xFFE1(写事件行) / TX 0xFFE2(notify 控制行) */
static int ble_rx_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    uint8_t buf[64];
    uint16_t off = 0;
    while (off < len) {
        uint16_t chunk = (uint16_t)((len - off > sizeof buf) ? sizeof buf : (len - off));
        if (os_mbuf_copydata(ctxt->om, off, chunk, buf) != 0) {
            break;
        }
        for (uint16_t i = 0; i < chunk; i++) {
            if (pet_linebuf_feed(&s_rx_lb, (char)buf[i]) == PET_LINE_READY) {
                pet_rx_submit(s_rx_lb.buf, s_rx_lb.len);
            }
        }
        off += chunk;
    }
    return 0;
}

static int ble_tx_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)ctxt; (void)arg;
    return BLE_ATT_ERR_UNLIKELY; /* TX 只做 notify */
}

static const struct ble_gatt_svc_def ble_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0xFFE0),
        .characteristics = (struct ble_gatt_chr_def[]){
            { .uuid = BLE_UUID16_DECLARE(0xFFE1),
              .access_cb = ble_rx_access,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                       BLE_GATT_CHR_F_WRITE_ENC,
              .val_handle = &s_rx_handle },
            { .uuid = BLE_UUID16_DECLARE(0xFFE2),
              .access_cb = ble_tx_access,
              .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
              .val_handle = &s_tx_handle },
            { .uuid = BLE_UUID16_DECLARE(0xFFE3),
              .access_cb = ble_tx_access,
              .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_NOTIFY_INDICATE_ENC,
              .val_handle = &s_status_handle },
            { 0 } },
    },
    { 0 },
};

static bool ble_notify_handle(uint16_t handle, const char *line)
{
    if (line == NULL || s_conn_handle == 0 || handle == 0) {
        return false;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(line, (uint16_t)strlen(line));
    if (om == NULL) {
        return false;
    }
    return ble_gatts_notify_custom(s_conn_handle, handle, om) == 0;
}

/* M17：握手(hello，订阅时一次) + 心跳(hb，每 10s) */
static void ble_send_status(const char *kind)
{
    if (!s_status_subscribed) {
        return;
    }
    uint16_t mv = 0;
    int pct = -1;
    bool chg = false;
    (void)pm1_battery(&mv, &pct, &chg);
    char ip[16];
    pet_net_get_ip(ip, sizeof ip);
    char line[192];
    snprintf(line, sizeof line,
             "{\"v\":1,\"evt\":\"%s\",\"name\":\"%s\",\"uptime_s\":%lld,"
             "\"batt\":%d,\"chg\":%d,\"wifi\":%d,\"ip\":\"%s\"}\n",
             kind, s_name, (long long)(esp_timer_get_time() / 1000000),
             pct, chg ? 1 : 0, ip[0] != '\0' ? 1 : 0, ip);
    (void)ble_notify_handle(s_status_handle, line);
    ESP_LOGI(TAG, "ble: %s sent (batt=%d wifi=%d)", kind, pct, ip[0] != '\0' ? 1 : 0);
}

static void ble_hb_timer_cb(void *arg)
{
    (void)arg;
    ble_send_status("hb");
}

bool pet_ble_send_line(const char *line)
{
    if (line == NULL || s_conn_handle == 0 || s_tx_handle == 0) {
        return false;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(line, (uint16_t)strlen(line));
    if (om == NULL) {
        return false;
    }
    const int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_handle, om);
    return rc == 0;
}
static uint8_t s_own_addr_type;

static void ble_advertise(void);

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        const bool ok = (event->connect.status == 0);
        if (ok) {
            s_conn_handle = event->connect.conn_handle;
        }
        ESP_LOGI(TAG, "ble: connect %s handle=%d", ok ? "ok" : "fail", event->connect.conn_handle);
        if (!ok) {
            ble_advertise();
        }
        break;
    }
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "ble: disconnect reason=0x%02x", event->disconnect.reason);
        s_conn_handle = 0;
        s_tx_handle = 0;
        s_status_subscribed = false;
        ble_advertise();
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "ble: enc change status=%d", event->enc_change.status);
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_status_handle) {
            s_status_subscribed = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "ble: status subscribe=%d", s_status_subscribed);
            if (s_status_subscribed) {
                ble_send_status("hello");
            }
        }
        break;
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            (void)ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
        break;
    }
    return 0;
}

static void ble_advertise(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof fields);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)s_name;
    fields.name_len = (uint8_t)strlen(s_name);
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble: adv fields rc=%d", rc);
        return;
    }

    struct ble_gap_adv_params params;
    memset(&params, 0, sizeof params);
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params,
                           ble_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble: adv start rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "ble: advertising as %s", s_name);
    }
}

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble: ensure addr rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble: addr type rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "ble: handles rx/tx/status=%u/%u/%u", (unsigned)s_rx_handle,
             (unsigned)s_tx_handle, (unsigned)s_status_handle);
    ble_advertise();
}

static void ble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool pet_ble_pairing_mode(void)
{
    if (!s_started) {
        return false;
    }
    (void)ble_store_clear(); /* 清绑定：PC 需重新配对 */
    (void)ble_gap_adv_stop();
    ble_advertise();
    ESP_LOGI(TAG, "ble: pairing mode (bonds cleared, advertising)");
    return true;
}

void pet_ble_start(void)
{
    uint8_t mac[6];
    (void)esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_name, sizeof s_name, "ESP32-Buddy-%02X%02X", mac[4], mac[5]);

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ble: nimble init %s", esp_err_to_name(err));
        return;
    }
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT; /* Just Works */
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    (void)ble_svc_gap_device_name_set(s_name);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    pet_linebuf_init(&s_rx_lb);
    {
        const int n = ble_gatts_count_cfg(ble_gatt_svcs);
        const int a = ble_gatts_add_svcs(ble_gatt_svcs);
        ESP_LOGI(TAG, "ble: gatt count=%d add=%d", n, a);
    }
    nimble_port_freertos_init(ble_host_task);
    if (s_hb_timer == NULL) {
        const esp_timer_create_args_t targs = {.callback = ble_hb_timer_cb,
                                               .name = "ble_hb"};
        if (esp_timer_create(&targs, &s_hb_timer) == ESP_OK) {
            (void)esp_timer_start_periodic(s_hb_timer, BLE_HB_INTERVAL_US);
        }
    }
    s_started = 1;
    ESP_LOGI(TAG, "ble: start name=%s", s_name);
}
