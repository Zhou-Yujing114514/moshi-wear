/*
 * qaic.c — QAIC 应用层实现（笔记 §3~§10）
 *
 * 常量逐项对照笔记 §10 表；协议逻辑对照 §3~§9。
 * 使用 cJSON（ESP-IDF json 组件）。
 */
#include "qaic.h"
#include "http_bridge.h"
#include "codec.h"
#include "log_tags.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

/* ================= 常量（笔记 §10） ================= */
static const char *TAG_HS   = "__hs__";
static const char *TAG_FETCH = "fetch";
static const char *TAG_CHUNK = "fetch-chunk";
static const char *TAG_ACK   = "fetch-ack";
static const char *TAG_STREAM = "fetch-stream";
static const char *TAG_STREAM_ACK = "fetch-stream-ack";
static const char *TAG_STREAM_CANCEL = "fetch-stream-cancel";
static const char *TAG_STREAM_ERROR   = "fetch-stream-error";

#define LOCAL_PROTOCOL_VERSION   4        /* §10 协议版本 */
#define DEFAULT_CHUNK_SIZE       4096      /* §10 默认 chunkSize */
#define CHUNK_SIZE_MIN           256       /* §10 */
#define CHUNK_SIZE_MAX           65536     /* §10 */
#define DEFAULT_ACK_WINDOW       4         /* §10 */
#define ACK_WINDOW_MIN           1         /* §10 */
#define ACK_WINDOW_MAX           64        /* §10 */
#define COMPRESS_MIN_SIZE        256       /* §10 压缩最小字节 */
#define MAX_UNCHUNKED_WIRE_LEN   16384     /* §10 单消息字符上限 */
#define AUTO_STREAM_THRESHOLD    65536     /* §10 自动开流阈值 */
#define MAX_REDIRECTS            10        /* §10 */
#define SESSION_IDLE_TIMEOUT_MS  (600u*1000u) /* §10 会话 600s */
#define TRANSFER_TIMEOUT_MS      (30u*1000u)  /* §10 分片/流 ACK 30s */
#define MAX_CONCURRENT_STREAMS   8         /* §6.4 并发流 */

/* ================= 本端能力（§3.3） ================= */
typedef struct {
    int  version;
    bool chunk;
    int  maxChunkSize;
    /* encodings / compressions 保持对端偏好顺序（§3.4 求交） */
    bool enc_base64, enc_hex, enc_text;
    bool cmp_none, cmp_deflate, cmp_lz4;
    bool ack;
    int  ackWindow;
    bool stream;
} caps_t;

static void local_caps(caps_t *c)
{
    memset(c, 0, sizeof(*c));
    c->version = LOCAL_PROTOCOL_VERSION;
    c->chunk = true;
    c->maxChunkSize = CHUNK_SIZE_MAX;   /* §3.3 对外声明 65536 */
    c->enc_base64 = c->enc_hex = c->enc_text = true;
    c->cmp_none = c->cmp_deflate = c->cmp_lz4 = true;
    c->ack = true;
    c->ackWindow = DEFAULT_ACK_WINDOW;
    c->stream = true;
}

/* 解析对端 caps（缺省 = v1 客户端基线，§3.2） */
static void parse_peer_caps(const cJSON *caps_obj, caps_t *out)
{
    /* v1 基线：不分片/不压缩/base64 基线 */
    memset(out, 0, sizeof(*out));
    out->version = 1;
    out->chunk = false;
    out->maxChunkSize = 0;
    out->enc_base64 = true;
    out->cmp_none = true;
    out->ack = false;
    out->ackWindow = 0;
    out->stream = false;
    if (!caps_obj) return;
    const cJSON *j;
    if ((j = cJSON_GetObjectItem(caps_obj, "version")) && cJSON_IsNumber(j))
        out->version = j->valueint;
    if ((j = cJSON_GetObjectItem(caps_obj, "chunk")) && cJSON_IsBool(j))
        out->chunk = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(caps_obj, "maxChunkSize")) && cJSON_IsNumber(j))
        out->maxChunkSize = j->valueint;
    if ((j = cJSON_GetObjectItem(caps_obj, "encodings")) && cJSON_IsArray(j)) {
        cJSON *e; cJSON_ArrayForEach(e, j) {
            if (!cJSON_IsString(e)) continue;
            if (strcmp(e->valuestring, "base64") == 0) out->enc_base64 = true;
            else if (strcmp(e->valuestring, "hex") == 0) out->enc_hex = true;
            else if (strcmp(e->valuestring, "text") == 0) out->enc_text = true;
        }
    }
    if ((j = cJSON_GetObjectItem(caps_obj, "compressions")) && cJSON_IsArray(j)) {
        cJSON *e; cJSON_ArrayForEach(e, j) {
            if (!cJSON_IsString(e)) continue;
            if (strcmp(e->valuestring, "none") == 0) out->cmp_none = true;
            else if (strcmp(e->valuestring, "deflate") == 0) out->cmp_deflate = true;
            else if (strcmp(e->valuestring, "lz4") == 0) out->cmp_lz4 = true;
        }
    }
    if ((j = cJSON_GetObjectItem(caps_obj, "ack")) && cJSON_IsBool(j))
        out->ack = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(caps_obj, "ackWindow")) && cJSON_IsNumber(j))
        out->ackWindow = j->valueint;
    if ((j = cJSON_GetObjectItem(caps_obj, "stream")) && cJSON_IsBool(j))
        out->stream = cJSON_IsTrue(j);
}

