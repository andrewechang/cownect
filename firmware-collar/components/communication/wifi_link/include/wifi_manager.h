#pragma once

#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "prototype_wifi_config.h"

namespace cownect::wifi {

enum class WifiState { UNINITIALIZED, DISCONNECTED, CONNECTING, CONNECTED, ERROR };

// Material 14 section 6. No TCP logic here. Station mode, one configured AP, bounded connect.
class IWifiManager {
public:
    virtual ~IWifiManager() = default;
    virtual esp_err_t init() = 0;
    virtual esp_err_t connect(const PrototypeWifiConfig& config) = 0;
    virtual bool connected() const = 0;
    virtual esp_err_t disconnect() = 0;
    virtual esp_err_t stop() = 0;
};

class WifiManager final : public IWifiManager {
public:
    esp_err_t init() override;
    esp_err_t connect(const PrototypeWifiConfig& config) override;
    bool connected() const override { return state_ == WifiState::CONNECTED; }
    esp_err_t disconnect() override;
    esp_err_t stop() override;  // radio off; driver kept initialized for the next cycle

    WifiState state() const { return state_; }
    uint32_t last_connect_ms() const { return last_connect_ms_; }
    const char* ip_string() const { return ip_; }

private:
    static void on_event(void* arg, const char* base, int32_t id, void* data);

    WifiState state_ = WifiState::UNINITIALIZED;
    EventGroupHandle_t events_ = nullptr;
    bool started_ = false;
    uint32_t last_connect_ms_ = 0;
    char ip_[16] = {};
};

WifiManager& wifi_manager();

}  // namespace cownect::wifi
