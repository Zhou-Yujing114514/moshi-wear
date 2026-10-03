/*
 * codec.c — 编解码实现（笔记 §7/§8/§9）
 */
#include "codec.h"
#include <string.h>

/* ================= base64 RFC4648 ================= */
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_encode(const uint8_t *in, size_t n, char *out, size_t cap)
{
    size_t need = ((n + 2) / 3) * 4;
    if (cap < need + 1) return -1;
    size_t i = 0, o = 0;
    while (i + 3 <= n) {
        uint32_t v = (in[i] << 16) | (in[i+1] << 8) | in[i+2];
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = B64[(v >> 6) & 63];
        out[o++] = B64[v & 63];
        i += 3;
    }
    if (i < n) {
        uint32_t v = in[i] << 16;
        if (i + 1 < n) v |= in[i+1] << 8;
        out[o++] = B64[(v >> 18) & 63];
        out[o++] = B64[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? B64[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    out[o] = 0;
    return (int)o;
}

static int b64val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int base64_decode(const char *in, size_t n, uint8_t *out, size_t cap)
{
    size_t o = 0;
    uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        if (c == '=') break;
        int v = b64val(c);
        if (v < 0) continue;
        acc = (acc << 6) | v; bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (o >= cap) return -1;
            out[o++] = (acc >> bits) & 0xFF;
        }
    }
    return (int)o;
}

/* ================= hex 小写 ================= */
static const char HEX[] = "0123456789abcdef";
int hex_encode(const uint8_t *in, size_t n, char *out, size_t cap)
{
    if (cap < n * 2 + 1) return -1;
    for (size_t i = 0; i < n; i++) {
        out[2*i]   = HEX[in[i] >> 4];
        out[2*i+1] = HEX[in[i] & 0xF];
    }
    out[2*n] = 0;
    return (int)(n * 2);
}

static int hv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
int hex_decode(const char *in, size_t n, uint8_t *out, size_t cap)
{
    if (n % 2) return -1;
    size_t bytes = n / 2;
    if (cap < bytes) return -1;
    for (size_t i = 0; i < bytes; i++) {
        int hi = hv(in[2*i]), lo = hv(in[2*i+1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return (int)bytes;
}

/* ================= UTF-8 校验 ================= */
bool is_valid_utf8(const uint8_t *d, size_t n)
{
    size_t i = 0;
    while (i < n) {
        if (d[i] < 0x80) { i++; continue; }
        int need; uint32_t cp;
        if ((d[i] & 0xE0) == 0xC0) { need = 1; cp = d[i] & 0x1F; }
        else if ((d[i] & 0xF0) == 0xE0) { need = 2; cp = d[i] & 0x0F; }
        else if ((d[i] & 0xF8) == 0xF0) { need = 3; cp = d[i] & 0x07; }
        else return false;
        if (i + need >= n) return false;
        for (int k = 1; k <= need; k++) {
            if ((d[i+k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (d[i+k] & 0x3F);
        }
        if (cp < (uint32_t)(1 << (5*need+1))) return false; /* overlong */
        i += need + 1;
    }
    return true;
}

/* ================= CRC32 IEEE ================= */
uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        built = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

/* ================= raw deflate (RFC1951) =================
 * 用 zlib inflate，windowBits = -15 = 无 zlib 头的裸 deflate。 */
#include "zlib.h"
int deflate_decompress(const uint8_t *in, size_t in_len,
                      uint8_t *out, size_t out_cap)
{
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    /* -15：raw deflate，无 zlib/gzip 头 */
    if (inflateInit2(&strm, -15) != Z_OK) return -1;
    strm.next_in = (Bytef *)in;
    strm.avail_in = (uInt)in_len;
    strm.next_out = out;
    strm.avail_out = (uInt)out_cap;
    int rc = inflate(&strm, Z_FINISH);
    int produced = (int)strm.total_out;
    inflateEnd(&strm);
    if (rc != Z_STREAM_END && rc != Z_OK) return -1;
    return produced;
}

/* ================= LZ4 裸 block 解压 =================
 * 极简 LZ4 block 解码器（与 lz4_flex 默认块格式兼容）。
 * 格式：token byte = (literal_len << 4) | match_len；
 *  若 literal_len==15，后续字节累加直到 !=255；然后字面量；
 *  然后 2 字节 little-endian offset；match_len = token_nibble + 4，
 *  若 nibble==15 继续累加；从 offset 处拷贝。直到耗尽输入。 */
int lz4_block_decompress(const uint8_t *in, size_t in_len,
                        uint8_t *out, size_t original_bytes)
{
    size_t ip = 0, op = 0;
    while (ip < in_len && op < original_bytes) {
        uint8_t token = in[ip++];
        unsigned lit_len = token >> 4;
        if (lit_len == 15) {
            while (ip < in_len) {
                uint8_t b = in[ip++];
                lit_len += b;
                if (b != 255) break;
            }
        }
        if (op + lit_len > original_bytes) return -1;
        if (ip + lit_len > in_len) return -1;
        memcpy(out + op, in + ip, lit_len);
        ip += lit_len; op += lit_len;

        if (ip + 2 > in_len) break;
        uint16_t offset = in[ip] | (in[ip+1] << 8);
        ip += 2;
        if (offset == 0 || offset > op) return -1;
        unsigned match_len = (token & 0x0F) + 4;
        if ((token & 0x0F) == 15) {
            while (ip < in_len) {
                uint8_t b = in[ip++];
                match_len += b;
                if (b != 255) break;
            }
        }
        if (op + match_len > original_bytes) return -1;
        for (unsigned k = 0; k < match_len; k++)
            out[op + k] = out[op - offset + k];
        op += match_len;
    }
    if (op != original_bytes) return -1;
    return (int)op;
}
