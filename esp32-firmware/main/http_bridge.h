/*
 * http_bridge.h — 由 QAIC 调用的 HTTP(S) 执行后端（Task 6 实现）
 *
 * QAIC 解出手环 fetch 请求后，调本接口真正发 HTTP，拿回状态码/头/体。
 * 非流式（v1/v2/v3）：一次性拿回整个 body。
 * 流式（v4）：通过 read 回调按 offset 拉取（背压由 QAIC 窗口控制）。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

/* 一次 HTTP 结果（非流式） */
typedef struct {
    int   status;          /* HTTP 状态码；0=传输层错误 */
    char  status_text[64];
    char *headers_json;    /* cJSON 序列化的 headers 对象（QAIC 拥有后释放） */
    uint8_t *body;         /* 响应体（调用方释放） */
    size_t body_len;
    char  content_type[64];
    int   content_length; /* -1 = 未知 */
} http_result_t;

/*
 * 执行一次 fetch。
 * method/url/headers_json/body 均来自手环请求。
 * follow_redirects 已在 QAIC 层按跳数控制；本函数内部不再跨跳（或最多跟到由参数指定）。
 * 返回 0 成功，<0 失败。结果由 out 带出（堆分配）。
 */
int http_bridge_fetch(const char *method, const char *url,
                     const char *headers_json,
                     const char *body, size_t body_len,
                     int max_redirects,
                     http_result_t *out);

/* 释放 http_result_t 内部分配 */
void http_result_free(http_result_t *r);

/* ---- v4 流式 ---- */
typedef struct http_stream http_stream_t;

/* 打开一个流；返回句柄，NULL=失败。 */
http_stream_t *http_bridge_open_stream(const char *method, const char *url,
                                       const char *headers_json,
                                       const char *body, size_t body_len);
/* 从绝对 offset 读至多 len 字节到 out；返回读到的字节数，0=EOF，<0=错。 */
int http_bridge_stream_read(http_stream_t *s, uint64_t offset,
                           uint8_t *out, size_t len);
/* 流总字节数（Content-Length，未知返回 -1） */
int64_t http_bridge_stream_total(http_stream_t *s);
const char *http_bridge_stream_content_type(http_stream_t *s);
void http_bridge_stream_close(http_stream_t *s);
