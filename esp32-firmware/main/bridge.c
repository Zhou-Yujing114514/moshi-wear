/*
 * bridge.c — 粘合层：把 BLE / SAR / L2 / 加密 / 握手 / QAIC / HTTP 串起来
 *
 * 数据通路：
 *   上行(手环→ESP32)：
 *     BLE notify → sar_on_ble_rx(L1 校验/重组/ACK) → sar_up
 *       → L2 解析 → (未握手:明文→握手状态机 | 已握手:CTR解密→WearPacket→JSON→qaic_on_json)
 *   下行(ESP32→手环)：
 *     qaic_send / 握手回调 → JSON→WearPacket(Account) → [CTR加密] → L2(Pb) → sar_send_l2 → BLE 0x005f
 */
#include "bridge.h"
#include "ble_client.h"
#include "sar.h"
#include "l1_frame.h"
#include "l2_frame.h"
#include "mi_handshake.h"
#include "mi_crypto.h"
#include "proto_pack.h"
#include "proto_const.h"
#include "qaic.h"
#include "config.h"
#include "log_tags.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "BRIDGE";

static sar_t           s_sar;
static mi_handshake_t  s_hs;
static bool            s_link_up = false;   /* L1 startReq 完成 */

/* ---------- 下行：把一段内层(Account) bytes 经 L2+SAR 发出 ---------- */
static int send_l2_payload(const uint8_t *inner, size_t inner_len, bool encrypted)
{
    uint8_t l2buf[BR_MPS];
    l2_channel_t ch = L2_CH_PB;
    l2_opcode_t  op = encrypted ? L2_OP_WRITE_ENC : L2_OP_WRITE;

    /* 若加密：先 CTR 加密内层 */
    uint8_t *payload = (uint8_t*)inner;
    size_t   paylen  = inner_len;
    uint8_t *enc_buf = NULL;
    if (encrypted) {
        const mi_session_keys_t *k = mi_hs_keys(&s_hs);
        if (!k) return -1;
        enc_buf = malloc(inner_len);
        mi_ctr_crypt(k->enc_key, NULL, inner, enc_buf, inner_len, BR_CTR_IV_USE_KEY);
        payload = enc_buf; paylen = inner_len;
    }

    size_t l2len = l2_encode(l2buf, sizeof(l2buf), ch, op, payload, paylen);
    int rc = sar_send_l2(&s_sar, l2buf, l2len);
    free(enc_buf);
    return rc;
}

/* 把 JSON 文本包成 WearPacket{Account{QAIC_PAYLOAD: json}} */
static int wrap_json_to_wearpacket(const char *json, uint8_t *out, size_t out_cap)
{
    pbuf_t w; pbuf_init(&w, out, out_cap);
    /* Account { ACC_FIELD_QAIC_PAYLOAD: json bytes } */
    if (!pbuf_bytes(&w, ACC_FIELD_QAIC_PAYLOAD, json, strlen(json))) return -1;
    /* WearPacket { WP_FIELD_INNER: Account } */
    uint8_t wp[BR_MPS];
    pbuf_t ww; pbuf_init(&ww, wp, sizeof(wp));
    if (!pbuf_bytes(&ww, WP_FIELD_INNER, out, pbuf_len(&w))) return -1;
    memcpy(out, wp, pbuf_len(&ww));
    return (int)pbuf_len(&ww);
}

/* QAIC 下行发送回调 */
static int qaic_send_cb(const char *json, void *ctx)
{
    (void)ctx;
    uint8_t wp[BR_MPS];
    int n = wrap_json_to_wearpacket(json, wp, sizeof(wp));
    if (n < 0) return -1;
    return send_l2_payload(wp, n, /*encrypted=*/ mi_hs_keys(&s_hs) != NULL);
}

/* 握手发送回调（明文） */
static int hs_send_cb(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    return send_l2_payload(data, len, false);
}

