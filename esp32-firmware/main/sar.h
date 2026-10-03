/*
 * sar.h — 分片与流控（笔记 §B4 / B2.2）
 *
 *  - seq：u8 回绕计数器，每包分配一个
 *  - 发送窗口 LOCAL_TX_WIN = 32
 *  - 最大重试 MAX_PACKET_RETRIES = 3
 *  - 超时 SEND_TIMEOUT = 10s
 *  - ACK/Nak：L1 type=1(Ack)/0(Nak)，seq=被确认序号，payload 空
 *  - MPS = 64512（重组缓冲上限；逻辑最大 payload，非 BLE MTU）
 *  - 落到 BLE：005f 用无响应写，每包 ≤ max_write_len(=MTU-3)
 *
 * 同时负责 L1 startReq/startRsp（链路建立命令）的 TLV 协商。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SAR_TX_WIN          32
#define SAR_MAX_RETRIES     3
#define SAR_SEND_TIMEOUT_MS 10000
#define SAR_MPS             64512

/* 下层写 BLE 的回调：把已封装好的 L1 原始字节（≤ max_write_len）写到 0x005f */
typedef int (*sar_ble_write_fn)(const uint8_t *data, size_t len, void *ctx);
/* 上层交付回调：把重组完成的一条 L2 payload 上交 */
typedef void (*sar_up_fn)(const uint8_t *l2, size_t len, void *ctx);

typedef struct {
    /* 配置（运行时） */
    sar_ble_write_fn ble_write;
    void *ble_ctx;
    sar_up_fn       up;
    void *up_ctx;
    uint16_t max_write_len;   /* MTU-3 */

    /* TX 状态 */
    uint8_t tx_seq;
    /* 待确认帧表（按 seq(u8) 直接索引，256 槽覆盖回绕；每槽一帧 ≤ max_write_len） */
    uint8_t  pending_buf[256][300];
    uint16_t pending_len[256];
    uint8_t  pending_retries[256];
    uint32_t pending_sent_ms[256];
    bool     pending_active[256];

    /* RX 重组缓冲 */
    uint8_t rx_buf[SAR_MPS];
    size_t  rx_len;
} sar_t;

void sar_init(sar_t *s, sar_ble_write_fn bw, void *bw_ctx,
              sar_up_fn up, void *up_ctx, uint16_t max_write_len);

/* 发送一条完整 L2 payload：内部包 L1(Data)、切片、按窗口写 BLE。 */
int sar_send_l2(sar_t *s, const uint8_t *l2, size_t len);

/* 收到 BLE notify 原始字节（可能是部分 L1 帧）时调用；尝试解析并推进。 */
void sar_on_ble_rx(sar_t *s, const uint8_t *data, size_t len);

/* 链路建立：发 L1 startReq（version1.0.0 / MPS64512 / TX_WIN32 / TIMEOUT10s） */
int sar_start_link(sar_t *s);

/* 周期调度（建议每 1s 调一次）：超时未 ACK 的帧重试（最多 SAR_MAX_RETRIES） */
void sar_tick(sar_t *s, uint32_t now_ms);
