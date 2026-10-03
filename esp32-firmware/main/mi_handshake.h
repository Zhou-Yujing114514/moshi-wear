/*
 * mi_handshake.h — 认证握手四步状态机（笔记 §B3.2）
 *
 *   手机(ESP32)                              手环
 *    |--[1] AuthAppVerify{app_random(16B)} -->|
 *    |<--[2] AuthDeviceVerify{w_random(16B), device_sign(32B)} --|
 *    |   校验 device_sign = HMAC(dec_key, w‖p)                 |
 *    |--[3] AuthAppConfirm{app_sign, CCM(CompanionDevice)} -->|
 *    |<--[4] AuthDeviceConfirm{confirm_result} ---------------|
 *
 * 握手期间 Pb 通道为明文 L2(opcode=Write=1)；握手成功后切到 WriteEnc(2)。
 * 本模块只负责组包/解包与状态推进，收发由 sar.c 通过回调注入。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "mi_crypto.h"

typedef enum {
    HS_IDLE = 0,
    HS_WAIT_DEV_VERIFY,   /* 已发 [1]，等 [2] */
    HS_WAIT_DEV_CONFIRM, /* 已发 [3]，等 [4] */
    HS_DONE,             /* 成功 */
    HS_FAILED            /* 失败（签名不符/超时/格式错） */
} mi_hs_state_t;

/* 发送回调：把一帧 Pb 明文 L2 payload（已含 WearPacket 信封）交给下层 */
typedef int (*hs_send_fn)(const uint8_t *data, size_t len, void *ctx);

typedef struct {
    mi_hs_state_t state;
    uint8_t  authkey[16];
    uint8_t  p_random[16];
    uint8_t  w_random[16];
    mi_session_keys_t keys;
    hs_send_fn send;
    void *send_ctx;
    bool keys_ready;
} mi_handshake_t;

/* 初始化：注入 authkey(16B) 与发送回调 */
void mi_hs_init(mi_handshake_t *h, const uint8_t authkey[16],
                hs_send_fn send, void *send_ctx);

/* 发起握手：生成 p_random 并发出 [1]。返回 <0 失败。 */
int mi_hs_start(mi_handshake_t *h);

/*
 * 收到一帧 Pb 通道的明文 L2 payload（即 WearPacket 内层 Account 消息）时调用。
 * 驱动状态机推进。返回：
 *    1 = 状态推进（可能已 done）
 *    0 = 不是握手相关消息（调用方按普通数据处理）
 *   -1 = 握手失败
 */
int mi_hs_on_account(mi_handshake_t *h, const uint8_t *data, size_t len);

/* 握手成功后取数据面密钥（用于 CTR 加解密） */
const mi_session_keys_t *mi_hs_keys(const mi_handshake_t *h);
