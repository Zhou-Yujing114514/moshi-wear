/*
 * sar.c — 分片与流控实现（笔记 §B4 / B2.2）
 *
 * 说明：本实现为清晰可读的 go-back-N 简化版，常量严格按笔记：
 *   TX_WIN=32、RETRIES=3、TIMEOUT=10s、MPS=64512。
 * 真机验证时若发现手环 ACK 行为不同，优先按 TAG_FLOW_ACK 日志调整。
 */
#include "sar.h"
#include "l1_frame.h"
#include "l2_frame.h"
#include "log_tags.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* 待确认槽位数（≥ TX_WIN 32），seq 对其取模索引 */
#define SLOTN   48
#define SLOT(seq) ((seq) % SLOTN)

void sar_init(sar_t *s, sar_ble_write_fn bw, void *bw_ctx,
              sar_up_fn up, void *up_ctx, uint16_t max_write_len)
{
    memset(s, 0, sizeof(*s));
    s->ble_write = bw;
    s->ble_ctx = bw_ctx;
    s->up = up;
    s->up_ctx = up_ctx;
    s->max_write_len = max_write_len ? max_write_len : 100;
    s->tx_seq = 0;
}

/* 把一帧 L1 Data 实际写到 BLE 并登记待确认 */
static int tx_frame(sar_t *s, const uint8_t *l2_payload, size_t l2_len)
{
    /* 单条 L2 可能超过 max_write_len，切成多片（每片一帧） */
    size_t offset = 0;
    int rc = 0;
    while (offset < l2_len) {
        size_t chunk = l2_len - offset;
        size_t max_payload = (s->max_write_len > L1_HEADER_LEN)
                              ? s->max_write_len - L1_HEADER_LEN : 1;
        if (chunk > max_payload) chunk = max_payload;

        uint8_t frame[300];
        size_t flen = l1_encode(frame, sizeof(frame),
                                L1_TYPE_DATA, false, s->tx_seq,
                                l2_payload + offset, chunk);
        if (flen == 0) return -1;

        /* 登记待确认 */
        uint8_t seq = s->tx_seq;
        uint8_t slot = SLOT(seq);
        memcpy(s->pending_buf[slot], frame, flen);
        s->pending_len[slot] = (uint16_t)flen;
        s->pending_retries[slot] = 0;
        s->pending_sent_ms[slot] = now_ms();
        s->pending_active[slot] = true;

        ESP_LOGD(TAG_FLOW_ACK, "TX data seq=%u chunk=%u", seq, (unsigned)chunk);
        if (s->ble_write(frame, flen, s->ble_ctx) != 0) rc = -1;

        s->tx_seq++;
        offset += chunk;
    }
    return rc;
}

int sar_send_l2(sar_t *s, const uint8_t *l2, size_t len)
{
    if (len > SAR_MPS) {
        ESP_LOGW(TAG_FLOW_ACK, "L2 payload %u > MPS %d, truncate",
                 (unsigned)len, SAR_MPS);
        len = SAR_MPS;
    }
    return tx_frame(s, l2, len);
}

/* 回 Ack/Nak */
static void send_ack(sar_t *s, uint8_t seq, bool nak)
{
    uint8_t frame[L1_HEADER_LEN];
    size_t flen = l1_encode(frame, sizeof(frame),
                           nak ? L1_TYPE_NAK : L1_TYPE_ACK,
                           false, seq, NULL, 0);
    if (flen && s->ble_write)
        s->ble_write(frame, flen, s->ble_ctx);
    ESP_LOGD(TAG_FLOW_ACK, "TX %s seq=%u", nak ? "Nak" : "Ack", seq);
}

/* 处理收到的 Ack/Nak */
static void on_ack_frame(sar_t *s, uint8_t seq, bool nak)
{
    uint8_t slot = SLOT(seq);
    if (nak) {
        /* 重传该帧一次（受最大重试限制） */
        if (s->pending_active[slot] && s->pending_retries[slot] < SAR_MAX_RETRIES) {
            s->pending_retries[slot]++;
            s->pending_sent_ms[slot] = now_ms();
            if (s->ble_write)
                s->ble_write(s->pending_buf[slot], s->pending_len[slot], s->ble_ctx);
            ESP_LOGI(TAG_FLOW_ACK, "Nak seq=%u -> retransmit #%d",
                     seq, s->pending_retries[slot]);
        }
        return;
    }
    /* Ack：该 seq 已确认，释放 */
    if (s->pending_active[slot]) {
        s->pending_active[slot] = false;
        ESP_LOGD(TAG_FLOW_ACK, "Ack seq=%u released", seq);
    }
}

