idf_component_register(
    SRCS "wifi_capture_uploader.cpp"
    INCLUDE_DIRS "include"
    REQUIRES capture_stream wifi_link tcp_client data cn_config
    PRIV_REQUIRES esp_timer log heap
)
