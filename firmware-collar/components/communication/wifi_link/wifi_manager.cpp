#include "wifi_manager.h"

#include <cstring>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

namespace cownect::wifi {
namespace {
constexpr const char* TAG = "WIFI";
constexpr EventBits_t kGotIp = BIT0;
}  // namespace

WifiManager& wifi_manager()
{
    static WifiManager instance;
    return instance;
}

void WifiManager::on_event(void* arg, const char* base, int32_t id, void* data)
{
    auto* self = static_cast<WifiManager*>(arg);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(self->events_, kGotIp);
        if (self->state_ == WifiState::CONNECTING) {
            esp_wifi_connect();  // retried only inside the bounded connect() window
        } else if (self->state_ == WifiState::CONNECTED) {
            self->state_ = WifiState::DISCONNECTED;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* ev = static_cast<ip_event_got_ip_t*>(data);
        esp_ip4addr_ntoa(&ev->ip_info.ip, self->ip_, sizeof(self->ip_));
        xEventGroupSetBits(self->events_, kGotIp);
    }
}

esp_err_t WifiManager::init()
{
    if (state_ != WifiState::UNINITIALIZED) {
        return ESP_OK;
    }
    esp_err_t err = nvs_flash_init();  // required by the Wi-Fi driver
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);  // credentials are not persisted by the driver
    events_ = xEventGroupCreate();
    if (events_ == nullptr) return ESP_ERR_NO_MEM;
    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::on_event, this, nullptr);
    if (err == ESP_OK) {
        err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::on_event, this, nullptr);
    }
    if (err != ESP_OK) return err;
    state_ = WifiState::DISCONNECTED;
    return ESP_OK;
}

esp_err_t WifiManager::connect(const PrototypeWifiConfig& c)
{
    esp_err_t err = init();
    if (err != ESP_OK) {
        state_ = WifiState::ERROR;
        return err;
    }
    wifi_config_t wc = {};
    std::strncpy(reinterpret_cast<char*>(wc.sta.ssid), c.ssid, sizeof(wc.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(wc.sta.password), c.password, sizeof(wc.sta.password) - 1);
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_set_config(WIFI_IF_STA, &wc);
    if (err == ESP_OK && !started_) {
        err = esp_wifi_start();
        started_ = err == ESP_OK;
    }
    if (err != ESP_OK) {
        state_ = WifiState::ERROR;
        return err;
    }
    ESP_LOGI(TAG, "connecting ssid=\"%s\"", c.ssid);  // password intentionally not logged
    xEventGroupClearBits(events_, kGotIp);
    state_ = WifiState::CONNECTING;
    const int64_t t0 = esp_timer_get_time();
    esp_wifi_connect();
    const EventBits_t bits =
        xEventGroupWaitBits(events_, kGotIp, pdFALSE, pdTRUE, pdMS_TO_TICKS(c.wifi_connect_timeout_ms));
    last_connect_ms_ = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
    if ((bits & kGotIp) == 0) {
        state_ = WifiState::DISCONNECTED;
        ESP_LOGW(TAG, "CONNECT_TIMEOUT after %u ms", static_cast<unsigned>(last_connect_ms_));
        esp_wifi_disconnect();
        return ESP_ERR_TIMEOUT;
    }
    state_ = WifiState::CONNECTED;
    ESP_LOGI(TAG, "connected connect_ms=%u ip=%s", static_cast<unsigned>(last_connect_ms_), ip_);
    return ESP_OK;
}

esp_err_t WifiManager::disconnect()
{
    if (state_ == WifiState::UNINITIALIZED) {
        return ESP_OK;
    }
    state_ = WifiState::DISCONNECTED;
    return started_ ? esp_wifi_disconnect() : ESP_OK;
}

esp_err_t WifiManager::stop()
{
    if (!started_) {
        return ESP_OK;
    }
    state_ = WifiState::DISCONNECTED;
    esp_err_t err = esp_wifi_stop();
    started_ = false;
    ESP_LOGI(TAG, "stopped");
    return err;
}

}  // namespace cownect::wifi
