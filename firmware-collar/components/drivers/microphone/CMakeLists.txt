idf_component_register(
    SRCS "microphone_capture_driver.cpp"
         "microphone_diagnostics.cpp"
    INCLUDE_DIRS "include"
    REQUIRES analog_adc board cn_config
    PRIV_REQUIRES esp_timer log
)
