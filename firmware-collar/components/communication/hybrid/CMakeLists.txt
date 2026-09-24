idf_component_register(
    SRCS "prototype_telemetry_encoder.cpp"
         "hybrid_communication_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES features lora_radio wifi_upload data capture_stream cn_config
    PRIV_REQUIRES esp_timer log
)
