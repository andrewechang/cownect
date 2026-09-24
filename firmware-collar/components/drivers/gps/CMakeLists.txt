idf_component_register(
    SRCS "nmea_framer.cpp"
         "nmea_parser.cpp"
         "gps_fix_builder.cpp"
         "atgm336h_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board cn_config esp_driver_uart
    PRIV_REQUIRES esp_timer log
)
