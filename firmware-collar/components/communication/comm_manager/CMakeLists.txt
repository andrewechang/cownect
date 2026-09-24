idf_component_register(
    SRCS "communication_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES hybrid lora_full lora_radio wifi_upload features data capture_stream cn_config
    PRIV_REQUIRES esp_timer log
)
