/*
 * codec.h — 编码 / 压缩 / CRC32（笔记 §7/§8/§9）
 *
 *  - base64：RFC4648，带 '=' 填充
 *  - hex   ：小写
 *  - text  ：直接 UTF-8（不编解码，仅透传）
 *  - deflate：raw（RFC1951，无 zlib 头）解压
 *  - lz4   ：裸 block 解压（需 originalBytes）
 *  - crc32 ：IEEE（init/xorout 0xFFFFFFFF，poly 反射 0xEDB88320）
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- base64（RFC4648 带 =）---- */
/* 返回输出串长度（含填充）；out_cap 不足返回 -1 */
int base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap);
int base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap);

/* ---- hex 小写 ---- */
int hex_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap);
int hex_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap);

/* ---- 是否合法 UTF-8（text 编码判定用） ---- */
bool is_valid_utf8(const uint8_t *data, size_t len);

/* ---- CRC32 IEEE；测试向量 crc32("123456789")=0xCBF43926 ---- */
uint32_t crc32_ieee(const uint8_t *data, size_t len);

/* ---- raw deflate (RFC1951) 解压；out_cap 为目标缓冲大小。返回解压长度，<0 错 ---- */
int deflate_decompress(const uint8_t *in, size_t in_len,
                      uint8_t *out, size_t out_cap);

/* ---- LZ4 裸 block 解压；original_bytes 为解压后目标长度。返回解压长度，<0 错 ---- */
int lz4_block_decompress(const uint8_t *in, size_t in_len,
                         uint8_t *out, size_t original_bytes);
