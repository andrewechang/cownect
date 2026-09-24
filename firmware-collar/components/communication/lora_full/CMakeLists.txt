idf_component_register(
    SRCS "lora_full_protocol.cpp"
         "lora_full_fragmenter.cpp"
         "lora_full_transfer.cpp"
         "lora_full_config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES capture_stream lora_radio data cn_config
    PRIV_REQUIRES esp_timer log
)
