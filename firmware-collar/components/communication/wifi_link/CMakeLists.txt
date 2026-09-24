idf_component_register(
    SRCS "wifi_manager.cpp"
         "prototype_wifi_config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES cn_config esp_wifi esp_netif esp_event
    PRIV_REQUIRES nvs_flash esp_timer log
)