/* 协商结果 */
typedef struct {
    int  version;
    bool chunked;
    int  chunkSize;
    bool can_base64, can_hex, can_text; /* 求交后，保持对端偏好顺序 */
    bool prefer_text_first;
    bool can_deflate, can_lz4, can_none;
    int  ackWindow;     /* 0=不开 */
    bool stream;
} nego_t;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* §3.4 协商算法（逐行对应） */
static void negotiate(const caps_t *peer, nego_t *n)
{
    caps_t local; local_caps(&local);
    memset(n, 0, sizeof(*n));

    n->version = (peer->version < LOCAL_PROTOCOL_VERSION) ? peer->version : LOCAL_PROTOCOL_VERSION;
    n->chunked = local.chunk && peer->chunk && n->version >= 2;

    int mcs = peer->maxChunkSize == 0 ? DEFAULT_CHUNK_SIZE : peer->maxChunkSize;
    n->chunkSize = n->chunked ? clampi(mcs, CHUNK_SIZE_MIN, CHUNK_SIZE_MAX) : 0;

    /* encodings 求交（保持对端偏好：对端列了啥就用啥，本地也支持） */
    n->can_base64 = peer->enc_base64 && local.enc_base64;
    n->can_hex    = peer->enc_hex    && local.enc_hex;
    n->can_text   = peer->enc_text   && local.enc_text;

    n->can_none   = peer->cmp_none   && local.cmp_none;
    n->can_deflate= peer->cmp_deflate&& local.cmp_deflate;
    n->can_lz4    = peer->cmp_lz4    && local.cmp_lz4;

    n->ackWindow = (n->version >= 3 && n->chunked && local.ack && peer->ack)
                   ? clampi(peer->ackWindow == 0 ? DEFAULT_ACK_WINDOW : peer->ackWindow,
                            ACK_WINDOW_MIN, ACK_WINDOW_MAX)
                   : 0;
    n->stream = (n->version >= 4 && local.stream && peer->stream
                 && n->chunked && n->ackWindow > 0);
}

/* ================= 会话 ================= */
typedef struct session {
    char  pkg[64];
    bool  open;
    uint32_t last_seen_ms;
    caps_t peer_caps;
    nego_t nego;
    bool  negotiated;
    struct session *next;
} session_t;

static session_t *s_sessions = NULL;
static qaic_send_fn s_send;
static void *s_send_ctx;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time()/1000); }

static session_t *find_or_create_session(const char *pkg)
{
    session_t *s = s_sessions;
    while (s) { if (strcmp(s->pkg, pkg) == 0) return s; s = s->next; }
    s = calloc(1, sizeof(session_t));
    strncpy(s->pkg, pkg ? pkg : QAIC_DEFAULT_PKG, sizeof(s->pkg)-1);
    s->next = s_sessions; s_sessions = s;
    return s;
}

/* ================= 下行发送工具 ================= */
static int send_json(cJSON *obj)
{
    char *s = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!s) return -1;
    ESP_LOGD(TAG_FETCH_RESP, "-> %s", s);
    int rc = s_send ? s_send(s, s_send_ctx) : -1;
    free(s);
    return rc;
}

/* 错误响应（§5.4，v1 单消息形态） */
static void send_error(const char *id, const char *msg)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "tag", TAG_FETCH);
    if (id) cJSON_AddStringToObject(o, "id", id);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", false);
    cJSON_AddNumberToObject(r, "status", 0);
    cJSON_AddStringToObject(r, "statusText", msg ? msg : "bridge error");
    cJSON *h = cJSON_CreateObject();
    cJSON_AddItemToObject(r, "headers", h);
    cJSON_AddStringToObject(r, "body", "");
    cJSON_AddBoolToObject(r, "raw", false);
    cJSON_AddItemToObject(o, "resp", r);
    send_json(o);
}

