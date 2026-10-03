/*
 * main.c — 入口
 *
 * ESP32 插电自启 → WiFi 连网 → BLE 直连手环 9 Pro → 桥接 QAIC fetch 到互联网。
 * 全部由 bridge_start() 拉起；本文件仅做最薄启动。
 */
#include "bridge.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=== MiWear ESP32 Internet Bridge (Mi Band 9 Pro) ===");
    ESP_LOGI(TAG, "协议层：L1(0xA5A5/CRC16-ARC) -> L2(Pb/AES-CTR) -> QAIC(FetchBridge v4)");
    bridge_start();
    /* app_main 返回后 FreeRTOS 调度继续运行各任务 */
}
