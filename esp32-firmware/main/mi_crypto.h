/*
 * mi_crypto.h — 小米手环加密原语（笔记 §B3）
 *
 *  - AES-128-CCM：认证期，key16 / nonce12 / tag4
 *  - AES-128-CTR ：数据面，key=enc_key 或 dec_key；IV 怪癖：默认 IV=key（可配置）
 *  - HKDF-SHA256 ：info="miwear-auth"，PRK=HMAC(phone||watch, authkey)
 *  - 双层 HMAC-SHA256：device_sign=HMAC(dec_key, w‖p)，app_sign=HMAC(enc_key, p‖w)
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MI_KEY_LEN     16
#define MI_NONCE_LEN   12
#define MI_TAG_LEN     4
#define MI_RANDOM_LEN  16
#define MI_SIGN_LEN    32

/* 会话派生密钥组（§B3.3） */
typedef struct {
    uint8_t dec_key[16];    /* 手环→手机 数据密钥 */
    uint8_t enc_key[16];    /* 手机→手环 数据密钥 */
    uint8_t dec_nonce[4];
    uint8_t enc_nonce[4];
} mi_session_keys_t;

/*
 * KDF：由 authkey(16B) + p_random(16B) + w_random(16B) 派生会话密钥。
 * 严格按 §B3.3：
 *   init_key = p ‖ w
 *   PRK      = HMAC-SHA256(init_key, authkey)
 *   T1 = HMAC(PRK, ""  ‖ "miwear-auth" ‖ 0x01)
 *   T2 = HMAC(PRK, T1 ‖ "miwear-auth" ‖ 0x02)
 *   T3 = HMAC(PRK, T2 ‖ "miwear-auth" ‖ 0x03)
 *   block64 = T1‖T2
 *   dec_key=block[0:16], enc_key=block[16:32], dec_nonce=block[32:36], enc_nonce=block[36:40]
 */
void mi_kdf(const uint8_t authkey[16],
            const uint8_t p_random[16],
            const uint8_t w_random[16],
            mi_session_keys_t *out);

/* 双层 HMAC（§B3.4） */
/* expect = HMAC(dec_key, w_random‖p_random)，与手环 device_sign 比较 */
void mi_device_sign_expect(const mi_session_keys_t *k,
                           const uint8_t w_random[16],
                           const uint8_t p_random[16],
                           uint8_t out[32]);
/* app_sign = HMAC(enc_key, p_random‖w_random)，回给手环 */
void mi_app_sign(const mi_session_keys_t *k,
                 const uint8_t p_random[16],
                 const uint8_t w_random[16],
                 uint8_t out[32]);

/*
 * AES-128-CCM 加密（认证期 CompanionDevice）。
 * nonce=12B；输出密文+ciphertext 后跟 4B tag。
 * 返回密文长度；失败返回 -1。
 */
int mi_ccm_encrypt(const uint8_t key[16], const uint8_t nonce[12],
                   const uint8_t *aad, size_t aad_len,
                   const uint8_t *plain, size_t plain_len,
                   uint8_t *out, size_t out_cap /* >= plain_len+4 */);

/* AES-128-CTR 数据面加解密（CTR 加解密同函数）。
 * use_key_as_iv=1 时 IV 直接取 key（§B3.6 怪癖）；否则用显式 iv16。 */
void mi_ctr_crypt(const uint8_t key[16], const uint8_t iv[16],
                  const uint8_t *in, uint8_t *out, size_t len,
                  bool use_key_as_iv);

/* 从 32 字符 hex 串解析 16 字节密钥；失败返回 false */
bool mi_hex16_to_key(const char *hex32, uint8_t out[16]);
