/*
 * proto_pack.c — 最小 protobuf 线格式实现（varint / length-delimited）
 */
#include "proto_pack.h"
#include <string.h>

void pbuf_init(pbuf_t *w, uint8_t *buf, size_t cap)
{
    w->buf = buf; w->cap = cap; w->pos = 0;
}
size_t pbuf_len(const pbuf_t *w) { return w->pos; }

static bool emit_byte(pbuf_t *w, uint8_t b)
{
    if (w->pos >= w->cap) return false;
    w->buf[w->pos++] = b;
    return true;
}

static bool emit_varint(pbuf_t *w, uint64_t v)
{
    do {
        uint8_t b = v & 0x7F;
        v >>= 7;
        if (v) b |= 0x80;
        if (!emit_byte(w, b)) return false;
    } while (v);
    return true;
}

static bool write_key(pbuf_t *w, uint32_t field, pwire_t wire)
{
    uint64_t key = ((uint64_t)field << 3) | (uint64_t)wire;
    return emit_varint(w, key);
}

bool pbuf_varint(pbuf_t *w, uint32_t field, uint64_t val)
{
    if (!write_key(w, field, PWF_VARINT)) return false;
    return emit_varint(w, val);
}

bool pbuf_bool(pbuf_t *w, uint32_t field, bool v)
{
    return pbuf_varint(w, field, v ? 1 : 0);
}

bool pbuf_bytes(pbuf_t *w, uint32_t field, const void *data, size_t len)
{
    if (!write_key(w, field, PWF_LEN)) return false;
    if (!emit_varint(w, len)) return false;
    if (len) {
        if (w->pos + len > w->cap) return false;
        memcpy(w->buf + w->pos, data, len);
        w->pos += len;
    }
    return true;
}

bool pbuf_string(pbuf_t *w, uint32_t field, const char *s)
{
    return pbuf_bytes(w, field, s, strlen(s));
}

bool pbuf_message(pbuf_t *w, uint32_t field, pbuf_builder_fn fn, void *ctx)
{
    /* 先在尾部预留位置，构建后回填长度。
     * 简化做法：直接要求内部用同一缓冲构建，先记 pos0，写 key+占位长度，
     * 构建后若超长则回滚。这里用临时栈缓冲不现实，故采用"两步"：
     * 先构建到 (cap) 区域会破坏布局，因此改用：记录起点，写 key，
     * 然后构建到紧跟其后，构建完成后若溢出则失败。 */
    size_t key_pos = w->pos;
    (void)key_pos; /* 起点仅用于回滚排查；当前实现不回滚 */
    if (!write_key(w, field, PWF_LEN)) return false;
    size_t len_pos = w->pos;
    /* 预留 varint 长度（最多 5 字节足够） */
    size_t inner_start = w->pos + 5;
    if (inner_start > w->cap) return false;
    pbuf_t inner;
    inner.buf = w->buf + inner_start;
    inner.cap = w->cap - inner_start;
    inner.pos = 0;
    if (!fn(&inner, ctx)) return false;
    size_t inner_len = inner.pos;
    /* 回填长度 varint（可能 < 5 字节，需移动数据） */
    uint8_t tmp[5];
    pbuf_t lw; pbuf_init(&lw, tmp, sizeof(tmp));
    if (!emit_varint(&lw, inner_len)) return false;
    size_t len_bytes = lw.pos;
    /* 移动 inner 数据到 len_pos+len_bytes */
    memmove(w->buf + len_pos + len_bytes, w->buf + inner_start, inner_len);
    memcpy(w->buf + len_pos, tmp, len_bytes);
    w->pos = len_pos + len_bytes + inner_len;
    return true;
}

/* ---- 读取器 ---- */
void prbuf_init(prbuf_t *r, const uint8_t *buf, size_t len)
{
    r->buf = buf; r->len = len; r->pos = 0;
}

static bool read_varint(prbuf_t *r, uint64_t *out)
{
    uint64_t v = 0;
    int shift = 0;
    while (r->pos < r->len) {
        uint8_t b = r->buf[r->pos++];
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { *out = v; return true; }
        shift += 7;
        if (shift > 63) return false;
    }
    return false;
}

bool prbuf_next(prbuf_t *r, uint32_t *out_field, pwire_t *out_wire)
{
    if (r->pos >= r->len) return false;
    uint64_t key;
    if (!read_varint(r, &key)) return false;
    uint32_t field = (uint32_t)(key >> 3);
    pwire_t wire = (pwire_t)(key & 0x7);
    if (wire != PWF_VARINT && wire != PWF_LEN && wire != PWF_FIXED32) {
        /* 不支持的 wire type：跳过（仅 length-delimited 可安全跳） */
        if (wire == PWF_FIXED64) { if (r->pos + 8 > r->len) return false; r->pos += 8; }
        else if (wire == PWF_FIXED32) { if (r->pos + 4 > r->len) return false; r->pos += 4; }
        else return false;
    }
    if (out_field) *out_field = field;
    if (out_wire) *out_wire = wire;
    return true;
}

bool prbuf_get_varint(prbuf_t *r, uint64_t *out)
{
    return read_varint(r, out);
}

bool prbuf_get_bytes(prbuf_t *r, const uint8_t **out, size_t *out_len)
{
    uint64_t len;
    if (!read_varint(r, &len)) return false;
    if (r->pos + len > r->len) return false;
    if (out) *out = r->buf + r->pos;
    if (out_len) *out_len = (size_t)len;
    r->pos += (size_t)len;
    return true;
}

bool proto_find_bytes(const uint8_t *msg, size_t msg_len,
                      uint32_t field, const uint8_t **out, size_t *out_len)
{
    prbuf_t r; prbuf_init(&r, msg, msg_len);
    uint32_t f; pwire_t w;
    while (prbuf_next(&r, &f, &w)) {
        if (f == field && w == PWF_LEN)
            return prbuf_get_bytes(&r, out, out_len);
        if (w == PWF_LEN) { const uint8_t *d; size_t l; if (!prbuf_get_bytes(&r,&d,&l)) return false; }
        else { uint64_t v; if (!prbuf_get_varint(&r,&v)) return false; }
    }
    return false;
}

bool proto_find_varint(const uint8_t *msg, size_t msg_len,
                       uint32_t field, uint64_t *out)
{
    prbuf_t r; prbuf_init(&r, msg, msg_len);
    uint32_t f; pwire_t w;
    while (prbuf_next(&r, &f, &w)) {
        if (f == field && w == PWF_VARINT)
            return prbuf_get_varint(&r, out);
        if (w == PWF_LEN) { const uint8_t *d; size_t l; if (!prbuf_get_bytes(&r,&d,&l)) return false; }
        else { uint64_t v; if (!prbuf_get_varint(&r,&v)) return false; }
    }
    return false;
}
