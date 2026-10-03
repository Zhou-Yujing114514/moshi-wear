/*
 * http_bridge.c — 用 esp_http_client 执行手环 fetch 请求（笔记 §4）
 *
 *  - 方法映射 GET/POST/PUT/DELETE/HEAD/PATCH/OPTIONS
 *  - headers 按手环给出的 JSON 键值表逐个 set_header
 *  - body 为字符串（二进制由手环自行 base64，§4.2）
 *  - 重定向：max_redirects>0 时开 follow；ESP-IDF 内部跟随 301/302/303/307/308
 *    （笔记 §4.2 的跨源删凭据语义由 ESP-IDF 尽量满足；真机若发现凭据泄漏需在此补）
 *
 * 下行响应压缩（deflate/lz4）：当前实现回退为 none（qaic.c 已注明），
 * 保证功能正确；体积优化可在本文件接入 zlib deflateInit2(windowBits=-15)。
 */
#include "http_bridge.h"
#include "config.h"
#include "log_tags.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <inttypes.h>

/* 累积 body 的回调缓冲 */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} acc_t;

static void acc_push(acc_t *a, const char *data, size_t n)
{
    if (a->len + n > a->cap) {
        size_t ncap = a->cap ? a->cap*2 : 1024;
        while (ncap < a->len + n) ncap *= 2;
        a->buf = realloc(a->buf, ncap);
        a->cap = ncap;
    }
    memcpy(a->buf + a->len, data, n);
    a->len += n;
}

static esp_err_t event_cb(esp_http_client_event_t *evt)
{
    acc_t *a = evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (!esp_http_client_is_chunked_response(evt->client))
            acc_push(a, evt->data, evt->data_len);
        break;
    default: break;
    }
    return ESP_OK;
}

/* 把 headers JSON 对象转成 esp_http_client header 迭代 */
static void apply_headers(esp_http_client_handle_t cli, const char *headers_json)
{
    if (!headers_json) return;
    cJSON *h = cJSON_Parse(headers_json);
    if (!h) return;
    cJSON *kv;
    cJSON_ArrayForEach(kv, h) {
        if (!cJSON_IsString(kv)) continue;
        esp_http_client_set_header(cli, kv->string, kv->valuestring);
    }
    cJSON_Delete(h);
}

int http_bridge_fetch(const char *method, const char *url,
                       const char *headers_json,
                       const char *body, size_t body_len,
                       int max_redirects,
                       http_result_t *out)
{
    memset(out, 0, sizeof(*out));
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = event_cb,
        .buffer_size = 2048,
        .timeout_ms = 10000,
        .keep_alive_enable = true,
    };
    acc_t acc = {0};
    cfg.user_data = &acc;

    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (!cli) return -1;

    if (max_redirects > 0)
        esp_http_client_set_redirection(cli);   /* 内部跟随（跳数上限由 esp 控制） */

    apply_headers(cli, headers_json);

    esp_http_client_set_method(cli,
        strcasecmp(method,"POST")==0 ? HTTP_METHOD_POST :
        strcasecmp(method,"PUT")==0 ? HTTP_METHOD_PUT :
        strcasecmp(method,"DELETE")==0 ? HTTP_METHOD_DELETE :
        strcasecmp(method,"HEAD")==0 ? HTTP_METHOD_HEAD :
        strcasecmp(method,"PATCH")==0 ? HTTP_METHOD_PATCH :
        strcasecmp(method,"OPTIONS")==0 ? HTTP_METHOD_OPTIONS :
        HTTP_METHOD_GET);

    if (body && body_len)
        esp_http_client_set_post_field(cli, body, body_len);

    esp_err_t err = esp_http_client_perform(cli);
    if (err != ESP_OK) {
        ESP_LOGW(TAG_HTTP, "perform failed: %s", esp_err_to_name(err));
        free(acc.buf);
        esp_http_client_cleanup(cli);
        return -1;
    }

    out->status = esp_http_client_get_status_code(cli);
    strncpy(out->status_text, out->status==200?"OK":"HTTP", sizeof(out->status_text)-1);

    /* content-type */
    char ct[64] = {0};
    if (esp_http_client_get_header(cli, "content-type", ct) == ESP_OK)
        strncpy(out->content_type, ct, sizeof(out->content_type)-1);

    /* content-length（可能 -1） */
    int64_t cl = esp_http_client_get_content_length(cli);
    out->content_length = (cl < INT32_MAX) ? (int)cl : -1;

    /* headers 对象 JSON（简化：仅 content-type；真机可遍历所有响应头） */
    cJSON *h = cJSON_CreateObject();
    if (out->content_type[0])
        cJSON_AddStringToObject(h, "content-type", out->content_type);
    out->headers_json = cJSON_PrintUnformatted(h);
    cJSON_Delete(h);

    out->body = (uint8_t*)acc.buf;
    out->body_len = acc.len;
    if (!out->body) { out->body = malloc(1); out->body_len = 0; }

    esp_http_client_cleanup(cli);
    ESP_LOGI(TAG_HTTP, "%s -> %d, body=%u bytes", url, out->status, (unsigned)out->body_len);
    return 0;
}

void http_result_free(http_result_t *r)
{
    if (!r) return;
    free(r->body); r->body = NULL;
    free(r->headers_json); r->headers_json = NULL;
}

/* ================= v4 流式（简化实现） =================
 * 真机边下边播需用 esp_http_client 的 chunked 增量回调；
 * 这里给出可编译的占位实现：整包缓冲后按 offset 读。 */
struct http_stream {
    uint8_t *data;
    size_t   len;
    char     content_type[64];
    int      status;
};

http_stream_t *http_bridge_open_stream(const char *method, const char *url,
                                     const char *headers_json,
                                     const char *body, size_t body_len)
{
    http_result_t r;
    if (http_bridge_fetch(method, url, headers_json, body, body_len, 0, &r) != 0)
        return NULL;
    http_stream_t *s = calloc(1, sizeof(*s));
    s->data = r.body; s->len = r.body_len;
    strncpy(s->content_type, r.content_type, sizeof(s->content_type)-1);
    s->status = r.status;
    free(r.headers_json);
    return s;
}

int http_bridge_stream_read(http_stream_t *s, uint64_t offset,
                            uint8_t *out, size_t len)
{
    if (offset >= s->len) return 0;               /* EOF */
    size_t avail = s->len - (size_t)offset;
    if (len > avail) len = avail;
    memcpy(out, s->data + offset, len);
    return (int)len;
}

int64_t http_bridge_stream_total(http_stream_t *s) { return (int64_t)s->len; }
const char *http_bridge_stream_content_type(http_stream_t *s) { return s->content_type; }

void http_bridge_stream_close(http_stream_t *s)
{
    if (!s) return;
    free(s->data);
    free(s);
}
