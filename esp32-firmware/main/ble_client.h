/*
 * ble_client.h — NimBLE GATT 中心（笔记 §B1）
 *
 *  - 服务 0xFE95
 *  - 写特征 0x005f：Write Without Response（下行）
 *  - 通知特征 0x005e：Notify（上行，订阅）
 *  - 控制特征 0x0050：Read（订阅 notify 后读一次）
 *  - MTU 协商后 max_write_len = MTU - 3
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* 收到一条上行 BLE notify 数据时的回调（交给 sar_on_ble_rx） */
typedef void (*ble_rx_fn)(const uint8_t *data, size_t len, void *ctx);

/* 连接 / 握手完成事件回调 */
typedef void (*ble_event_fn)(bool connected, void *ctx);

/* 初始化 NimBLE 并开始扫描连接。 */
void ble_bridge_init(ble_rx_fn rx, void *rx_ctx,
                     ble_event_fn ev, void *ev_ctx,
                     const uint8_t target_addr[6]);

/* 写无响应（下行）。供 sar 调用；ctx 未用（与 sar_ble_write_fn 签名对齐）。 */
int ble_bridge_write(const uint8_t *data, size_t len, void *ctx);

/* 当前协商到的 max_write_len（MTU-3） */
uint16_t ble_bridge_max_write_len(void);

/* 是否已连接 */
bool ble_bridge_connected(void);
