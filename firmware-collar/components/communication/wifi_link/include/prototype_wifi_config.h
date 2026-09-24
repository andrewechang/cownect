#pragma once

#include <cstddef>
#include <cstdint>
#include "esp_err.h"

namespace cownect::wifi {

// Material 14 Rev B section 40. Real values are supplied through menuconfig by the user;
// nothing is hardcoded. The password is never logged.
struct PrototypeWifiConfig {
    const char* ssid;
    const char* password;

    const char* jetson_ip;
    uint16_t jetson_port;

    uint32_t wifi_connect_timeout_ms;
    uint32_t tcp_connect_timeout_ms;
    uint32_t socket_write_timeout_ms;

    size_t upload_chunk_bytes;
};

// Full configuration (Wi-Fi + Jetson TCP). COWNECT_ERR_NOT_CONFIGURED lists what is missing.
esp_err_t prototype_wifi_config_load(PrototypeWifiConfig& out, const char** missing);
// Wi-Fi association only (SSID, password, connect timeout) for the connect test.
esp_err_t prototype_wifi_config_load_wifi_only(PrototypeWifiConfig& out, const char** missing);

}  // namespace cownect::wifi
