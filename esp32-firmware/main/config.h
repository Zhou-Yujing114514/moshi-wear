/*
 * config.h — 固件可配置项（authkey / WiFi / 目标设备）
 *
 * 重要：authkey 是唯一外部依赖。本文件不提供任何默认 authkey（绝不编造）。
 * 获取方式见 README「authkey 获取」章节。可通过 menuconfig 或运行时配置写入。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* ---- WiFi 配置（menuconfig 可覆盖）---- */
#ifndef BR_WIFI_SSID
#define BR_WIFI_SSID            "your-wifi-ssid"
#endif
#ifndef BR_WIFI_PASS
#define BR_WIFI_PASS            "your-wifi-password"
#endif

/*
 * 目标手环 BLE 地址（小端 MAC，6 字节）。
 * 若为全 0，则固件在扫描时选择第一个命中 0xFE95 服务的设备（开发期方便）。
 * 生产建议显式配置，避免连错设备。
 */
#ifndef BR_TARGET_ADDR
#define BR_TARGET_ADDR          {0x00,0x00,0x00,0x00,0x00,0x00}
#endif

/*
 * authkey：配对主密钥，16 字节。
 * 来源：用小米运动健康 App / AstroBox 完成首次配对后，从配对存储或抓包提取。
 * 此处默认全 0 = 未配置；未配置时握手必然失败（日志 HS_STEP 会报 AuthKey 错误）。
 * 建议长度 32 字符 hex 串（如 "a1b2c3...16字节"）。请勿在源码里硬编码真实密钥，
 * 应通过 menuconfig(CONFIG_BR_AUTHKEY_HEX) 或 NVS/配置分区写入。
 */
#ifndef BR_AUTHKEY_HEX
#define BR_AUTHKEY_HEX          "00000000000000000000000000000000"
#endif

/* ---- 协议开关 ---- */

/*
 * 数据面 CTR 的非标准怪癖：CTR 的 IV 直接复用 16B 密钥本身（key==IV）。
 * 这是为与手环固件互操作而照抄的非标准用法（笔记 B3.6）。
 * 抓包核实后若手环实际使用 enc_nonce/dec_nonce，可关闭此开关改用派生 nonce。
 */
#ifndef BR_CTR_IV_USE_KEY
#define BR_CTR_IV_USE_KEY       1
#endif

/* 设备类型 / 设备名（CompanionDevice protobuf，随握手第 3 步发送） */
#define BR_DEVICE_TYPE          0x01
#define BR_DEVICE_NAME          "ESP32Bridge"

/* HTTP 桥：单次 fetch 的最大响应缓冲（v1/v3 非流式），字节 */
#define BR_HTTP_MAX_BODY        (64 * 1024)
/* HTTP 桥：全局并发 v4 流上限（笔记 §6.4 = 8） */
#define BR_MAX_CONCURRENT_STREAMS 8

/* ---- 运行时可调缓存大小 ---- */
/* L1 重组缓冲上限 = MPS=64512（笔记 B2.2） */
#define BR_MPS                  64512
/* BLE 写：运行时取 MTU-3；此处仅为初值 */
#define BR_DEFAULT_MAX_WRITE    200
