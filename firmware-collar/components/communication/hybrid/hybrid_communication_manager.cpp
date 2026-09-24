#include "hybrid_communication_manager.h"

#include "cownect_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "prototype_telemetry_encoder.h"
#include "sdkconfig.h"
#include "sx1262_adapter.h"

namespace cownect::hybrid {
namespace {
constexpr const char* TAG = "HYBRID";
}

HybridPrototypeConfig hybrid_config_from_menuconfig()
{
    HybridPrototypeConfig c = {};
#if CONFIG_COWNECT_HYBRID_ENABLE_LORA_TELEMETRY
    c.enable_lora_telemetry = true;
#endif
#if CONFIG_COWNECT_HYBRID_ENABLE_WIFI_UPLOAD
    c.enable_wifi_raw_upload = true;
#endif
    return c;
}

const char* hybrid_result_name(HybridResult r)
{
    switch (r) {
    case HybridResult::BOTH_OK:             return "BOTH_OK";
    case HybridResult::LORA_OK_WIFI_FAILED: return "LORA_OK_WIFI_FAILED";
    case HybridResult::LORA_FAILED_WIFI_OK: return "LORA_FAILED_WIFI_OK";
    case HybridResult::BOTH_FAILED:         return "BOTH_FAILED";
    }
    return "?";
}

esp_err_t HybridCommunicationManager::run(const data::CaptureSession& capture,
                                          const features::TelemetryRecord& telemetry, SwitchedRailRelease release_rail,
                                          void* release_ctx)
{
    const HybridPrototypeConfig cfg = hybrid_config_from_menuconfig();
    stats_ = {};
    stats_.capture_id = capture.id.capture_id;
    const int64_t t_start = esp_timer_get_time();
    ESP_LOGI(TAG, "capture=%u", static_cast<unsigned>(capture.id.capture_id));

    // 1. One compact LoRa telemetry packet (same device_id/capture_id as the capture).
    state_ = HybridState::SENDING_LORA;
    if (cfg.enable_lora_telemetry) {
        stats_.lora_attempted = true;
        const int64_t t0 = esp_timer_get_time();
        uint8_t pkt[TELEMETRY_PACKET_BYTES];
        size_t len = 0;
        esp_err_t err = PrototypeTelemetryEncoder().encode(telemetry, pkt, sizeof(pkt), len);
        if (err == ESP_OK) err = radio::lora_radio_prepare_from_config();
        uint32_t tx_ms = 0;
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "LoRa start bytes=%u", static_cast<unsigned>(len));
            err = radio::lora_radio().send_blocking(pkt, len, tx_ms);  // bounded (Material 12)
        }
        stats_.lora_error = err;
        stats_.lora_success = err == ESP_OK;
        stats_.lora_bytes = stats_.lora_success ? static_cast<uint32_t>(len) : 0;
        stats_.lora_time_ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
        if (stats_.lora_success) {
            ESP_LOGI(TAG, "LoRa OK time_ms=%u", static_cast<unsigned>(stats_.lora_time_ms));
        } else {
            ESP_LOGW(TAG, "LoRa FAILED (%s) - continuing to Wi-Fi", cownect_err_name(err));
        }
    }
    // LoRa is idle now; the scheduler may release SWITCHED_3V3 before the Wi-Fi upload.
    if (release_rail != nullptr) {
        release_rail(release_ctx);
    }

    // 2. Raw capture over Wi-Fi TCP (Material 14 Rev B, unchanged).
    state_ = HybridState::UPLOADING_WIFI;
    if (cfg.enable_wifi_raw_upload) {
        stats_.wifi_attempted = true;
        const int64_t t0 = esp_timer_get_time();
        ESP_LOGI(TAG, "WiFi connect");
        esp_err_t err = upload::wifi_uploader().upload_capture(capture, stats_.wifi);
        stats_.wifi_success = err == ESP_OK && stats_.wifi.upload_complete;
        stats_.wifi_bytes = stats_.wifi.bytes_sent;
        stats_.wifi_time_ms = static_cast<uint32_t>((esp_timer_get_time() - t0) / 1000);
        if (stats_.wifi_success) {
            ESP_LOGI(TAG, "WiFi OK bytes=%u time_ms=%u", static_cast<unsigned>(stats_.wifi_bytes),
                     static_cast<unsigned>(stats_.wifi_time_ms));
        } else {
            ESP_LOGW(TAG, "WiFi %s", stats_.wifi.failure_stage);
        }
    }

    state_ = HybridState::CLEANUP;
    stats_.total_communication_time_ms = static_cast<uint32_t>((esp_timer_get_time() - t_start) / 1000);
    const bool lora_ok = stats_.lora_success;
    const bool wifi_ok = stats_.wifi_success;
    stats_.result = lora_ok ? (wifi_ok ? HybridResult::BOTH_OK : HybridResult::LORA_OK_WIFI_FAILED)
                            : (wifi_ok ? HybridResult::LORA_FAILED_WIFI_OK : HybridResult::BOTH_FAILED);
    state_ = (lora_ok || wifi_ok) ? HybridState::COMPLETE : HybridState::FAILED;
    ESP_LOGI(TAG, "result=%s total_ms=%u", hybrid_result_name(stats_.result),
             static_cast<unsigned>(stats_.total_communication_time_ms));
    return stats_.result == HybridResult::BOTH_OK ? ESP_OK : COWNECT_ERR_INCOMPLETE;
}

}  // namespace cownect::hybrid