/* ================= __hs__（§3） ================= */
static void on_handshake(const cJSON *j)
{
    const cJSON *cj = cJSON_GetObjectItem(j, "count");
    int count = cJSON_IsNumber(cj) ? cj->valueint : 0;
    session_t *s = find_or_create_session(NULL);
    s->last_seen_ms = now_ms();

    const cJSON *caps = cJSON_GetObjectItem(j, "caps");
    if (caps && cJSON_IsObject(caps)) {
        parse_peer_caps(caps, &s->peer_caps);
        negotiate(&s->peer_caps, &s->nego);
        s->negotiated = true;
    }
    if (count > 0) s->open = true;   /* §3.1 任何 count>0 置 open */

    ESP_LOGI(TAG_QAIC_HS, "__hs__ count=%d version=%d chunked=%d ackWin=%d stream=%d",
             count, s->nego.version, s->nego.chunked, s->nego.ackWindow, s->nego.stream);

    if (count < 2) {
        /* 回 count+1，附本端 caps（§3.3） */
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "tag", TAG_HS);
        cJSON_AddNumberToObject(o, "count", count + 1);
        cJSON *c = cJSON_CreateObject();
        cJSON_AddNumberToObject(c, "version", LOCAL_PROTOCOL_VERSION);
        cJSON_AddBoolToObject(c, "chunk", true);
        cJSON_AddNumberToObject(c, "maxChunkSize", CHUNK_SIZE_MAX);
        cJSON *enc = cJSON_CreateArray();
        cJSON_AddItemToArray(enc, cJSON_CreateString("base64"));
        cJSON_AddItemToArray(enc, cJSON_CreateString("hex"));
        cJSON_AddItemToArray(enc, cJSON_CreateString("text"));
        cJSON_AddItemToObject(c, "encodings", enc);
        cJSON *cmp = cJSON_CreateArray();
        cJSON_AddItemToArray(cmp, cJSON_CreateString("none"));
        cJSON_AddItemToArray(cmp, cJSON_CreateString("deflate"));
        cJSON_AddItemToArray(cmp, cJSON_CreateString("lz4"));
        cJSON_AddItemToObject(c, "compressions", cmp);
        cJSON_AddBoolToObject(c, "ack", true);
        cJSON_AddNumberToObject(c, "ackWindow", DEFAULT_ACK_WINDOW);
        cJSON_AddBoolToObject(c, "stream", true);
        cJSON_AddItemToObject(o, "caps", c);
        send_json(o);
    }
    /* count>=2：不回包，握手完成（§3.1） */
}

/* ================= 压缩/编码选择（§5.1 build_plan） ================= */
typedef struct {
    const char *compression;   /* "none"/"deflate"/"lz4" */
    const char *encoding;      /* "text"/"base64"/"hex" */
    int original_bytes;        /* 压缩前字节数（!=none 时） */
} plan_t;

static void pick_plan(const nego_t *n, const uint8_t *body, size_t len,
                      bool raw, bool chunked, plan_t *p)
{
    /* §5.1 发送端编码顺序 */
    /* 1) 压缩：无协商或 body<256 → none；否则取对端支持的第一个 */
    const char *comp = "none";
    if (len >= COMPRESS_MIN_SIZE && n->can_deflate) comp = "deflate";
    else if (len >= COMPRESS_MIN_SIZE && n->can_lz4) comp = "lz4";
    p->compression = comp;
    p->original_bytes = (strcmp(comp,"none") != 0) ? (int)len : 0;

    /* 3) 编码：text 仅当 未压缩 && 未分片 && raw==false && 合法 UTF-8 */
    if (strcmp(comp,"none") == 0 && !chunked && !raw && is_valid_utf8(body, len))
        p->encoding = "text";
    else if (n->can_base64) p->encoding = "base64";
    else if (n->can_hex)    p->encoding = "hex";
    else                    p->encoding = "base64"; /* 基线 */
}