/* ---------- 上行：SAR 上交一条完整 L2 ---------- */
static void sar_up_cb(const uint8_t *l2, size_t len, void *ctx)
{
    (void)ctx;
    l2_channel_t ch; l2_opcode_t op;
    const uint8_t *payload; size_t paylen;
    if (l2_decode(l2, len, &ch, &op, &payload, &paylen) == 0) return;
    if (ch != L2_CH_PB) return;   /* 本固件只桥 Pb/QAIC 通道 */

    if (op == L2_OP_WRITE) {
        /* 明文：握手期。交给握手状态机（payload 即 WearPacket/Account）。 */
        int r = mi_hs_on_account(&s_hs, payload, paylen);
        if (r < 0) ESP_LOGE(TAG, "handshake failed");
        else if (mi_hs_keys(&s_hs)) {
            ESP_LOGI(TAG, "data plane keys established; channel encrypted");
        }
        return;
    }

    if (op == L2_OP_WRITE_ENC) {
        const mi_session_keys_t *k = mi_hs_keys(&s_hs);
        if (!k) { ESP_LOGW(TAG, "got encrypted before keys; drop"); return; }
        /* CTR 解密（手环→手机用 dec_key） */
        uint8_t *plain = malloc(paylen ? paylen : 1);
        mi_ctr_crypt(k->dec_key, NULL, payload, plain, paylen, BR_CTR_IV_USE_KEY);

        /* 解 WearPacket 信封取内层 Account，再取 QAIC JSON */
        const uint8_t *account = NULL; size_t alen = 0;
        if (proto_find_bytes(plain, paylen, WP_FIELD_INNER, &account, &alen)) {
            const uint8_t *json = NULL; size_t jlen = 0;
            if (proto_find_bytes(account, alen, ACC_FIELD_QAIC_PAYLOAD, &json, &jlen)) {
                char *js = malloc(jlen + 1);
                memcpy(js, json, jlen); js[jlen] = 0;
                ESP_LOGD(TAG_QAIC_HS, "QAIC <- %s", js);
                qaic_on_json(js);
                free(js);
            }
        } else {
            /* 宽松：payload 本身就是 JSON */
            char *js = malloc(paylen + 1);
            memcpy(js, plain, paylen); js[paylen] = 0;
            qaic_on_json(js);
            free(js);
        }
        free(plain);
        return;
    }

    /* opcode==3 Read 等其它：忽略 */
}

/* BLE notify 上行 */
static void ble_rx_cb(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    sar_on_ble_rx(&s_sar, data, len);
}

/* BLE 连接事件 */
static void ble_ev_cb(bool connected, void *ctx)
{
    (void)ctx;
    if (connected) {
        ESP_LOGI(TAG, "BLE connected; starting L1 link negotiation");
        /* 等 MTU 协商后再 start_link（简化：直接发；真机可在 MTU 事件后调） */
        sar_start_link(&s_sar);
        s_link_up = true;
        /* 开始认证握手 */
        uint8_t key[16];
        if (!mi_hex16_to_key(BR_AUTHKEY_HEX, key)) {
            ESP_LOGE(TAG, "authkey 未配置/格式错（BR_AUTHKEY_HEX 应为 32 hex）");
            return;
        }
        mi_hs_init(&s_hs, key, hs_send_cb, NULL);
        mi_hs_start(&s_hs);
    } else {
        ESP_LOGW(TAG, "BLE disconnected; will reconnect");
        s_link_up = false;
    }
}

/* ---------- 周期定时器：sar_tick + qaic_tick ---------- */
static void timer_cb(void *arg)
{
    (void)arg;
    uint32_t now = (uint32_t)(esp_timer_get_time()/1000);
    sar_tick(&s_sar, now);
    qaic_tick(now);
}

/* ---------- WiFi ---------- */
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG_WIFI, "WiFi got IP, bridge ready");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG_WIFI, "WiFi disconnected, retrying");
        esp_wifi_connect();
    }
}

static void wifi_start(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);

    wifi_config_t wc = {0};
    strncpy((char*)wc.sta.ssid, BR_WIFI_SSID, sizeof(wc.sta.ssid)-1);
    strncpy((char*)wc.sta.password, BR_WIFI_PASS, sizeof(wc.sta.password)-1);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    esp_wifi_start();
}

/* ---------- bridge 启动（main.c 调用） ---------- */
void bridge_start(void)
{
    /* NVS（WiFi/BLE 需要） */
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    qaic_init(qaic_send_cb, NULL);
    sar_init(&s_sar,
             (sar_ble_write_fn)ble_bridge_write, NULL,
             sar_up_cb, NULL,
             BR_DEFAULT_MAX_WRITE);

    /* 1s 周期定时器 */
    const esp_timer_create_args_t tcfg = { .callback = timer_cb, .name = "br-tick" };
    esp_timer_handle_t t;
    esp_timer_create(&tcfg, &t);
    esp_timer_start_periodic(t, 1000000);

    wifi_start();

    /* BLE 中心：扫描并连接手环 */
    uint8_t addr[6] = BR_TARGET_ADDR;
    ble_bridge_init(ble_rx_cb, NULL, ble_ev_cb, NULL, addr);

    ESP_LOGI(TAG, "bridge started");
}
