idf_component_register(
    SRCS "capture_session.cpp"
         "rtc_state.cpp"
    INCLUDE_DIRS "include"
    REQUIRES accelerometer gps temperature microphone analog_adc cn_config
    PRIV_REQUIRES heap log esp_hw_support
)