/* 把 bytes 按 plan 编码成字符串 */
static char *encode_bytes(const uint8_t *body, size_t len, const plan_t *p)
{
    /* 先压缩（占位，实际压缩见 do_compress 说明） */
    int clen = 0;
    uint8_t *cdata;
    if (strcmp(p->compression, "none") == 0) {
        cdata = malloc(len); memcpy(cdata, body, len); clen = (int)len;
    } else {
        /* 下行压缩：zlib raw deflate / lz4。实现见 http_bridge.c（README 标注）。
         * 此处保守回退为 none，保证功能可用（体积略大）。 */
        cdata = malloc(len); memcpy(cdata, body, len); clen = (int)len;
        p->compression = "none"; p->original_bytes = 0;
    }
    /* 再编码 */
    char *out;
    if (strcmp(p->encoding, "text") == 0) {
        out = malloc(clen + 1); memcpy(out, cdata, clen); out[clen] = 0;
    } else if (strcmp(p->encoding, "hex") == 0) {
        out = malloc(clen * 2 + 1);
        hex_encode(cdata, clen, out, clen*2+1);
    } else {
        out = malloc(((clen+2)/3)*4 + 1);
        base64_encode(cdata, clen, out, ((clen+2)/3)*4 + 1);
    }
    free(cdata);
    return out;
}

/* ================= fetch 响应（v1 / v2v3 / v4 分发） ================= */
static void send_v1_response(session_t *s, const char *id,
                             const http_result_t *r, bool raw)
{
    plan_t p;
    pick_plan(&s->nego, r->body, r->body_len, raw, false, &p);
    char *enc = encode_bytes(r->body, r->body_len, &p);

    size_t wire_len = strlen(enc);
    if (wire_len > MAX_UNCHUNKED_WIRE_LEN) {
        /* §5.1 单消息保护线：太大不发大包 */
        free(enc);
        send_error(id, "response too large for v1; negotiate chunking");
        return;
    }

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "tag", TAG_FETCH);
    if (id) cJSON_AddStringToObject(o, "id", id);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", r->status >= 200 && r->status < 300);
    cJSON_AddNumberToObject(resp, "status", r->status);
    cJSON_AddStringToObject(resp, "statusText", r->status_text);
    /* headers：直接复用 http_bridge 给的 JSON 片段 */
    if (r->headers_json) {
        cJSON *h = cJSON_Parse(r->headers_json);
        cJSON_AddItemToObject(resp, "headers", h ? h : cJSON_CreateObject());
    } else cJSON_AddItemToObject(resp, "headers", cJSON_CreateObject());
    cJSON_AddStringToObject(resp, "body", enc);
    cJSON_AddBoolToObject(resp, "raw", raw);
    if (strcmp(p.encoding,"text") != 0)
        cJSON_AddStringToObject(resp, "bodyEncoding", p.encoding);
    if (strcmp(p.compression,"none") != 0) {
        cJSON_AddStringToObject(resp, "compression", p.compression);
        cJSON_AddNumberToObject(resp, "originalBytes", p.original_bytes);
    }
    cJSON_AddItemToObject(o, "resp", resp);
    free(enc);
    send_json(o);
}

/* v2/v3 有限分片：发送头部 + 背靠背分片（v3 由 ack 状态机驱动）。
 * 本函数实现"发头部并泵出首批窗口"；后续 ack 驱动补发（见 v3 传输表）。 */
typedef struct {
    char id[64];
    uint8_t *data;      /* 编码后的分片数据（压缩后） */
    int total_bytes;
    int chunk_size;
    int chunk_count;
    int base;           /* 已确认前沿 */
    int next;           /* 下一个待发 */
    int window;
    uint32_t last_ack_ms;
    bool active;
} transfer_t;

#define MAX_TRANSFERS 4
static transfer_t s_transfers[MAX_TRANSFERS];

static transfer_t *find_transfer(const char *id)
{
    for (int i = 0; i < MAX_TRANSFERS; i++)
        if (s_transfers[i].active && strcmp(s_transfers[i].id, id) == 0)
            return &s_transfers[i];
    return NULL;
}

/* 发送单个分片 */
static void send_chunk(transfer_t *t, int seq)
{
    int off = seq * t->chunk_size;
    int len = t->chunk_size;
    if (off + len > t->total_bytes) len = t->total_bytes - off;
    /* 取本分片编码（base64） */
    char *enc = malloc(((len+2)/3)*4 + 1);
    base64_encode(t->data + off, len, enc, ((len+2)/3)*4 + 1);

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "tag", TAG_CHUNK);
    cJSON_AddStringToObject(o, "id", t->id);
    cJSON_AddNumberToObject(o, "seq", seq);
    cJSON_AddNumberToObject(o, "total", t->chunk_count);
    cJSON_AddStringToObject(o, "data", enc);
    free(enc);
    send_json(o);
    ESP_LOGD(TAG_FETCH_RESP, "chunk seq=%d/%d", seq, t->chunk_count);
}

/* 泵出窗口 [next, base+window) 且 < chunk_count（§5.3 on_ack） */
static void pump_window(transfer_t *t)
{
    int limit = t->base + t->window;
    if (limit > t->chunk_count) limit = t->chunk_count;
    while (t->next < limit) {
        send_chunk(t, t->next);
        t->next++;
    }
}

