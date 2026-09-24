idf_component_register(
    SRCS "accelerometer_decode.cpp"
         "lis2dw12_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board cn_config esp_driver_i2c
    PRIV_REQUIRES esp_timer log
)
