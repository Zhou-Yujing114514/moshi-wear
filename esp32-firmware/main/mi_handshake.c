/*
 * mi_handshake.c — 四步握手实现（笔记 §B3.2~B3.5）
 *
 * 字段编号取自 proto_const.h（待核实），不编造。
 */
#include "mi_handshake.h"
#include "proto_pack.h"
#include "proto_const.h"
#include "config.h"
#include "log_tags.h"
#include "esp_log.h"
#include "esp_random.h"
#include <string.h>

/* ---------- 内层消息构建 ---------- */

/* AuthAppVerify{ app_random }，再包进 Account.AppVerify */
static int build_app_verify(const uint8_t p_random[16],
                           uint8_t *out, size_t out_cap)
{
    pbuf_t w; pbuf_init(&w, out, out_cap);

    /* inner = AuthAppVerify { AV_FIELD_APP_RANDOM: p_random } */
    uint8_t inner[64];
    pbuf_t iw; pbuf_init(&iw, inner, sizeof(inner));
    if (!pbuf_bytes(&iw, AV_FIELD_APP_RANDOM, p_random, 16)) return -1;

    /* Account { ACC_FIELD_APP_VERIFY: inner } */
    if (!pbuf_bytes(&w, ACC_FIELD_APP_VERIFY, inner, pbuf_len(&iw))) return -1;

    /* WearPacket { WP_FIELD_INNER: Account } */
    uint8_t wp[160];
    pbuf_t ww; pbuf_init(&ww, wp, sizeof(wp));
    if (!pbuf_bytes(&ww, WP_FIELD_INNER, out, pbuf_len(&w))) return -1;
    memcpy(out, wp, pbuf_len(&ww));
    return (int)pbuf_len(&ww);
}

/* AuthAppConfirm{ app_sign, companion_cipher }，包进 Account.AppConfirm */
static int build_app_confirm(const uint8_t app_sign[32],
                             const uint8_t *companion_ct, size_t companion_ct_len,
                             uint8_t *out, size_t out_cap)
{
    pbuf_t w; pbuf_init(&w, out, out_cap);

    /* inner = AuthAppConfirm { app_sign; companion_ct } */
    uint8_t inner[160];
    pbuf_t iw; pbuf_init(&iw, inner, sizeof(inner));
    if (!pbuf_bytes(&iw, AC_FIELD_APP_SIGN, app_sign, 32)) return -1;
    if (!pbuf_bytes(&iw, AC_FIELD_COMPANION, companion_ct, companion_ct_len)) return -1;

    if (!pbuf_bytes(&w, ACC_FIELD_APP_CONFIRM, inner, pbuf_len(&iw))) return -1;

    uint8_t wp[256];
    pbuf_t ww; pbuf_init(&ww, wp, sizeof(wp));
    if (!pbuf_bytes(&ww, WP_FIELD_INNER, out, pbuf_len(&w))) return -1;
    memcpy(out, wp, pbuf_len(&ww));
    return (int)pbuf_len(&ww);
}

/* CompanionDevice 明文 protobuf（被 CCM 加密） */
static int build_companion(uint8_t *out, size_t out_cap)
{
    pbuf_t w; pbuf_init(&w, out, out_cap);
    if (!pbuf_varint(&w, CD_FIELD_DEVICE_TYPE, BR_DEVICE_TYPE)) return -1;
    if (!pbuf_string(&w, CD_FIELD_DEVICE_NAME, BR_DEVICE_NAME)) return -1;
    if (!pbuf_varint(&w, CD_FIELD_APP_CAPABILITY, 0xFFFFFFFF)) return -1;
    return (int)pbuf_len(&w);
}

/* ---------- 生命周期 ---------- */

void mi_hs_init(mi_handshake_t *h, const uint8_t authkey[16],
                hs_send_fn send, void *send_ctx)
{
    memset(h, 0, sizeof(*h));
    memcpy(h->authkey, authkey, 16);
    h->state = HS_IDLE;
    h->send = send;
    h->send_ctx = send_ctx;
}

int mi_hs_start(mi_handshake_t *h)
{
    /* 生成手机随机数 p_random */
    esp_fill_random(h->p_random, 16);

    uint8_t buf[256];
    int n = build_app_verify(h->p_random, buf, sizeof(buf));
    if (n < 0) { h->state = HS_FAILED; return -1; }

    ESP_LOGI(TAG_HS_STEP, "[1] AuthAppVerify sent (p_random generated)");
    h->state = HS_WAIT_DEV_VERIFY;
    return h->send(buf, (size_t)n, h->send_ctx);
}