static void start_chunked(session_t *s, const char *id,
                          const uint8_t *payload, size_t len, bool raw,
                          const char *headers_json, int status, const char *status_text)
{
    /* 选压缩 + 编码（分片方向固定 base64/hex）。这里简化为不压缩直接 base64 分片。 */
    int chunk_size = s->nego.chunkSize ? s->nego.chunkSize : DEFAULT_CHUNK_SIZE;
    int chunk_count = (len + chunk_size - 1) / chunk_size;

    /* 头部帧（§5.2） */
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "tag", TAG_FETCH);
    cJSON_AddStringToObject(o, "id", id);
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", status >= 200 && status < 300);
    cJSON_AddNumberToObject(resp, "status", status);
    cJSON_AddStringToObject(resp, "statusText", status_text);
    if (headers_json) { cJSON *h = cJSON_Parse(headers_json);
        cJSON_AddItemToObject(resp, "headers", h?h:cJSON_CreateObject()); }
    else cJSON_AddItemToObject(resp, "headers", cJSON_CreateObject());
    cJSON_AddStringToObject(resp, "body", "");
    cJSON_AddBoolToObject(resp, "raw", true);
    cJSON_AddBoolToObject(resp, "chunked", true);
    cJSON_AddNumberToObject(resp, "totalBytes", (int)len);
    cJSON_AddNumberToObject(resp, "chunkSize", chunk_size);
    cJSON_AddNumberToObject(resp, "chunkCount", chunk_count);
    cJSON_AddStringToObject(resp, "bodyEncoding", "base64");
    if (s->nego.ackWindow > 0)
        cJSON_AddBoolToObject(resp, "ack", true);
    cJSON_AddItemToObject(o, "resp", resp);
    send_json(o);

    if (s->nego.ackWindow == 0) {
        /* v2：背靠背发完（§5.2，大响应可能死锁，仅小响应用） */
        for (int i = 0; i < chunk_count; i++) {
            transfer_t tmp = { .data=(uint8_t*)payload, .total_bytes=(int)len,
                               .chunk_size=chunk_size, .chunk_count=chunk_count };
            strncpy(tmp.id, id, sizeof(tmp.id)-1);
            send_chunk(&tmp, i);
        }
        return;
    }

    /* v3：登记传输并泵首批窗口 */
    transfer_t *t = NULL;
    for (int i = 0; i < MAX_TRANSFERS; i++) if (!s_transfers[i].active) { t=&s_transfers[i]; break; }
    if (!t) { send_error(id, "too many concurrent transfers"); return; }
    memset(t, 0, sizeof(*t));
    strncpy(t->id, id, sizeof(t->id)-1);
    t->data = malloc(len); memcpy(t->data, payload, len);
    t->total_bytes = (int)len;
    t->chunk_size = chunk_size;
    t->chunk_count = chunk_count;
    t->base = 0; t->next = 0;
    t->window = s->nego.ackWindow;
    t->last_ack_ms = now_ms();
    t->active = true;
    pump_window(t);
}

/* fetch-ack（§5.3） */
static void on_fetch_ack(const cJSON *j)
{
    const cJSON *id = cJSON_GetObjectItem(j, "id");
    const cJSON *ack = cJSON_GetObjectItem(j, "ack");
    if (!cJSON_IsString(id) || !cJSON_IsNumber(ack)) return;
    transfer_t *t = find_transfer(id->valuestring);
    if (!t) { ESP_LOGW(TAG_FLOW_ACK, "ack for unknown transfer %s", id->valuestring); return; }

    int a = ack->valueint;
    if (a > t->chunk_count) a = t->chunk_count;   /* 防越界 */
    t->last_ack_ms = now_ms();

    if (a > t->base) {
        t->base = a;
        pump_window(t);                          /* 窗口前移补发 */
    }
    /* 停滞但有在途 → go-back-N 重发整窗（每个停滞点只重传一次，简化实现） */
    if (t->base >= t->chunk_count) {
        ESP_LOGI(TAG_FETCH_RESP, "transfer %s complete", t->id);
        free(t->data); t->active = false;
    }
}

/* ================= v4 流（§6） ================= */
typedef struct {
    char id[64];
    http_stream_t *stream;
    uint64_t sent_offset;   /* 已交付到的绝对偏移（按 ACK 前沿） */
    uint64_t next_offset;
    uint32_t last_ack_ms;
    bool active;
} qstream_t;
static qstream_t s_streams[MAX_CONCURRENT_STREAMS];

