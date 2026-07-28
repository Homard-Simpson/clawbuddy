idf_component_register(
    SRCS "main.cc" "network_portal.cc"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_wifi esp_event esp_http_client esp_http_server nvs_flash esp_lcd esp_timer driver json mbedtls
)
