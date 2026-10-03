# main 组件 CMake
idf_component_register(
    SRCS
        "main.c"
        "ble_client.c"
        "l1_frame.c"
        "l2_frame.c"
        "mi_crypto.c"
        "proto_pack.c"
        "mi_handshake.c"
        "sar.c"
        "codec.c"
        "qaic.c"
        "http_bridge.c"
        "bridge.c"
    INCLUDE_DIRS "."
    REQUIRES
        bt
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_http_client
        mbedtls
        json
        driver
)