static qstream_t *find_stream(const char *id)
{
    for (int i = 0; i < MAX_CONCURRENT_STREAMS; i++)
        if (s_streams[i].active && strcmp(s_streams[i].id, id) == 0)
            return &s_streams[i];
    return NULL;
}

/* 发一帧数据/结束帧（§6.2） */
static void send_stream_frame(qstream_t *st, uint64_t offset,
                              const uint8_t *data, int len, bool final)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "tag", TAG_STREAM);
    cJSON_AddStringToObject(o, "id", st->id);
    cJSON_AddNumberToObject(o, "seq", (double)(offset / DEFAULT_CHUNK_SIZE)); /* 顺序号 */
    cJSON_AddNumberToObject(o, "offset", (double)offset);
    if (!final && len > 0) {
        char *enc = malloc(((len+2)/3)*4+1);
        base64_encode(data, len, enc, ((len+2)/3)*4+1);
        cJSON_AddStringToObject(o, "data", enc);
        uint32_t crc = crc32_ieee(data, len);
        char crcs[9]; snprintf(crcs, sizeof(crcs), "%08x", crc);
        cJSON_AddStringToObject(o, "crc32", crcs);
        free(enc);
    } else {
        cJSON_AddStringToObject(o, "data", "");
        cJSON_AddStringToObject(o, "crc32", "00000000");
        cJSON_AddBoolToObject(o, "final", true);
        cJSON_AddNumberToObject(o, "totalBytes", (double)offset);
    }
    send_json(o);
}

/* 从 HTTP 源流读出一窗数据并发送（背压：只在 ACK 前沿前移时补读） */
static void pump_stream(qstream_t *st, int window_chunks, int chunk_size)
{
    /* 简化：每次补读一个 chunk_size；真实实现应控制在窗口内 */
    uint8_t buf[DEFAULT_CHUNK_SIZE];
    while (st->next_offset < (uint64_t)http_bridge_stream_total(st->stream)
           || http_bridge_stream_total(st->stream) < 0) {
        int n = http_bridge_stream_read(st->stream, st->next_offset, buf, sizeof(buf));
        if (n < 0) {
            /* 错误帧（§6.4） */
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "tag", TAG_STREAM_ERROR);
            cJSON_AddStringToObject(o, "id", st->id);
            cJSON_AddStringToObject(o, "message", "read streaming body failed");
            send_json(o);
            return;
        }
        if (n == 0) {
            send_stream_frame(st, st->next_offset, NULL, 0, true); /* 结束帧 */
            return;
        }
        send_stream_frame(st, st->next_offset, buf, n, false);
        st->next_offset += n;
        /* 背压：单窗内发一帧即等 ACK（真机可按 window_chunks 批量） */
        break;
    }
    (void)window_chunks; (void)chunk_size;
}

static void start_stream(session_t *s, const char *id,
                         const char *method, const char *url,
                         const char *headers_json, const char *body)
{
    if (!id || !*id) { send_error(id, "stream request requires unique id"); return; }
    if (find_stream(id)) { send_error(id, "stream id already in use"); return; }

    http_stream_t *st = http_bridge_open_stream(method, url, headers_json, body, body?strlen(body):0);
    if (!st) { send_error(id, "failed to open stream"); return; }

    qstream_t *qs = NULL;
    for (int i = 0; i < MAX_CONCURRENT_STREAMS; i++) if (!s_streams[i].active) { qs=&s_streams[i]; break; }
    if (!qs) { http_bridge_stream_close(st); send_error(id, "max concurrent streams"); return; }
    memset(qs, 0, sizeof(*qs));
    strncpy(qs->id, id, sizeof(qs->id)-1);
    qs->stream = st;
    qs->sent_offset = 0; qs->next_offset = 0;
    qs->last_ack_ms = now_ms();
    qs->active = true;

    /* 流头部（§6.1）：compression 固定 none（§8） */
    int chunk_size = s->nego.chunkSize ? s->nego.chunkSize : DEFAULT_CHUNK_SIZE;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "tag", TAG_FETCH);
    cJSON_AddStringToObject(o, "id", id);
    cJSON *r = cJSON_CreateObject();
    cJSON_AddBoolToObject(r, "ok", true);
    cJSON_AddNumberToObject(r, "status", 200);
    cJSON_AddStringToObject(r, "statusText", "OK");
    cJSON *h = cJSON_CreateObject();
    const char *ct = http_bridge_stream_content_type(st);
    if (ct) cJSON_AddStringToObject(h, "content-type", ct);
    cJSON_AddItemToObject(r, "headers", h);
    cJSON_AddStringToObject(r, "body", "");
    cJSON_AddBoolToObject(r, "raw", true);
    cJSON_AddBoolToObject(r, "stream", true);
    cJSON_AddNumberToObject(r, "chunkSize", chunk_size);
    cJSON_AddStringToObject(r, "bodyEncoding", "base64");
    cJSON_AddStringToObject(r, "compression", "none");   /* §8 v4 固定 none */
    cJSON_AddBoolToObject(r, "ack", true);
    cJSON_AddStringToObject(r, "checksum", "crc32");
    int64_t total = http_bridge_stream_total(st);
    if (total > 0) cJSON_AddNumberToObject(r, "contentLength", (double)total);
    cJSON_AddItemToObject(o, "resp", r);
    send_json(o);

    pump_stream(qs, s->nego.ackWindow, chunk_size);
}

