idf_component_register(
    SRCS "analog_adc_engine.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board cn_config esp_adc
    PRIV_REQUIRES esp_timer log
)
