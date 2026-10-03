/*
 * l2_frame.h — L2 通道层帧（笔记 §B2.3）
 *
 * [0] channel
 * [1] opcode
 * [2..] payload（明文或密文）
 *
 * channel: 1=Pb(protobuf/QAIC), 2=Mass, ... （见笔记 §B2.3 表）
 * opcode : 1=Write(明文), 2=WriteEnc(加密), 3=Read
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#define L2_HEADER_LEN   2

/* 通道号（笔记 §B2.3） */
typedef enum {
    L2_CH_PB          = 1,   /* Pb：protobuf / interconnect QAIC JSON */
    L2_CH_MASS        = 2,
    L2_CH_MASS_VOICE  = 3,
    L2_CH_FILE_SENSOR = 4,
    L2_CH_FILE_FIT    = 5,
    L2_CH_OTA         = 6,
    L2_CH_NETWORK     = 7,
    L2_CH_LYRA        = 8,
    L2_CH_RESEARCH    = 9,
    L2_CH_MULTIMODAL  = 10
} l2_channel_t;

/* 操作码（笔记 §B2.3） */
typedef enum {
    L2_OP_WRITE       = 1,   /* 明文 */
    L2_OP_WRITE_ENC   = 2,   /* 加密 */
    L2_OP_READ        = 3
} l2_opcode_t;

/* 打包 L2 帧到 out；返回总长度，失败 0 */
size_t l2_encode(uint8_t *out, size_t out_cap,
                 l2_channel_t ch, l2_opcode_t op,
                 const uint8_t *payload, size_t payload_len);

/*
 * 解析 L2。成功返回 L2_HEADER_LEN+payload_len，payload 指向 buf 内。
 */
size_t l2_decode(const uint8_t *buf, size_t len,
                 l2_channel_t *out_ch, l2_opcode_t *out_op,
                 const uint8_t **out_payload, size_t *out_payload_len);