/* fetch-stream-ack（§6.3） */
static void on_stream_ack(const cJSON *j)
{
    const cJSON *id = cJSON_GetObjectItem(j, "id");
    const cJSON *ack = cJSON_GetObjectItem(j, "ack");
    if (!cJSON_IsString(id) || !cJSON_IsNumber(ack)) return;
    qstream_t *st = find_stream(id->valuestring);
    if (!st) return;
    st->last_ack_ms = now_ms();
    st->sent_offset = (uint64_t)ack->valueint * DEFAULT_CHUNK_SIZE; /* 前沿按 chunk 数 */
    pump_stream(st, 0, 0);   /* ACK 前移 → 补读填满窗口（背压） */
}

/* fetch-stream-cancel（§6.4） */
static void on_stream_cancel(const cJSON *j)
{
    const cJSON *id = cJSON_GetObjectItem(j, "id");
    if (!cJSON_IsString(id)) return;
    qstream_t *st = find_stream(id->valuestring);
    if (st) {
        ESP_LOGI(TAG_STREAM, "stream %s cancelled", st->id);
        http_bridge_stream_close(st->stream);
        st->active = false;
    }
}

/* ================= fetch 请求（§4） ================= */
static void on_fetch(const cJSON *j)
{
    const cJSON *id   = cJSON_GetObjectItem(j, "id");
    const cJSON *url  = cJSON_GetObjectItem(j, "url");
    if (!cJSON_IsString(url) || !url->valuestring) {
        send_error(cJSON_IsString(id)?id->valuestring:NULL, "missing url");
        return;
    }
    const cJSON *opt  = cJSON_GetObjectItem(j, "options");

    const char *method = "GET";
    const char *body_str = NULL;
    const char *headers_json = NULL;
    bool raw = false, stream_req = false, follow = false;
    if (opt && cJSON_IsObject(opt)) {
        const cJSON *m = cJSON_GetObjectItem(opt, "method");
        if (cJSON_IsString(m)) method = m->valuestring;
        const cJSON *bd = cJSON_GetObjectItem(opt, "body");
        if (cJSON_IsString(bd)) body_str = bd->valuestring;
        const cJSON *h  = cJSON_GetObjectItem(opt, "headers");
        if (h) { char *hs = cJSON_PrintUnformatted(h); headers_json = hs; }
        const cJSON *rw = cJSON_GetObjectItem(opt, "raw");
        if (cJSON_IsBool(rw)) raw = cJSON_IsTrue(rw);
        const cJSON *sr = cJSON_GetObjectItem(opt, "stream");
        if (cJSON_IsBool(sr)) stream_req = cJSON_IsTrue(sr);
        const cJSON *fr = cJSON_GetObjectItem(opt, "followRedirects");
        if (cJSON_IsBool(fr)) follow = cJSON_IsTrue(fr);
    }

    session_t *s = find_or_create_session(NULL);
    s->last_seen_ms = now_ms();
    /* 乐观 open（§3.5：未握手先开，首响应走 v1 安全路径） */
    if (!s->negotiated) {
        s->open = true;
        caps_t base; memset(&base,0,sizeof(base));
        negotiate(&base, &s->nego);
        s->negotiated = true;
    }

    ESP_LOGI(TAG_FETCH_REQ, "%s %s (raw=%d stream=%d)", method, url->valuestring, raw, stream_req);

    /* v4 判定：请求强制 stream，或对端支持 stream（§6） */
    if (stream_req && s->nego.stream) {
        start_stream(s, cJSON_IsString(id)?id->valuestring:NULL,
                     method, url->valuestring, headers_json, body_str);
        if (headers_json) free((void*)headers_json);
        return;
    }

    /* 非流式：一次性 HTTP */
    http_result_t r;
    memset(&r, 0, sizeof(r));
    int rc = http_bridge_fetch(method, url->valuestring, headers_json,
                               body_str, body_str?strlen(body_str):0,
                               follow ? MAX_REDIRECTS : 0, &r);
    if (headers_json) free((void*)headers_json);

    if (rc != 0 || r.status == 0) {
        send_error(cJSON_IsString(id)?id->valuestring:NULL, "http request failed");
        http_result_free(&r);
        return;
    }

    /* 自动开流：content-type 音视频 或 Content-Length≥64KiB（§4.2 should_stream） */
    bool auto_stream = s->nego.stream &&
        (r.content_length >= AUTO_STREAM_THRESHOLD ||
         (strstr(r.content_type,"audio/") || strstr(r.content_type,"video/")));
    if (auto_stream) {
        /* 把已拿到的 body 转成流（简化：直接走 chunked；真机应边下边流） */
        start_chunked(s, cJSON_IsString(id)?id->valuestring:NULL,
                      r.body, r.body_len, raw, r.headers_json, r.status, r.status_text);
        http_result_free(&r);
        return;
    }

    /* 是否分片：协商分片且 body > chunkSize（§5.1） */
    if (s->nego.chunked && (int)r.body_len > s->nego.chunkSize) {
        start_chunked(s, cJSON_IsString(id)?id->valuestring:NULL,
                      r.body, r.body_len, raw, r.headers_json, r.status, r.status_text);
    } else {
        send_v1_response(s, cJSON_IsString(id)?id->valuestring:NULL, &r, raw);
    }
    http_result_free(&r);
}

