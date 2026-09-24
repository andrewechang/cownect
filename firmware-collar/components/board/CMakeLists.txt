idf_component_register(
    SRCS "board_mode.cpp"
         "board_power.cpp"
         "board_identity.cpp"
         "board_i2c.cpp"
         "board_adc.cpp"
         "board_self_test.cpp"
    INCLUDE_DIRS "include"
    REQUIRES cn_config esp_driver_gpio esp_driver_i2c esp_driver_uart esp_adc esp_timer
    PRIV_REQUIRES esp_psram heap log freertos
)
