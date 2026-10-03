/*
 * proto_pack.h — 最小 protobuf 线格式编解码器（仅实现所需子集）
 *
 * 仅支持：
 *   - varint (wire type 0)：int32/int64/bool/enum
 *   - length-delimited (wire type 2)：bytes / string / 嵌套 message
 *   - fixed32 (wire type 5)：预留
 * 不做 schema 校验；字段编号在 proto_const.h 配置（待核实）。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- 写入器 ---- */
typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
} pbuf_t;

void pbuf_init(pbuf_t *w, uint8_t *buf, size_t cap);
size_t pbuf_len(const pbuf_t *w);

/* 写 varint 字段（field 编号，val） */
bool pbuf_varint(pbuf_t *w, uint32_t field, uint64_t val);
/* 写 bool（varint） */
bool pbuf_bool(pbuf_t *w, uint32_t field, bool v);
/* 写 length-delimited：raw bytes */
bool pbuf_bytes(pbuf_t *w, uint32_t field, const void *data, size_t len);
/* 写字符串 */
bool pbuf_string(pbuf_t *w, uint32_t field, const char *s);
/*
 * 写嵌套消息：先回调 build 到临时缓冲，再以 bytes 字段写入。
 * builder_ctx 透传给回调。返回是否成功。
 */
typedef bool (*pbuf_builder_fn)(pbuf_t *inner, void *ctx);
bool pbuf_message(pbuf_t *w, uint32_t field, pbuf_builder_fn fn, void *ctx);

/* ---- 读取器 ---- */
typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
} prbuf_t;

typedef enum {
    PWF_VARINT = 0,
    PWF_FIXED64 = 1,
    PWF_LEN = 2,
    PWF_FIXED32 = 5
} pwire_t;

void prbuf_init(prbuf_t *r, const uint8_t *buf, size_t len);
/* 遍历下一个字段；返回 false 表示读完。 */
bool prbuf_next(prbuf_t *r, uint32_t *out_field, pwire_t *out_wire);
/* 取当前 varint 值 */
bool prbuf_get_varint(prbuf_t *r, uint64_t *out);
/* 取当前 bytes 片段（指针+长度，不拷贝） */
bool prbuf_get_bytes(prbuf_t *r, const uint8_t **out, size_t *out_len);

/*
 * 便捷：在当前消息缓冲里查找指定 field 的第一个 length-delimited 片段。
 * 不改变全局遍历状态（内部拷贝读取器）。
 */
bool proto_find_bytes(const uint8_t *msg, size_t msg_len,
                      uint32_t field, const uint8_t **out, size_t *out_len);
bool proto_find_varint(const uint8_t *msg, size_t msg_len,
                       uint32_t field, uint64_t *out);