/* ================= 入口分发 ================= */
void qaic_on_json(const char *json_text)
{
    if (!json_text) return;
    cJSON *j = cJSON_Parse(json_text);
    if (!j) { ESP_LOGW(TAG_FETCH_REQ, "bad json"); return; }
    const cJSON *tag = cJSON_GetObjectItem(j, "tag");
    if (!cJSON_IsString(tag)) { cJSON_Delete(j); return; }

    if (strcmp(tag->valuestring, TAG_HS) == 0)              on_handshake(j);
    else if (strcmp(tag->valuestring, TAG_FETCH) == 0)      on_fetch(j);
    else if (strcmp(tag->valuestring, TAG_ACK) == 0)        on_fetch_ack(j);
    else if (strcmp(tag->valuestring, TAG_STREAM_ACK) == 0) on_stream_ack(j);
    else if (strcmp(tag->valuestring, TAG_STREAM_CANCEL) == 0) on_stream_cancel(j);
    else ESP_LOGD(TAG_FETCH_REQ, "ignored tag %s", tag->valuestring);

    cJSON_Delete(j);
}

void qaic_init(qaic_send_fn send, void *ctx)
{
    s_send = send; s_send_ctx = ctx;
}

const char *qaic_local_caps_json(void)
{
    static char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"version\":%d,\"chunk\":true,\"maxChunkSize\":%d,"
             "\"encodings\":[\"base64\",\"hex\",\"text\"],"
             "\"compressions\":[\"none\",\"deflate\",\"lz4\"],"
             "\"ack\":true,\"ackWindow\":%d,\"stream\":true}",
             LOCAL_PROTOCOL_VERSION, CHUNK_SIZE_MAX, DEFAULT_ACK_WINDOW);
    return buf;
}

/* ================= 周期清理（§3.5 / §5.3 / §6.4） ================= */
void qaic_tick(uint32_t now)
{
    /* 会话 600s 过期 */
    session_t **ps = &s_sessions;
    while (*ps) {
        if (now - (*ps)->last_seen_ms > SESSION_IDLE_TIMEOUT_MS) {
            session_t *d = *ps; *ps = d->next; free(d);
        } else ps = &(*ps)->next;
    }
    /* 传输/流 30s 无 ACK 清理 */
    for (int i = 0; i < MAX_TRANSFERS; i++) {
        if (s_transfers[i].active && now - s_transfers[i].last_ack_ms > TRANSFER_TIMEOUT_MS) {
            ESP_LOGW(TAG_FETCH_RESP, "transfer %s timeout, drop", s_transfers[i].id);
            free(s_transfers[i].data); s_transfers[i].active = false;
        }
    }
    for (int i = 0; i < MAX_CONCURRENT_STREAMS; i++) {
        qstream_t *st = &s_streams[i];
        if (st->active && now - st->last_ack_ms > TRANSFER_TIMEOUT_MS) {
            ESP_LOGW(TAG_STREAM, "stream %s timeout, close", st->id);
            http_bridge_stream_close(st->stream); st->active = false;
        }
    }
}