/* 处理收到的 Data 帧：重组并回 Ack */
static void on_data_frame(sar_t *s, const uint8_t *payload, size_t plen, uint8_t seq)
{
    if (s->rx_len + plen > SAR_MPS) {
        ESP_LOGW(TAG_FLOW_ACK, "RX reassembly overflow, reset");
        s->rx_len = 0;
        send_ack(s, seq, true /* Nak */);
        return;
    }
    memcpy(s->rx_buf + s->rx_len, payload, plen);
    s->rx_len += plen;
    /* 简化：每收到一片即回 Ack（增量 ACK，符合笔记 §5.3 硬性要求的精神）。
     * 真机组包边界由 L2 长度判断：这里把整片直接上交，由上层按 L2 解析。 */
    send_ack(s, seq, false);

    /* 尝试上交：L2 头固定 2 字节。若本帧是完整 L2（理想情况下一片一消息），上交。
     * 注意：真实链路 L2 消息可能跨多 BLE 片；此处简化为"片到达即重组缓冲累积"，
     * 由上层 qaic 在收到完整 JSON 后处理。真机需按 L2/L1 边界完善重组。 */
    if (s->rx_len >= L2_HEADER_LEN) {
        /* 启发式：Pb 通道且 payload 像完整消息时上交一次并复位。
         * 为不丢数据，这里每帧即上交当前累积缓冲（真机需改为按 L2 长度字段组包）。 */
        if (s->up) s->up(s->rx_buf, s->rx_len, s->up_ctx);
        s->rx_len = 0;
    }
}

void sar_on_ble_rx(sar_t *s, const uint8_t *data, size_t len)
{
    l1_type_t type; bool frx; uint8_t seq;
    const uint8_t *payload; size_t plen;
    size_t consumed = l1_decode(data, len, &type, &frx, &seq, &payload, &plen);
    if (consumed == 0) return; /* CRC/magic 错 → 不回包或回 Nak（见 l1 日志） */

    switch (type) {
    case L1_TYPE_ACK: on_ack_frame(s, seq, false); break;
    case L1_TYPE_NAK: on_ack_frame(s, seq, true);  break;
    case L1_TYPE_DATA: on_data_frame(s, payload, plen, seq); break;
    case L1_TYPE_CMD:
        ESP_LOGI(TAG_FLOW_ACK, "RX L1 Cmd (startRsp/stopRsp?) len=%u", (unsigned)plen);
        /* startRsp 协商值以手环回包为准（笔记 §B6）；此处仅记录。 */
        break;
    default: break;
    }
}

void sar_tick(sar_t *s, uint32_t now_ms_arg)
{
    (void)now_ms_arg;
    uint32_t t = now_ms();
    for (int slot = 0; slot < SLOTN; slot++) {
        if (!s->pending_active[slot]) continue;
        if (t - s->pending_sent_ms[slot] >= SAR_SEND_TIMEOUT_MS) {
            if (s->pending_retries[slot] < SAR_MAX_RETRIES) {
                s->pending_retries[slot]++;
                s->pending_sent_ms[slot] = t;
                if (s->ble_write)
                    s->ble_write(s->pending_buf[slot], s->pending_len[slot], s->ble_ctx);
                ESP_LOGI(TAG_FLOW_ACK, "timeout slot=%d -> retransmit #%d",
                         slot, s->pending_retries[slot]);
            } else {
                ESP_LOGW(TAG_FLOW_ACK, "slot=%d gave up after %d retries",
                         slot, SAR_MAX_RETRIES);
                s->pending_active[slot] = false;
            }
        }
    }
}

int sar_start_link(sar_t *s)
{
    /* L1 startReq：cmd=1，其后 TLV（笔记 §B2.2） */
    uint8_t pdu[64];
    size_t pos = 0;
    pdu[pos++] = 1; /* startReq 命令码 */

    /* TLV: key=1 version(3B)=1.0.0 */
    pdu[pos++] = 1; pdu[pos++] = 3; pdu[pos++] = 0;
    pdu[pos++] = 1; pdu[pos++] = 0; pdu[pos++] = 0;
    /* TLV: key=2 MPS(2B LE)=64512 */
    pdu[pos++] = 2; pdu[pos++] = 2; pdu[pos++] = 0;
    pdu[pos++] = (uint8_t)(SAR_MPS & 0xFF);
    pdu[pos++] = (uint8_t)((SAR_MPS >> 8) & 0xFF);
    /* TLV: key=3 TX_WIN(2B LE)=32 */
    pdu[pos++] = 3; pdu[pos++] = 2; pdu[pos++] = 0;
    pdu[pos++] = SAR_TX_WIN; pdu[pos++] = 0;
    /* TLV: key=4 SEND_TIMEOUT(2B LE)=10000ms */
    pdu[pos++] = 4; pdu[pos++] = 2; pdu[pos++] = 0;
    pdu[pos++] = (uint8_t)(SAR_SEND_TIMEOUT_MS & 0xFF);
    pdu[pos++] = (uint8_t)((SAR_SEND_TIMEOUT_MS >> 8) & 0xFF);

    uint8_t frame[128];
    size_t flen = l1_encode(frame, sizeof(frame),
                            L1_TYPE_CMD, false, s->tx_seq++, pdu, pos);
    if (flen == 0) return -1;
    ESP_LOGI(TAG_FLOW_ACK, "TX startReq MPS=%d WIN=%d TO=%dms",
             SAR_MPS, SAR_TX_WIN, SAR_SEND_TIMEOUT_MS);
    return s->ble_write(frame, flen, s->ble_ctx);
}
