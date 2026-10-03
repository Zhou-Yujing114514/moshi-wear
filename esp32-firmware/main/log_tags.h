/*
 * log_tags.h — 统一日志标记（供真机排错）
 * 用法：ESP_LOGI(TAG_BLE_CONN, "...")；每个阶段一个固定 TAG。
 */
#pragma once

/* BLE 连接 / 扫描 / MTU 协商 */
#define TAG_BLE_CONN        "BLE_CONN"
/* L1 帧发送（0xA5A5 封装） */
#define TAG_L1_SEND         "L1_SEND"
/* L1 帧接收 / 校验 */
#define TAG_L1_RECV         "L1_RECV"
/* L2 通道层（channel/opcode） */
#define TAG_L2_RECV         "L2_RECV"
#define TAG_L2_SEND         "L2_SEND"
/* 认证握手四步 */
#define TAG_HS_STEP         "HS_STEP"
/* 加密 / 解密 */
#define TAG_CRYPTO          "CRYPTO"
/* SAR 流控（ACK/Nak/重传） */
#define TAG_FLOW_ACK        "FLOW_ACK"
/* QAIC 应用层：fetch 请求 */
#define TAG_FETCH_REQ       "FETCH_REQ"
/* QAIC 应用层：响应/分片 */
#define TAG_FETCH_RESP      "FETCH_RESP"
/* 流式 v4 */
#define TAG_STREAM          "STREAM"
/* HTTP 桥 */
#define TAG_HTTP            "HTTP_BRIDGE"
/* WiFi */
#define TAG_WIFI            "WIFI"
/* 会话/握手 __hs__ */
#define TAG_QAIC_HS         "QAIC_HS"
/* protobuf */
#define TAG_PROTO           "PROTO"
