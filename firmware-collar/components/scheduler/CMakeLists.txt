idf_component_register(
    SRCS "cycle_scheduler.cpp"
    INCLUDE_DIRS "include"
    REQUIRES sensor_manager features comm_manager power data board cn_config lora_radio
    PRIV_REQUIRES esp_timer log
)