int mi_hs_on_account(mi_handshake_t *h, const uint8_t *data, size_t len)
{
    /* 先解 WearPacket 信封取内层 Account */
    const uint8_t *account = NULL; size_t account_len = 0;
    if (!proto_find_bytes(data, len, WP_FIELD_INNER, &account, &account_len)) {
        /* 可能本身就是 Account（无外层信封）——宽松处理 */
        account = data; account_len = len;
    }

    if (h->state == HS_WAIT_DEV_VERIFY) {
        /* 解 AuthDeviceVerify */
        const uint8_t *dv = NULL; size_t dv_len = 0;
        if (!proto_find_bytes(account, account_len, ACC_FIELD_DEV_VERIFY, &dv, &dv_len)) {
            ESP_LOGW(TAG_HS_STEP, "[2] no AuthDeviceVerify (field 待核实?)");
            return 0;
        }
        const uint8_t *w_random = NULL; size_t wl = 0;
        const uint8_t *device_sign = NULL; size_t sl = 0;
        if (!proto_find_bytes(dv, dv_len, DV_FIELD_DEV_RANDOM, &w_random, &wl) || wl != 16) {
            ESP_LOGE(TAG_HS_STEP, "[2] bad device_random");
            h->state = HS_FAILED; return -1;
        }
        if (!proto_find_bytes(dv, dv_len, DV_FIELD_DEV_SIGN, &device_sign, &sl) || sl != 32) {
            ESP_LOGE(TAG_HS_STEP, "[2] bad device_sign");
            h->state = HS_FAILED; return -1;
        }
        memcpy(h->w_random, w_random, 16);

        /* KDF 派生会话密钥 */
        mi_kdf(h->authkey, h->p_random, h->w_random, &h->keys);
        h->keys_ready = true;

        /* 校验 device_sign = HMAC(dec_key, w‖p) */
        uint8_t expect[32];
        mi_device_sign_expect(&h->keys, h->w_random, h->p_random, expect);
        if (memcmp(expect, device_sign, 32) != 0) {
            ESP_LOGE(TAG_HS_STEP,
                "[2] device_sign mismatch -> AuthKey 错误（典型表现）或字段编号待核实");
            h->state = HS_FAILED;
            memset(expect, 0, sizeof(expect));
            return -1;
        }
        ESP_LOGI(TAG_HS_STEP, "[2] AuthDeviceVerify verified (device_sign OK)");
        memset(expect, 0, sizeof(expect));

        /* 计算 app_sign = HMAC(enc_key, p‖w) */
        uint8_t app_sign[32];
        mi_app_sign(&h->keys, h->p_random, h->w_random, app_sign);

        /* CCM 加密 CompanionDevice */
        uint8_t cd_plain[64];
        int cd_len = build_companion(cd_plain, sizeof(cd_plain));
        if (cd_len < 0) { h->state = HS_FAILED; return -1; }

        /* nonce = enc_nonce(4) || counterHi(4 LE=0) || counterLo(4 LE=0) */
        uint8_t nonce[12];
        memcpy(nonce, h->keys.enc_nonce, 4);
        memset(nonce + 4, 0, 8);

        uint8_t ct[64 + 4];
        int ct_len = mi_ccm_encrypt(h->keys.enc_key, nonce,
                                    NULL, 0, cd_plain, cd_len, ct, sizeof(ct));
        memset(cd_plain, 0, sizeof(cd_plain));
        if (ct_len < 0) { h->state = HS_FAILED; return -1; }

        uint8_t buf[320];
        int n = build_app_confirm(app_sign, ct, ct_len, buf, sizeof(buf));
        memset(app_sign, 0, sizeof(app_sign));
        memset(ct, 0, sizeof(ct));
        if (n < 0) { h->state = HS_FAILED; return -1; }

        ESP_LOGI(TAG_HS_STEP, "[3] AuthAppConfirm sent (app_sign + CCM companion)");
        h->state = HS_WAIT_DEV_CONFIRM;
        h->send(buf, (size_t)n, h->send_ctx);
        return 1;
    }

    if (h->state == HS_WAIT_DEV_CONFIRM) {
        const uint8_t *dc = NULL; size_t dc_len = 0;
        if (!proto_find_bytes(account, account_len, ACC_FIELD_DEV_CONFIRM, &dc, &dc_len)) {
            ESP_LOGW(TAG_HS_STEP, "[4] no AuthDeviceConfirm (field 待核实?)");
            return 0;
        }
        uint64_t result = 0;
        if (!proto_find_varint(dc, dc_len, DC_FIELD_RESULT, &result)) {
            ESP_LOGW(TAG_HS_STEP, "[4] no confirm_result bool (field 待核实?)");
            return 0;
        }
        if (result) {
            ESP_LOGI(TAG_HS_STEP, "[4] AuthDeviceConfirm OK -> 握手完成，切加密通道");
            h->state = HS_DONE;
        } else {
            ESP_LOGE(TAG_HS_STEP, "[4] confirm_result=false -> 认证被拒");
            h->state = HS_FAILED;
        }
        return 1;
    }

    return 0;
}

const mi_session_keys_t *mi_hs_keys(const mi_handshake_t *h)
{
    return h->keys_ready ? &h->keys : NULL;
}
