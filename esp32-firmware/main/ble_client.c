/*
 * ble_client.c — NimBLE GATT 中心实现（笔记 §B1）
 *
 * 对应 ESP-IDF v5.x 的 NimBLE host API（esp_nimble_hci / nimble_port / ble_hs /
 * ble_gattc / ble_gap）。真机未验证，结构按 GATT 发现惯例编写。
 */
#include "ble_client.h"
#include "config.h"
#include "log_tags.h"
#include "sar.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* 小米私有 UUID：16 位短 UUID 扩展为全 UUID 时，base 为
 * 0000xxxx-0000-1000-8000-00805f9b34fb */
#define XIAOMI_SERVICE_UUID16   0xFE95
#define CHAR_TX_UUID16          0x005F   /* 写（下行） */
#define CHAR_RX_UUID16          0x005E   /* 通知（上行） */
#define CHAR_SVC_UUID16         0x0050   /* 读（控制点） */

static ble_rx_fn   s_rx;
static void       *s_rx_ctx;
static ble_event_fn s_ev;
static void       *s_ev_ctx;
static uint8_t     s_target[6];

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_attr_tx = 0;   /* 0x005f */
static uint16_t s_attr_rx = 0;   /* 0x005e */
static uint16_t s_attr_svc = 0;   /* 0x0050 */
static uint16_t s_mtu = 0;

bool ble_bridge_connected(void) { return s_conn_handle != BLE_HS_CONN_HANDLE_NONE; }
uint16_t ble_bridge_max_write_len(void)
{
    /* 有效负载 = MTU - 3（ATT 头） */
    if (s_mtu > 4) return s_mtu - 3;
    return BR_DEFAULT_MAX_WRITE;
}

/* ---------- GATT 发现 ---------- */
static int gatt_disc_cb(uint16_t conn, const struct ble_gatt_error *err,
                        const struct ble_gatt_chr *chr, void *arg)
{
    if (err && err->status != 0) {
        ESP_LOGE(TAG_BLE_CONN, "chr disc err=%d", err->status);
        return 0;
    }
    if (chr) {
        /* 按 16 位 UUID 匹配 */
        uint16_t u16 = 0;
        if (chr->uuid.u.type == BLE_UUID_TYPE_16) u16 = chr->uuid.u16.value;
        if (u16 == CHAR_TX_UUID16) {
            s_attr_tx = chr->val_handle;
            ESP_LOGI(TAG_BLE_CONN, "found TX(write) handle=%u", s_attr_tx);
        } else if (u16 == CHAR_RX_UUID16) {
            s_attr_rx = chr->val_handle;
            ESP_LOGI(TAG_BLE_CONN, "found RX(notify) handle=%u", s_attr_rx);
        } else if (u16 == CHAR_SVC_UUID16) {
            s_attr_svc = chr->val_handle;
            ESP_LOGI(TAG_BLE_CONN, "found SVC(read) handle=%u", s_attr_svc);
        }
    }
    return 0;
}

static void subscribe_rx_notify(void)
{
    if (!s_attr_rx) return;
    uint16_t cccd = 1; /* notify */
    ble_gattc_write_flat(s_conn_handle,
                         s_attr_rx + 1 /* CCCD handle = val_handle+1 */,
                         &cccd, sizeof(cccd), NULL, NULL);
    ESP_LOGI(TAG_BLE_CONN, "subscribed notify on 0x005e");
    /* 订阅后读一次 0x0050（笔记 §B1） */
    if (s_attr_svc)
        ble_gattc_read(s_conn_handle, s_attr_svc, NULL, NULL);
}

static void start_discovery(void)
{
    /* 遍历主服务范围内所有特征，按 16 位 UUID 在回调里匹配 0x005e/0x005f/0x0050。
     * 真机若已确认 0xFE95 服务 handle 范围，可收窄区间。 */
    ble_gattc_disc_all_chrs(s_conn_handle, 1, 0xffff, gatt_disc_cb, NULL);
}

/* ----------  notify 回调 ---------- */
static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG_BLE_CONN, "connected, doing MTU exchange");
            ble_gattc_exchange_mtu(s_conn_handle, NULL, NULL);
            start_discovery();
            if (s_ev) s_ev(true, s_ev_ctx);
        } else {
            ESP_LOGW(TAG_BLE_CONN, "connect failed %d; retrying scan",
                     event->connect.status);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG_BLE_CONN, "disconnected reason=%d",
                 event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_attr_tx = s_attr_rx = s_attr_svc = 0;
        if (s_ev) s_ev(false, s_ev_ctx);
        break;
    case BLE_GAP_EVENT_MTU:
        s_mtu = event->mtu.value;
        ESP_LOGI(TAG_BLE_CONN, "MTU=%u max_write_len=%u",
                 s_mtu, ble_bridge_max_write_len());
        subscribe_rx_notify();
        break;
    case BLE_GAP_EVENT_NOTIFY_RX: {
        struct os_mbuf *om = event->notify_rx.om;
        uint8_t buf[256];
        int len = OS_MBUF_PKTLEN(om);
        if (len > (int)sizeof(buf)) len = sizeof(buf);
        os_mbuf_copydata(om, 0, len, buf);
        if (s_rx) s_rx(buf, len, s_rx_ctx);
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        break;
    default:
        break;
    }
    return 0;
}

/* ---------- 扫描 ---------- */
static int scan_cb(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_DISC) return 0;
    /* 过滤：服务含 0xFE95 */
    const struct ble_gap_disc_desc *d = &event->disc;
    /* 简化：生产应解析 advertising 数据里的 0xFE95 UUID；
     * 开发期连第一个可连设备。真机按日志核对目标地址。 */
    ESP_LOGI(TAG_BLE_CONN, "scan found device");
    struct ble_gap_conn_params cp = {
        .scan_itvl = 0x0010, .scan_window = 0x0010,
        .itvl_min = 0x0006, .itvl_max = 0x000c,
        .latency = 0, .supervision_timeout = 400,
    };
    ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &d->addr, 30000, &cp,
                    ble_gap_event_cb, NULL);
    return 0;
}

/* ---------- NimBLE 宿主 ---------- */
static void ble_on_sync(void)
{
    ble_hs_id_infer_auto(0, NULL);
    struct ble_gap_disc_params dp = {
        .itvl = 0x0010, .window = 0x0010,
        .filter_policy = 0, .limited = 0,
        .passive = 0, .filter_duplicates = 1,
    };
    ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &dp, scan_cb, NULL);
}

static void ble_on_reset(int reason)
{
    ESP_LOGE(TAG_BLE_CONN, "nimble reset reason=%d", reason);
}

static void host_task(void *p)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int ble_bridge_write(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    if (!ble_bridge_connected() || !s_attr_tx) return -1;
    /* Write Without Response */
    return ble_gattc_write_no_rsp_flat(s_conn_handle, s_attr_tx, data, len);
}

void ble_bridge_init(ble_rx_fn rx, void *rx_ctx,
                     ble_event_fn ev, void *ev_ctx,
                     const uint8_t target_addr[6])
{
    s_rx = rx; s_rx_ctx = rx_ctx;
    s_ev = ev; s_ev_ctx = ev_ctx;
    if (target_addr) memcpy(s_target, target_addr, 6);

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_svc_gap_device_name_set(BR_DEVICE_NAME);
    ble_svc_gatt_init();
    ble_svc_gap_init();
    nimble_port_freertos_init(host_task);
}
