/*
 * mi_crypto.c — 加密实现（mbedTLS，ESP-IDF 自带）
 */
#include "mi_crypto.h"
#include "config.h"
#include "log_tags.h"
#include "esp_log.h"
#include <string.h>

#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/aes.h"
#include "mbedtls/ccm.h"
#include "mbedtls/platform.h"

static const char INFO_TAG[] = "miwear-auth";  /* §B3.3 info tag，ASCII */

void mi_kdf(const uint8_t authkey[16],
            const uint8_t p_random[16],
            const uint8_t w_random[16],
            mi_session_keys_t *out)
{
    /* init_key = p ‖ w (32B) */
    uint8_t init_key[32];
    memcpy(init_key, p_random, 16);
    memcpy(init_key + 16, w_random, 16);

    /* PRK = HMAC-SHA256(init_key, authkey) */
    uint8_t prk[32];
    size_t olen = 0;
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                    init_key, sizeof(init_key),
                    authkey, 16, prk, &olen);

    /* HKDF-Expand：T1=HMAC(PRK, ""‖info‖01)，T2=HMAC(PRK,T1‖info‖02) */
    uint8_t block64[64];
    uint8_t prev[32];
    memset(prev, 0, sizeof(prev));
    size_t off = 0;
    for (uint8_t counter = 1; counter <= 2; counter++) {
        uint8_t buf[32 + sizeof(INFO_TAG) + 1];
        size_t blen = 0;
        memcpy(buf + blen, prev, 32); blen += 32;
        memcpy(buf + blen, INFO_TAG, sizeof(INFO_TAG) - 1); blen += sizeof(INFO_TAG) - 1;
        buf[blen++] = counter;
        uint8_t ti[32]; size_t ti_len = 0;
        mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                        prk, 32, buf, blen, ti, &ti_len);
        memcpy(block64 + off, ti, 32);
        off += 32;
        memcpy(prev, ti, 32);
    }

    memcpy(out->dec_key,   block64 + 0,  16);
    memcpy(out->enc_key,   block64 + 16, 16);
    memcpy(out->dec_nonce, block64 + 32, 4);
    memcpy(out->enc_nonce, block64 + 36, 4);

    ESP_LOGD(TAG_CRYPTO, "KDF done");
    /* 敏感材料清零 */
    memset(init_key, 0, sizeof(init_key));
    memset(prk, 0, sizeof(prk));
    memset(block64, 0, sizeof(block64));
    memset(prev, 0, sizeof(prev));
}

void mi_device_sign_expect(const mi_session_keys_t *k,
                           const uint8_t w_random[16],
                           const uint8_t p_random[16],
                           uint8_t out[32])
{
    /* HMAC(dec_key, w‖p) */
    uint8_t msg[32];
    memcpy(msg, w_random, 16);
    memcpy(msg + 16, p_random, 16);
    size_t olen = 0;
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                    k->dec_key, 16, msg, sizeof(msg), out, &olen);
    memset(msg, 0, sizeof(msg));
}

void mi_app_sign(const mi_session_keys_t *k,
                 const uint8_t p_random[16],
                 const uint8_t w_random[16],
                 uint8_t out[32])
{
    /* HMAC(enc_key, p‖w) */
    uint8_t msg[32];
    memcpy(msg, p_random, 16);
    memcpy(msg + 16, w_random, 16);
    size_t olen = 0;
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                    k->enc_key, 16, msg, sizeof(msg), out, &olen);
    memset(msg, 0, sizeof(msg));
}

int mi_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                   const uint8_t *aad, size_t aad_len,
                   const uint8_t *plain, size_t plain_len,
                   uint8_t *out, size_t out_cap)
{
    if (out_cap < plain_len + MI_TAG_LEN) return -1;
    mbedtls_ccm_context ctx;
    mbedtls_ccm_init(&ctx);
    int rc = mbedtls_ccm_setkey(&ctx, MBEDTLS_CIPHER_ID_AES, key, 128);
    if (rc != 0) { mbedtls_ccm_free(&ctx); return -1; }
    /* out = 密文；tag 紧跟其后 4B */
    rc = mbedtls_ccm_encrypt_and_tag(&ctx, plain_len,
                                    nonce, MI_NONCE_LEN,
                                    aad, aad_len,
                                    plain, out,
                                    out + plain_len, MI_TAG_LEN);
    mbedtls_ccm_free(&ctx);
    if (rc != 0) return -1;
    return (int)plain_len + MI_TAG_LEN;
}

void mi_ctr_crypt(const uint8_t key[16], const uint8_t iv[16],
                  const uint8_t *in, uint8_t *out, size_t len,
                  bool use_key_as_iv)
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, 128);

    uint8_t iv_buf[16];
    if (use_key_as_iv)
        memcpy(iv_buf, key, 16);   /* §B3.6 怪癖：IV = key */
    else
        memcpy(iv_buf, iv, 16);

    size_t nc_off = 0;
    uint8_t stream[16];
    mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, iv_buf, stream, in, out);
    mbedtls_aes_free(&ctx);
}

bool mi_hex16_to_key(const char *hex32, uint8_t out[16])
{
    if (!hex32) return false;
    size_t n = strlen(hex32);
    if (n != 32) return false;
    for (int i = 0; i < 16; i++) {
        int hi, lo;
        char a = hex32[2*i], b = hex32[2*i+1];
        if (a >= '0' && a <= '9') hi = a - '0';
        else if (a >= 'a' && a <= 'f') hi = a - 'a' + 10;
        else if (a >= 'A' && a <= 'F') hi = a - 'A' + 10;
        else return false;
        if (b >= '0' && b <= '9') lo = b - '0';
        else if (b >= 'a' && b <= 'f') lo = b - 'a' + 10;
        else if (b >= 'A' && b <= 'F') lo = b - 'A' + 10;
        else return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}
