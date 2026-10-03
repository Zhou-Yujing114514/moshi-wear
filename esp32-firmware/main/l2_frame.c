/*
 * l2_frame.c — L2 通道层编解码（笔记 §B2.3）
 */
#include "l2_frame.h"
#include <string.h>

size_t l2_encode(uint8_t *out, size_t out_cap,
                 l2_channel_t ch, l2_opcode_t op,
                 const uint8_t *payload, size_t payload_len)
{
    size_t total = L2_HEADER_LEN + payload_len;
    if (out_cap < total) return 0;
    out[0] = (uint8_t)ch;
    out[1] = (uint8_t)op;
    if (payload_len && payload)
        memcpy(out + L2_HEADER_LEN, payload, payload_len);
    return total;
}

size_t l2_decode(const uint8_t *buf, size_t len,
                 l2_channel_t *out_ch, l2_opcode_t *out_op,
                 const uint8_t **out_payload, size_t *out_payload_len)
{
    if (len < L2_HEADER_LEN) return 0;
    if (out_ch) *out_ch = (l2_channel_t)buf[0];
    if (out_op) *out_op = (l2_opcode_t)buf[1];
    if (out_payload) *out_payload = buf + L2_HEADER_LEN;
    if (out_payload_len) *out_payload_len = len - L2_HEADER_LEN;
    return len;
}
