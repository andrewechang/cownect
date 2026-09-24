#include "prototype_wifi_config.h"

#include "config_parse.h"
#include "cownect_config.h"
#include "cownect_err.h"
#include "sdkconfig.h"

namespace cownect::wifi {

esp_err_t prototype_wifi_config_load_wifi_only(PrototypeWifiConfig& c, const char** missing)
{
    c = {};
    c.ssid = CONFIG_COWNECT_WIFI_SSID;
    c.password = CONFIG_COWNECT_WIFI_PASSWORD;
    c.wifi_connect_timeout_ms = CONFIG_COWNECT_WIFI_CONNECT_TIMEOUT_MS;
    c.upload_chunk_bytes = config::UPLOAD_CHUNK_BYTES;
    const char* m = nullptr;
    if (!config::is_set(c.ssid)) m = "WIFI_SSID";
    else if (!config::is_set(c.password)) m = "WIFI_PASSWORD";
    else if (c.wifi_connect_timeout_ms == 0) m = "WIFI_CONNECT_TIMEOUT_MS";
    if (missing) *missing = m ? m : "";
    return m ? COWNECT_ERR_NOT_CONFIGURED : ESP_OK;
}

esp_err_t prototype_wifi_config_load(PrototypeWifiConfig& c, const char** missing)
{
    esp_err_t err = prototype_wifi_config_load_wifi_only(c, missing);
    if (err != ESP_OK) {
        return err;
    }
    c.jetson_ip = CONFIG_COWNECT_JETSON_IP;
    c.jetson_port = static_cast<uint16_t>(CONFIG_COWNECT_JETSON_PORT);
    c.tcp_connect_timeout_ms = CONFIG_COWNECT_TCP_CONNECT_TIMEOUT_MS;
    c.socket_write_timeout_ms = CONFIG_COWNECT_SOCKET_WRITE_TIMEOUT_MS;
    const char* m = nullptr;
    if (!config::is_set(c.jetson_ip)) m = "JETSON_IP";
    else if (c.jetson_port == 0) m = "JETSON_PORT";
    else if (c.tcp_connect_timeout_ms == 0) m = "TCP_CONNECT_TIMEOUT_MS";
    else if (c.socket_write_timeout_ms == 0) m = "SOCKET_WRITE_TIMEOUT_MS";
    if (missing) *missing = m ? m : "";
    return m ? COWNECT_ERR_NOT_CONFIGURED : ESP_OK;
}

}  // namespace cownect::wifi
