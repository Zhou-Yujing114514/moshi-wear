/*
 * l1_frame.c — L1 帧编解码实现（笔记 §B2.1）
 */
#include "l1_frame.h"
#include "log_tags.h"
#include "esp_log.h"
#include <string.h>

/* CRC16-ARC：poly=0xA001，init=0x0000，输入/输出均不反射额外处理
 * （0xA001 已是反射多项式）。仅对 payload 计算。 */
uint16_t crc16_arc(const uint8_t *data, size_t len)
{
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

static inline void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static inline uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

size_t l1_encode(uint8_t *out, size_t out_cap,
                 l1_type_t type, bool frx, uint8_t seq,
                 const uint8_t *payload, size_t payload_len)
{
    if (payload_len > 0xFFFF) return 0;
    size_t total = L1_HEADER_LEN + payload_len;
    if (out_cap < total) return 0;

    out[0] = (uint8_t)(L1_MAGIC & 0xFF);        /* A5 */
    out[1] = (uint8_t)((L1_MAGIC >> 8) & 0xFF); /* A5 */
    out[2] = (uint8_t)((type & 0x0F) | (frx ? L1_FRX_FLAG : 0));
    out[3] = seq;
    put_le16(out + 4, (uint16_t)payload_len);

    uint16_t crc = crc16_arc(payload, payload_len);
    put_le16(out + 6, crc);

    if (payload_len && payload)
        memcpy(out + L1_HEADER_LEN, payload, payload_len);

    ESP_LOGD(TAG_L1_SEND, "enc type=%d frx=%d seq=%u len=%u crc=0x%04X",
             type, frx, seq, (unsigned)payload_len, crc);
    return total;
}

size_t l1_decode(const uint8_t *buf, size_t len,
                 l1_type_t *out_type, bool *out_frx, uint8_t *out_seq,
                 const uint8_t **out_payload, size_t *out_payload_len)
{
    if (len < L1_HEADER_LEN) {
        ESP_LOGD(TAG_L1_RECV, "too short: %u < %d", (unsigned)len, L1_HEADER_LEN);
        return 0;
    }
    if (buf[0] != (L1_MAGIC & 0xFF) || buf[1] != ((L1_MAGIC >> 8) & 0xFF)) {
        ESP_LOGW(TAG_L1_RECV, "bad magic 0x%02X%02X", buf[1], buf[0]);
        return 0;
    }
    uint8_t t = buf[2];
    l1_type_t type = (l1_type_t)(t & 0x0F);
    bool frx = (t & L1_FRX_FLAG) != 0;
    uint8_t seq = buf[3];
    uint16_t plen = get_le16(buf + 4);
    uint16_t crc_recv = get_le16(buf + 6);

    if ((size_t)L1_HEADER_LEN + plen > len) {
        ESP_LOGW(TAG_L1_RECV, "truncated: need %u have %u",
                 (unsigned)(L1_HEADER_LEN + plen), (unsigned)len);
        return 0;
    }
    const uint8_t *payload = buf + L1_HEADER_LEN;
    uint16_t crc_calc = crc16_arc(payload, plen);
    if (crc_calc != crc_recv) {
        ESP_LOGW(TAG_L1_RECV, "crc mismatch calc=0x%04X recv=0x%04X seq=%u",
                 crc_calc, crc_recv, seq);
        return 0; /* 校验失败 → 上交 Nak（见 sar.c） */
    }

    if (out_type) *out_type = type;
    if (out_frx) *out_frx = frx;
    if (out_seq) *out_seq = seq;
    if (out_payload) *out_payload = payload;
    if (out_payload_len) *out_payload_len = plen;

    ESP_LOGD(TAG_L1_RECV, "dec type=%d frx=%d seq=%u len=%u", type, frx, seq, plen);
    return L1_HEADER_LEN + plen;
}
