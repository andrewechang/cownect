# Ra-01SH / SX1262 radio (Material 12).
# The Semtech driver is vendored (pinned, not downloaded at build time) and kept PRIVATE:
# application code only sees the CowNect ILoraRadio / Sx1262Adapter API.
idf_component_register(
    SRCS "vendor/sx126x_driver/src/sx126x.c"
         "vendor/sx126x_driver/src/sx126x_driver_version.c"
         "sx1262_hal_espidf.cpp"
         "sx1262_adapter.cpp"
         "lora_config.cpp"
         "lora_test_packet.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "vendor/sx126x_driver/src" "."
    REQUIRES board cn_config esp_driver_spi esp_driver_gpio
    PRIV_REQUIRES esp_timer log heap esp_rom
)
