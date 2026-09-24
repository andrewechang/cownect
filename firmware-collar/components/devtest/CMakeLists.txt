# Development-only staged tests (CLAUDE_INSTRUCTIONS section 20). Every test calls the real
# production drivers/managers; there is no second fake implementation.
idf_component_register(
    SRCS "devtest_common.cpp"
         "devtest_console.cpp"
         "devtest_rtc.cpp"
         "test_board.cpp"
         "test_accel.cpp"
         "test_temperature.cpp"
         "test_gps.cpp"
         "test_microphone.cpp"
         "test_capture.cpp"
         "test_scheduler.cpp"
         "test_telemetry.cpp"
         "test_lora.cpp"
         "test_lora_full.cpp"
         "test_wifi.cpp"
         "test_hybrid.cpp"
         "test_unit.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES scheduler
    PRIV_REQUIRES board cn_config data sensor_manager accelerometer gps temperature microphone analog_adc
                  features capture_stream lora_radio lora_full wifi_link tcp_client wifi_upload hybrid
                  comm_manager power console esp_timer esp_system esp_driver_gpio log
)
