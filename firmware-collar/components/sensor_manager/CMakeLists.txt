idf_component_register(
    SRCS "sensor_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES data accelerometer gps temperature microphone analog_adc board cn_config
    PRIV_REQUIRES esp_timer log
)
