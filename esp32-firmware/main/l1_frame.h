/*
 * l1_frame.h — L1 链路层帧编解码（笔记 §B2.1）
 *
 * 帧格式（小端）：
 *   [0..1]  magic   = 0xA5A5（字节流 A5 A5）
 *   [2]     type|frx  低4位=type；bit4(0x10)=frx
 *   [3]     seq     u8
 *   [4..5]  length  u16 LE = payload 字节数
 *   [6..7]  crc     u16 LE = CRC16-ARC(payload)
 *   [8..]   payload (L2 帧)
 *
 * L1 头固定 8 字节；最小合法包 = 8 字节。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define L1_HEADER_LEN        8
#define L1_MAGIC            0xA5A5

/* L1 数据类型（笔记 §B2.1 L1DataType） */
typedef enum {
    L1_TYPE_NAK  = 0,   /* Nak  */
    L1_TYPE_ACK  = 1,   /* Ack  */
    L1_TYPE_CMD  = 2,   /* Cmd（链路建立命令） */
    L1_TYPE_DATA = 3    /* Data */
} l1_type_t;

/* frx 标志位（与 type 同字节的 bit4） */
#define L1_FRX_FLAG         0x10

/* 计算 CRC16-ARC：poly 0xA001（反射 0x8005），init 0x0000 */
uint16_t crc16_arc(const uint8_t *data, size_t len);

/*
 * 打包一帧到 out（至少 L1_HEADER_LEN+payload_len 字节）。
 * 返回总帧长；失败返回 0。
 */
size_t l1_encode(uint8_t *out, size_t out_cap,
                 l1_type_t type, bool frx, uint8_t seq,
                 const uint8_t *payload, size_t payload_len);

/*
 * 解析一帧。成功返回帧总长度（含头），并填充各字段；失败返回 0。
 * payload 指针直接指向 buf 内（不拷贝）。
 */
size_t l1_decode(const uint8_t *buf, size_t len,
                 l1_type_t *out_type, bool *out_frx, uint8_t *out_seq,
                 const uint8_t **out_payload, size_t *out_payload_len);
