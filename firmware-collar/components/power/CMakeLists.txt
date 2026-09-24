idf_component_register(
    SRCS "power_manager.cpp"
         "sleep_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board esp_hw_support esp_system
    PRIV_REQUIRES log freertos
)
