/*
 * qaic.h — QAIC 应用层：__hs__ 握手 + FetchBridge v1~v4（笔记 §3~§9）
 *
 * 入口：qaic_on_json() 收到从 Pb 通道解出的 JSON 文本字符串。
 * 出口：qaic_send_fn 把要发回手环的 JSON 字符串交出（再经 L2/L1/BLE）。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* 发送回调：把 JSON 文本发回手环（按会话 pkg） */
typedef int (*qaic_send_fn)(const char *json_text, void *ctx);

/* 初始化。send=下行发送回调。 */
void qaic_init(qaic_send_fn send, void *ctx);

/* 收到一段 JSON 文本时调用（已去 L2/L1/加密）。 */
void qaic_on_json(const char *json_text);

/* 周期调度（建议每 1s）：会话 600s 过期、传输/流 30s 超时清理。 */
void qaic_tick(uint32_t now_ms);

/* 取本端能力声明（用于 __hs__ count=1 回包）。返回紧凑 JSON 字符串（静态缓冲）。 */
const char *qaic_local_caps_json(void);
