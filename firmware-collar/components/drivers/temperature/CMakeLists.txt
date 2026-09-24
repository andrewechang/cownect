idf_component_register(
    SRCS "temperature_math.cpp"
         "slow_adc.cpp"
         "mcp9700_driver.cpp"
         "mf58_driver.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board cn_config esp_adc
    PRIV_REQUIRES log
)
