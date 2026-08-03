idf_component_register(
    SRCS "main.cc" "network_portal.cc" "auto_ota.cc" "runtime_operation.cc"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_wifi esp_event esp_http_client esp_http_server esp_hw_support esp_netif nvs_flash esp_lcd esp_timer driver json mbedtls
)
